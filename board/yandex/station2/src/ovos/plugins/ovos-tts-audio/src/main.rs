use atrios_ovos_plugin_sdk::{
    bus_channel, load_plugin_settings, run_service_plugin, BusSender, OvosMessage, OvosPlugin,
    MSG_AUDIO_OUTPUT_END, MSG_AUDIO_OUTPUT_START, MSG_PLUGIN_ERROR, MSG_SPEAK, MSG_STOP,
    MSG_WAKEWORD,
};
use serde_json::json;
use std::io::Write;
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Instant;
use tracing::{error, info};

const SR: u32 = 16000;

pub fn synthesize_chime_pcm(whisper: bool) -> Vec<u8> {
    let samples = (SR as usize * 75) / 1000; // 75 ms crisp chime
    let amp = if whisper { 2600.0f32 } else { 7200.0f32 };
    let mut pcm = Vec::with_capacity(samples * 2);
    for i in 0..samples {
        let t = i as f32 / SR as f32;
        let env = (i as f32 / 120.0).min(1.0) * ((samples - i) as f32 / 240.0).min(1.0);
        let freq = if i < samples / 2 { 660.0 } else { 880.0 };
        let v = ((2.0 * std::f32::consts::PI * freq * t).sin() * amp * env) as i16;
        pcm.extend_from_slice(&v.to_le_bytes());
    }
    pcm
}

pub fn scale_wav_pcm(wav: &mut [u8], gain: f32) {
    if wav.len() <= 44 || (gain - 1.0).abs() < 0.01 {
        return;
    }
    for chunk in wav[44..].chunks_exact_mut(2) {
        let s = i16::from_le_bytes([chunk[0], chunk[1]]) as f32;
        let scaled = (s * gain).clamp(-32768.0, 32767.0) as i16;
        let b = scaled.to_le_bytes();
        chunk[0] = b[0];
        chunk[1] = b[1];
    }
}

fn trigger_light(args: &[&str]) {
    let _ = Command::new("/usr/lib/atrios/light")
        .args(args)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn();
}

struct TtsAudioPlugin {
    sender: BusSender,
    active_child: Arc<Mutex<Option<Child>>>,
    cancelled: Arc<AtomicBool>,
}

impl TtsAudioPlugin {
    fn stop_playback(&self) {
        self.cancelled.store(true, Ordering::SeqCst);
        if let Ok(mut guard) = self.active_child.lock() {
            if let Some(mut child) = guard.take() {
                let _ = child.kill();
                let _ = child.wait();
            }
        }
    }

    fn play_chime(&self, whisper: bool) {
        let pcm = synthesize_chime_pcm(whisper);
        std::thread::spawn(move || {
            if let Ok(mut child) = Command::new("/usr/bin/aplay")
                .args(["-q", "-t", "raw", "-f", "S16_LE", "-r", "16000", "-c", "1"])
                .stdin(Stdio::piped())
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
            {
                if let Some(mut stdin) = child.stdin.take() {
                    let _ = stdin.write_all(&pcm);
                }
                let _ = child.wait();
            }
        });
    }

    fn speak_async(&self, utterance: String, whisper: bool) {
        self.stop_playback();
        self.cancelled.store(false, Ordering::SeqCst);
        let sender = self.sender.clone();
        let active_child = Arc::clone(&self.active_child);
        let cancelled = Arc::clone(&self.cancelled);

        std::thread::spawn(move || {
            let t0 = Instant::now();
            let settings = load_plugin_settings("ovos-tts-audio");
            let speed = settings
                .get("speed")
                .and_then(|v| v.as_i64())
                .unwrap_or(155)
                .clamp(100, 220)
                .to_string();

            let _ = sender.emit(OvosMessage::new(
                MSG_AUDIO_OUTPUT_START,
                json!({"whisper": whisper}),
            ));
            trigger_light(&["ring", "amber_green", "60"]);
            trigger_light(&["screen", "eyes", "speaking:default"]);

            let voice = if whisper { "ru+whisper" } else { "ru" };
            let synth = Command::new("/usr/bin/espeak-ng")
                .args(["-v", voice, "-s", &speed, "--stdout", &utterance])
                .stdin(Stdio::null())
                .stdout(Stdio::piped())
                .stderr(Stdio::null())
                .output();

            match synth {
                Ok(out) if out.status.success() && out.stdout.len() > 44 => {
                    let mut wav = out.stdout;
                    if whisper {
                        scale_wav_pcm(&mut wav, 0.35);
                    }
                    let synth_ms = t0.elapsed().as_millis() as u64;
                    if !cancelled.load(Ordering::SeqCst) {
                        match Command::new("/usr/bin/aplay")
                            .args(["-q", "-t", "wav"])
                            .stdin(Stdio::piped())
                            .stdout(Stdio::null())
                            .stderr(Stdio::null())
                            .spawn()
                        {
                            Ok(mut child) => {
                                let stdin_opt = child.stdin.take();
                                {
                                    if let Ok(mut g) = active_child.lock() {
                                        *g = Some(child);
                                    }
                                }
                                if let Some(mut stdin) = stdin_opt {
                                    let _ = stdin.write_all(&wav);
                                }
                                // Wait for playback completion unless killed by mycroft.stop
                                loop {
                                    if cancelled.load(Ordering::SeqCst) {
                                        break;
                                    }
                                    let done = active_child
                                        .lock()
                                        .ok()
                                        .and_then(|mut g| {
                                            if let Some(c) = g.as_mut() {
                                                match c.try_wait() {
                                                    Ok(Some(_)) => {
                                                        g.take();
                                                        Some(true)
                                                    }
                                                    Ok(None) => Some(false),
                                                    Err(_) => {
                                                        g.take();
                                                        Some(true)
                                                    }
                                                }
                                            } else {
                                                Some(true)
                                            }
                                        })
                                        .unwrap_or(true);
                                    if done {
                                        break;
                                    }
                                    std::thread::sleep(std::time::Duration::from_millis(15));
                                }
                                info!(
                                    synth_ms,
                                    total_ms = t0.elapsed().as_millis() as u64,
                                    whisper,
                                    "ovos-tts-audio: finished speak"
                                );
                            }
                            Err(e) => {
                                error!("aplay failed: {e}");
                                let _ = sender.emit(OvosMessage::plugin_error(
                                    "ovos-tts-audio",
                                    format!("aplay failed: {e}"),
                                ));
                            }
                        }
                    }
                }
                Ok(out) => {
                    let msg = format!("espeak-ng exited {}", out.status);
                    error!("{msg}");
                    let _ = sender.emit(OvosMessage::plugin_error("ovos-tts-audio", msg));
                }
                Err(e) => {
                    let msg = format!("espeak-ng failed: {e}");
                    error!("{msg}");
                    let _ = sender.emit(OvosMessage::plugin_error("ovos-tts-audio", msg));
                }
            }

            let _ = sender.emit(OvosMessage::new(MSG_AUDIO_OUTPUT_END, json!({})));
            trigger_light(&["ring", "off"]);
            trigger_light(&["screen", "clock"]);
        });
    }
}

impl OvosPlugin for TtsAudioPlugin {
    fn id(&self) -> &'static str {
        "ovos-tts-audio"
    }

    fn handle(&mut self, message: &OvosMessage) -> Vec<OvosMessage> {
        match message.message_type.as_str() {
            MSG_STOP => {
                info!("ovos-tts-audio: received mycroft.stop -> interrupting playback");
                self.stop_playback();
                trigger_light(&["ring", "off"]);
                trigger_light(&["screen", "clock"]);
                vec![OvosMessage::new(MSG_AUDIO_OUTPUT_END, json!({"stopped": true}))]
            }
            MSG_WAKEWORD => {
                let whisper = message
                    .data
                    .get("whisper")
                    .and_then(|v| v.as_bool())
                    .unwrap_or(false);
                self.play_chime(whisper);
                Vec::new()
            }
            MSG_SPEAK => {
                let utterance = message
                    .data
                    .get("utterance")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .trim()
                    .to_string();
                let whisper = message
                    .data
                    .get("whisper")
                    .and_then(|v| v.as_bool())
                    .unwrap_or(false);
                if !utterance.is_empty() {
                    self.speak_async(utterance, whisper);
                }
                Vec::new()
            }
            _ => Vec::new(),
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env().unwrap_or_else(|_| "info".into()),
        )
        .init();
    let (sender, rx) = bus_channel();
    let plugin = TtsAudioPlugin {
        sender,
        active_child: Arc::new(Mutex::new(None)),
        cancelled: Arc::new(AtomicBool::new(false)),
    };
    run_service_plugin(plugin, rx).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn chime_and_whisper_scaling_stay_in_pcm16_bounds() {
        let normal = synthesize_chime_pcm(false);
        let quiet = synthesize_chime_pcm(true);
        assert_eq!(normal.len(), quiet.len());
        assert_eq!(normal.len(), (SR as usize * 75 / 1000) * 2);

        let mut wav = vec![0u8; 44 + 8];
        for (i, s) in [10000i16, -10000, 20000, -20000].iter().enumerate() {
            wav[44 + i * 2..44 + i * 2 + 2].copy_from_slice(&s.to_le_bytes());
        }
        scale_wav_pcm(&mut wav, 0.35);
        let s0 = i16::from_le_bytes([wav[44], wav[45]]);
        assert!((s0 - 3500).abs() <= 1);
    }
}
