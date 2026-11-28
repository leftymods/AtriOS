use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use std::time::Duration;
use tokio::time::sleep;
use tokio_tungstenite::connect_async;
use tokio_tungstenite::tungstenite::Message;
use tracing::warn;

const MAX_OUTGOING_MESSAGE_BYTES: usize = 1024 * 1024;

#[derive(Debug, Clone, Serialize, Deserialize)]
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
        Self { message_type: message_type.into(), data, context: Value::Object(Map::new()) }
    }
}

pub trait OvosPlugin: Send + 'static {
    fn id(&self) -> &'static str;
    /// Handle one OVOS message and return zero or more messages to broadcast.
    /// Keep this callback short; move long DSP or inference work to a worker thread.
    fn handle(&mut self, message: &OvosMessage) -> Vec<OvosMessage>;
}

fn bus_url() -> Result<String, String> {
    let host = std::env::var("OVOS_BUS_HOST").unwrap_or_else(|_| "127.0.0.1".into());
    if host != "127.0.0.1" && host != "::1" && host != "[::1]" {
        return Err("Rust plugins may connect only to a loopback OVOS MessageBus".into());
    }
    let port = std::env::var("OVOS_BUS_PORT").ok()
        .and_then(|value| value.parse::<u16>().ok()).filter(|port| *port != 0).unwrap_or(8181);
    let route = std::env::var("OVOS_BUS_ROUTE").unwrap_or_else(|_| "/core".into());
    if !route.starts_with('/') || route.contains('?') || route.contains('#') {
        return Err("invalid OVOS bus route".into());
    }
    let host = if host == "::1" { "[::1]" } else { host.as_str() };
    Ok(format!("ws://{host}:{port}{route}"))
}

async fn connect_and_run<P: OvosPlugin>(plugin: &mut P, url: &str) -> Result<(), String> {
    let (mut socket, _) = connect_async(url).await.map_err(|error| error.to_string())?;
    while let Some(frame) = socket.next().await {
        match frame {
            Ok(Message::Text(text)) => {
                let message: OvosMessage = match serde_json::from_str(&text) {
                    Ok(message) => message,
                    Err(_) => continue,
                };
                if message.message_type == "connected"
                    || message.context.get("source").and_then(Value::as_str) == Some(plugin.id()) {
                    continue;
                }
                let outputs = plugin.handle(&message);
                if outputs.len() > 32 { return Err("plugin produced more than 32 messages for one event".into()); }
                for mut output in outputs {
                    if output.message_type.is_empty() || output.message_type.len() > 256 {
                        return Err("plugin produced an invalid OVOS message type".into());
                    }
                    if !output.context.is_object() {
                        output.context = if message.context.is_object() { message.context.clone() } else { Value::Object(Map::new()) };
                    }
                    if let Value::Object(context) = &mut output.context {
                        context.insert("source".into(), Value::String(plugin.id().into()));
                    }
                    let serialized = serde_json::to_string(&output).map_err(|error| error.to_string())?;
                    if serialized.len() > MAX_OUTGOING_MESSAGE_BYTES {
                        return Err("plugin message exceeds 1 MiB".into());
                    }
                    socket.send(Message::Text(serialized.into())).await.map_err(|error| error.to_string())?;
                }
            }
            Ok(Message::Ping(payload)) => {
                socket.send(Message::Pong(payload)).await.map_err(|error| error.to_string())?;
            }
            Ok(Message::Close(_)) | Err(_) => return Ok(()),
            Ok(_) => {}
        }
    }
    Ok(())
}

/// Run a native Rust plugin as an OVOS bus service. It reconnects with bounded
/// backoff and refuses non-loopback message buses.
pub async fn run_plugin<P: OvosPlugin>(mut plugin: P) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let id = plugin.id();
    if id.is_empty() || id.len() > 64
        || !id.bytes().all(|b| b.is_ascii_alphanumeric() || matches!(b, b'_' | b'-' | b'.')) {
        return Err("invalid plugin id".into());
    }
    if let Ok(host_id) = std::env::var("OVOS_PLUGIN_ID") {
        if host_id != id { return Err("plugin id does not match the supervisor manifest".into()); }
    }
    let url = bus_url().map_err(std::io::Error::other)?;
    let mut delay = 1u64;
    loop {
        if let Err(error) = connect_and_run(&mut plugin, &url).await {
            warn!(plugin = id, %error, "OVOS plugin connection failed");
        } else {
            warn!(plugin = id, "OVOS MessageBus connection closed; reconnecting");
        }
        sleep(Duration::from_secs(delay)).await;
        delay = (delay * 2).min(30);
    }
}
