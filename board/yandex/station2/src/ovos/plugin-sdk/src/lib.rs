use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};
use std::fs;
use std::path::Path;
use std::time::Duration;
use tokio::sync::mpsc;
use tokio::time::sleep;
use tokio_tungstenite::connect_async;
use tokio_tungstenite::tungstenite::Message;
use tracing::warn;

pub const MAX_OUTGOING_MESSAGE_BYTES: usize = 1024 * 1024;
pub const PLUGIN_SETTINGS_FILE: &str = "/var/lib/atrios/ovos/plugin-settings.json";

// Standard OpenVoiceOS MessageBus event types (Apache-2.0 protocol contract)
pub const MSG_WAKEWORD: &str = "recognizer_loop:wakeword";
pub const MSG_RECORD_BEGIN: &str = "recognizer_loop:record_begin";
pub const MSG_RECORD_END: &str = "recognizer_loop:record_end";
pub const MSG_UTTERANCE: &str = "recognizer_loop:utterance";
pub const MSG_AUDIO_OUTPUT_START: &str = "recognizer_loop:audio_output_start";
pub const MSG_AUDIO_OUTPUT_END: &str = "recognizer_loop:audio_output_end";
pub const MSG_SPEAK: &str = "speak";
pub const MSG_STOP: &str = "mycroft.stop";
pub const MSG_PLUGIN_STATUS: &str = "ovos.plugin.status";
pub const MSG_PLUGIN_ERROR: &str = "ovos.plugin.error";

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct OvosMessage {
    #[serde(rename = "type")]
    pub message_type: String,
    #[serde(default)]
    pub data: Value,
    #[serde(default)]
    pub context: Value,
}

impl OvosMessage {
    pub fn new(message_type: impl Into<String>, data: Value) -> Self {
        Self {
            message_type: message_type.into(),
            data,
            context: Value::Object(Map::new()),
        }
    }

    pub fn with_context(
        message_type: impl Into<String>,
        data: Value,
        context: Value,
    ) -> Self {
        Self {
            message_type: message_type.into(),
            data,
            context: if context.is_object() {
                context
            } else {
                Value::Object(Map::new())
            },
        }
    }

    pub fn speak(utterance: impl Into<String>, lang: &str, whisper: bool) -> Self {
        Self::new(
            MSG_SPEAK,
            json!({
                "utterance": utterance.into(),
                "lang": lang,
                "whisper": whisper,
            }),
        )
    }

    pub fn stop() -> Self {
        Self::new(MSG_STOP, json!({}))
    }

    pub fn plugin_error(plugin_id: &str, error: impl Into<String>) -> Self {
        Self::new(
            MSG_PLUGIN_ERROR,
            json!({
                "plugin_id": plugin_id,
                "error": error.into(),
            }),
        )
    }
}

/// Thread-safe handle allowing background DSP/audio worker threads to emit
/// OVOS messages onto the loopback MessageBus without blocking incoming events.
#[derive(Clone)]
pub struct BusSender {
    tx: mpsc::UnboundedSender<OvosMessage>,
}

impl BusSender {
    pub fn emit(&self, message: OvosMessage) -> Result<(), String> {
        self.tx
            .send(message)
            .map_err(|_| "OVOS bus sender closed".to_string())
    }
}

pub trait OvosPlugin: Send + 'static {
    fn id(&self) -> &'static str;
    /// Handle one OVOS message and return zero or more messages to broadcast.
    /// Keep this callback short; move long DSP or inference work to a worker thread.
    fn handle(&mut self, message: &OvosMessage) -> Vec<OvosMessage>;
}

/// Load this plugin's persisted settings map from `/var/lib/atrios/ovos/plugin-settings.json`.
pub fn load_plugin_settings(plugin_id: &str) -> Map<String, Value> {
    let path = Path::new(PLUGIN_SETTINGS_FILE);
    let Ok(raw) = fs::read_to_string(path) else {
        return Map::new();
    };
    let Ok(root) = serde_json::from_str::<Value>(&raw) else {
        return Map::new();
    };
    root.get("plugins")
        .and_then(|p| p.get(plugin_id))
        .and_then(Value::as_object)
        .cloned()
        .unwrap_or_default()
}

pub fn bus_url() -> Result<String, String> {
    let host = std::env::var("OVOS_BUS_HOST").unwrap_or_else(|_| "127.0.0.1".into());
    bus_url_from_parts(
        &host,
        std::env::var("OVOS_BUS_PORT")
            .ok()
            .and_then(|value| value.parse::<u16>().ok())
            .filter(|port| *port != 0)
            .unwrap_or(8181),
        &std::env::var("OVOS_BUS_ROUTE").unwrap_or_else(|_| "/core".into()),
    )
}

pub fn bus_url_from_parts(host: &str, port: u16, route: &str) -> Result<String, String> {
    if host != "127.0.0.1" && host != "::1" && host != "[::1]" {
        return Err("Rust plugins may connect only to a loopback OVOS MessageBus".into());
    }
    if port == 0 {
        return Err("invalid OVOS bus port".into());
    }
    if !route.starts_with('/') || route.contains('?') || route.contains('#') {
        return Err("invalid OVOS bus route".into());
    }
    let host = if host == "::1" { "[::1]" } else { host };
    Ok(format!("ws://{host}:{port}{route}"))
}

pub fn serialize_outgoing(
    plugin_id: &str,
    mut output: OvosMessage,
    fallback_context: Option<&Value>,
) -> Result<String, String> {
    if output.message_type.is_empty() || output.message_type.len() > 256 {
        return Err("plugin produced an invalid OVOS message type".into());
    }
    if !output.context.is_object() {
        output.context = fallback_context
            .filter(|c| c.is_object())
            .cloned()
            .unwrap_or_else(|| Value::Object(Map::new()));
    }
    if let Value::Object(context) = &mut output.context {
        context.insert("source".into(), Value::String(plugin_id.into()));
    }
    let serialized = serde_json::to_string(&output).map_err(|error| error.to_string())?;
    if serialized.len() > MAX_OUTGOING_MESSAGE_BYTES {
        return Err("plugin message exceeds 1 MiB".into());
    }
    Ok(serialized)
}

fn validate_plugin_id(id: &str) -> Result<(), String> {
    if id.is_empty()
        || id.len() > 64
        || !id
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || matches!(b, b'_' | b'-' | b'.'))
    {
        return Err("invalid plugin id".into());
    }
    if let Ok(host_id) = std::env::var("OVOS_PLUGIN_ID") {
        if host_id != id {
            return Err("plugin id does not match the supervisor manifest".into());
        }
    }
    Ok(())
}

async fn connect_and_run_duplex<P: OvosPlugin>(
    plugin: &mut P,
    url: &str,
    out_rx: &mut mpsc::UnboundedReceiver<OvosMessage>,
) -> Result<(), String> {
    let (mut socket, _) = connect_async(url)
        .await
        .map_err(|error| error.to_string())?;
    loop {
        tokio::select! {
            maybe_out = out_rx.recv() => {
                let Some(output) = maybe_out else {
                    return Ok(());
                };
                let serialized = serialize_outgoing(plugin.id(), output, None)?;
                socket
                    .send(Message::Text(serialized.into()))
                    .await
                    .map_err(|error| error.to_string())?;
            }
            maybe_frame = socket.next() => {
                let Some(frame) = maybe_frame else {
                    return Ok(());
                };
                match frame {
                    Ok(Message::Text(text)) => {
                        let message: OvosMessage = match serde_json::from_str(&text) {
                            Ok(message) => message,
                            Err(_) => continue,
                        };
                        if message.message_type == "connected"
                            || message.context.get("source").and_then(Value::as_str) == Some(plugin.id())
                        {
                            continue;
                        }
                        let outputs = plugin.handle(&message);
                        if outputs.len() > 32 {
                            return Err("plugin produced more than 32 messages for one event".into());
                        }
                        for output in outputs {
                            let serialized = serialize_outgoing(plugin.id(), output, Some(&message.context))?;
                            socket
                                .send(Message::Text(serialized.into()))
                                .await
                                .map_err(|error| error.to_string())?;
                        }
                    }
                    Ok(Message::Ping(payload)) => {
                        socket
                            .send(Message::Pong(payload))
                            .await
                            .map_err(|error| error.to_string())?;
                    }
                    Ok(Message::Close(_)) | Err(_) => return Ok(()),
                    Ok(_) => {}
                }
            }
        }
    }
}

/// Run a native Rust plugin as an OVOS bus service. It reconnects with bounded
/// backoff and refuses non-loopback message buses.
pub async fn run_plugin<P: OvosPlugin>(
    plugin: P,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let (_tx, rx) = mpsc::unbounded_channel();
    run_service_plugin(plugin, rx).await
}

/// Create an async `BusSender` + receiver pair for active producer plugins
/// (such as audio listeners or TTS playback workers).
pub fn bus_channel() -> (BusSender, mpsc::UnboundedReceiver<OvosMessage>) {
    let (tx, rx) = mpsc::unbounded_channel();
    (BusSender { tx }, rx)
}

/// Run a duplex native Rust OVOS plugin that both handles incoming bus events
/// and emits asynchronous events produced by background threads.
pub async fn run_service_plugin<P: OvosPlugin>(
    mut plugin: P,
    mut out_rx: mpsc::UnboundedReceiver<OvosMessage>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let id = plugin.id();
    validate_plugin_id(id).map_err(std::io::Error::other)?;
    let url = bus_url().map_err(std::io::Error::other)?;
    let mut delay = 1u64;
    loop {
        if let Err(error) = connect_and_run_duplex(&mut plugin, &url, &mut out_rx).await {
            warn!(plugin = id, %error, "OVOS plugin connection failed");
        } else {
            warn!(plugin = id, "OVOS MessageBus connection closed; reconnecting");
        }
        sleep(Duration::from_secs(delay)).await;
        delay = (delay * 2).min(30);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_non_loopback_bus_hosts() {
        assert!(bus_url_from_parts("127.0.0.1", 8181, "/core").is_ok());
        assert!(bus_url_from_parts("::1", 8181, "/core").is_ok());
        assert!(bus_url_from_parts("0.0.0.0", 8181, "/core").is_err());
        assert!(bus_url_from_parts("192.168.1.5", 8181, "/core").is_err());
        assert!(bus_url_from_parts("127.0.0.1", 8181, "/core?x=1").is_err());
    }

    #[test]
    fn ovos_envelope_compatibility_and_source_tagging() {
        let msg = OvosMessage::speak("Привет", "ru-ru", true);
        let raw = serialize_outgoing("ovos-intent-router", msg, None).unwrap();
        let parsed: OvosMessage = serde_json::from_str(&raw).unwrap();
        assert_eq!(parsed.message_type, MSG_SPEAK);
        assert_eq!(parsed.data["utterance"], "Привет");
        assert_eq!(parsed.data["lang"], "ru-ru");
        assert_eq!(parsed.data["whisper"], true);
        assert_eq!(parsed.context["source"], "ovos-intent-router");
    }

    #[test]
    fn rejects_empty_type_and_oversized_messages() {
        assert!(serialize_outgoing("p", OvosMessage::new("", json!({})), None).is_err());
        let huge = "x".repeat(MAX_OUTGOING_MESSAGE_BYTES + 16);
        assert!(serialize_outgoing("p", OvosMessage::new("speak", json!({"u": huge})), None).is_err());
    }
}
