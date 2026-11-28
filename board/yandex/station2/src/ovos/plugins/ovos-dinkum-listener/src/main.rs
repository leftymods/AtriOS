use atrios_ovos_plugin_sdk::{
    bus_channel, load_plugin_settings, run_service_plugin, BusSender, OvosMessage, OvosPlugin,
    MSG_AUDIO_OUTPUT_END, MSG_AUDIO_OUTPUT_START, MSG_RECORD_BEGIN,
    MSG_RECORD_END, MSG_STOP, MSG_UTTERANCE, MSG_WAKEWORD,
};
use serde_json::json;
use std::collections::VecDeque;
use std::ffi::{CStr, CString};
use std::io::Read;
use std::os::raw::{c_char, c_float, c_int, c_void};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tracing::{error, info, warn};

const SR: usize = 16000;
const FRAME_SAMPLES: usize = 512; // 32 ms @ 16 kHz
const MONO_FRAME_BYTES: usize = FRAME_SAMPLES * 2; // 1024 B
const DUAL_FRAME_BYTES: usize = MONO_FRAME_BYTES * 2; // 2048 B interleaved (vqe_0, vqe_bnr_0)
const PRE_ROLL_FRAMES: usize = 9; // 288 ms bounded pre-roll
const MAX_UTTERANCE_BYTES: usize = SR * 2 * 8; // 8 s hard upper bound (256 KiB)
const ACTIVE_WINDOW_SECS: u64 = 7;

const VOSK_LIB_PATHS: &[&str] = &[
    "/usr/lib/atrios/libvosk.so",
    "/usr/lib/libvosk.so",
    "/usr/lib/aarch64-linux-gnu/libvosk.so",
];
const VOSK_MODEL_PATHS: &[&str] = &[
    "/usr/share/atrios/models/vosk-model-small-ru",
    "/var/lib/atrios/models/vosk-model-small-ru",
];

const FAST_GRAMMAR: &str = r#"["атри", "а три", "алиса", "стоп", "хватит", "замолчи", "отмена", "тише", "потише", "громче", "погромче", "дальше", "следующий", "назад", "пауза", "продолжи", "играй", "привет", "время", "[unk]"]"#;

const PRIMARY_WAKES: &[&str] = &["атри", "а три", "atri", "алиса", "alice"];
const FUZZY_WAKES: &[&str] = &[
    "атли", "отли", "отри", "атре", "атрий", "атрия", "арти", "адри", "одри", "атры",
];
const STOP_WORDS: &[&str] = &["стоп", "хватит", "замолчи", "отмена", "прекрати"];
const QUICK_WORDS: &[&str] = &[
    "тише",
    "потише",
    "громче",
    "погромче",
    "дальше",
    "следующий",
    "назад",
    "пауза",
    "продолжи",
    "играй",
];

extern "C" {
    fn dlopen(filename: *const c_char, flag: c_int) -> *mut c_void;
    fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
}
const RTLD_NOW: c_int = 2;

struct VoskEngine {
    model: *mut c_void,
    rec_fast: *mut c_void,
    rec_full: *mut c_void,
    accept: unsafe extern "C" fn(*mut c_void, *const c_char, c_int) -> c_int,
    final_res: unsafe extern "C" fn(*mut c_void) -> *const c_char,
    reset: unsafe extern "C" fn(*mut c_void),
}

unsafe impl Send for VoskEngine {}

impl VoskEngine {
    fn load() -> Result<Self, String> {
        let mut handle = std::ptr::null_mut();
        for p in VOSK_LIB_PATHS {
            let c = CString::new(*p).unwrap();
            let h = unsafe { dlopen(c.as_ptr(), RTLD_NOW) };
            if !h.is_null() {
                handle = h;
                break;
            }
        }
        if handle.is_null() {
            return Err("libvosk.so not found".into());
        }
        macro_rules! sym {
            ($name:expr) => {{
                let c = CString::new($name).unwrap();
                let ptr = dlsym(handle, c.as_ptr());
                if ptr.is_null() {
                    return Err(format!("missing symbol {}", $name));
                }
                ptr
            }};
        }
        let set_log: unsafe extern "C" fn(c_int) = unsafe { std::mem::transmute(sym!("vosk_set_log_level")) };
        let model_new: unsafe extern "C" fn(*const c_char) -> *mut c_void =
            unsafe { std::mem::transmute(sym!("vosk_model_new")) };
        let rec_new: unsafe extern "C" fn(*mut c_void, c_float) -> *mut c_void =
            unsafe { std::mem::transmute(sym!("vosk_recognizer_new")) };
        let rec_new_grm: unsafe extern "C" fn(*mut c_void, c_float, *const c_char) -> *mut c_void =
            unsafe { std::mem::transmute(sym!("vosk_recognizer_new_grm")) };
        let accept = unsafe { std::mem::transmute(sym!("vosk_recognizer_accept_waveform")) };
        let final_res = unsafe { std::mem::transmute(sym!("vosk_recognizer_final_result")) };
        let reset = unsafe { std::mem::transmute(sym!("vosk_recognizer_reset")) };

        unsafe { set_log(-1) };
        let mut model = std::ptr::null_mut();
        for mp in VOSK_MODEL_PATHS {
            if std::path::Path::new(mp).is_dir() {
                let c = CString::new(*mp).unwrap();
                model = unsafe { model_new(c.as_ptr()) };
                if !model.is_null() {
                    break;
                }
            }
        }
        if model.is_null() {
            return Err("vosk-model-small-ru not found".into());
        }
        let grm = CString::new(FAST_GRAMMAR).unwrap();
        let rec_fast = unsafe { rec_new_grm(model, SR as c_float, grm.as_ptr()) };
        let rec_full = unsafe { rec_new(model, SR as c_float) };
        if rec_fast.is_null() || rec_full.is_null() {
            return Err("failed to create Vosk recognizers".into());
        }
        Ok(Self {
            model,
            rec_fast,
            rec_full,
            accept,
            final_res,
            reset,
        })
    }

    fn decode(&mut self, rec: *mut c_void, pcm: &[u8]) -> String {
        if pcm.is_empty() {
            return String::new();
        }
        unsafe {
            (self.reset)(rec);
            (self.accept)(rec, pcm.as_ptr() as *const c_char, pcm.len() as c_int);
            let raw = (self.final_res)(rec);
            if raw.is_null() {
                return String::new();
            }
            let s = CStr::from_ptr(raw).to_string_lossy();
            serde_json::from_str::<serde_json::Value>(&s)
                .ok()
                .and_then(|v| v.get("text").and_then(|t| t.as_str()).map(str::to_string))
                .unwrap_or_default()
                .trim()
                .to_string()
        }
    }

    fn decode_fast(&mut self, pcm: &[u8]) -> String {
        let _ = self.model;
        self.decode(self.rec_fast, pcm)
    }

    fn decode_full(&mut self, pcm: &[u8]) -> String {
        self.decode(self.rec_full, pcm)
    }
}

pub fn deinterleave_dual_vqe(stereo: &[u8], use_bnr: bool) -> Vec<u8> {
    let samples = stereo.len() / 4;
    let mut out = Vec::with_capacity(samples * 2);
    for frame in stereo.chunks_exact(4) {
        if use_bnr {
            out.extend_from_slice(&frame[2..4]);
        } else {
            out.extend_from_slice(&frame[0..2]);
        }
    }
    out
}

pub fn frame_rms(pcm: &[u8]) -> f32 {
    let n = pcm.len() / 2;
    if n == 0 {
        return 0.0;
    }
    let mut sum = 0.0f64;
    for c in pcm.chunks_exact(2) {
        let s = i16::from_le_bytes([c[0], c[1]]) as f64;
        sum += s * s;
    }
    (sum / n as f64).sqrt() as f32
}

pub fn boost_whisper_pcm(pcm: &[u8]) -> Vec<u8> {
    let rms = frame_rms(pcm) / 32768.0;
    if rms <= 0.0008 || rms >= 0.075 {
        return pcm.to_vec();
    }
    let gain = (0.10 / rms).min(28.0);
    let mut out = Vec::with_capacity(pcm.len());
    for c in pcm.chunks_exact(2) {
        let s = i16::from_le_bytes([c[0], c[1]]) as f32 / 32768.0;
        let boosted = ((s * gain).tanh() * 32767.0).clamp(-32768.0, 32767.0) as i16;
        out.extend_from_slice(&boosted.to_le_bytes());
    }
    out
}

pub fn normalize_ru(text: &str) -> String {
    text.to_lowercase()
        .replace('ё', "е")
        .chars()
        .map(|c| if c.is_alphanumeric() || c.is_whitespace() { c } else { ' ' })
        .collect::<String>()
        .split_whitespace()
        .collect::<Vec<_>>()
        .join(" ")
}

pub fn is_stop_phrase(text: &str) -> bool {
    let n = normalize_ru(text);
    STOP_WORDS.iter().any(|w| n == *w || n.starts_with(&format!("{w} ")))
}

pub fn is_quick_command(text: &str) -> bool {
    let n = normalize_ru(text);
    QUICK_WORDS.iter().any(|w| n == *w)
}

pub fn match_wake(text: &str, custom_wake: &str) -> (bool, bool, String) {
    let n = normalize_ru(text);
    if n.is_empty() {
        return (false, false, String::new());
    }
    let cw = normalize_ru(custom_wake);
    if !cw.is_empty() && (n == cw || n.starts_with(&format!("{cw} "))) {
        let rest = n.strip_prefix(&cw).unwrap_or("").trim().to_string();
        return (true, true, rest);
    }
    for w in PRIMARY_WAKES {
        if n == *w || n.starts_with(&format!("{w} ")) {
            let rest = n.strip_prefix(w).unwrap_or("").trim().to_string();
            return (true, true, rest);
        }
    }
    for w in FUZZY_WAKES {
        if n == *w || n.starts_with(&format!("{w} ")) {
            let rest = n.strip_prefix(w).unwrap_or("").trim().to_string();
            return (true, false, rest);
        }
    }
    (false, false, String::new())
}

pub fn push_bounded_utterance(buf: &mut Vec<u8>, chunk: &[u8]) -> bool {
    let remaining = MAX_UTTERANCE_BYTES.saturating_sub(buf.len());
    if remaining == 0 {
        return true;
    }
    let take = chunk.len().min(remaining);
    buf.extend_from_slice(&chunk[..take]);
    buf.len() >= MAX_UTTERANCE_BYTES
}

fn trigger_light(args: &[&str]) {
    let _ = Command::new("/usr/lib/atrios/light")
        .args(args)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn();
}

fn music_active() -> bool {
    std::fs::read_to_string("/var/run/atrios/ymusic-playing")
        .map(|s| s.trim() == "1")
        .unwrap_or(false)
}

struct ListenerPlugin {
    tts_speaking: Arc<AtomicBool>,
    stop_requested: Arc<AtomicBool>,
}

impl OvosPlugin for ListenerPlugin {
    fn id(&self) -> &'static str {
        "ovos-dinkum-listener"
    }

    fn handle(&mut self, message: &OvosMessage) -> Vec<OvosMessage> {
        match message.message_type.as_str() {
            MSG_AUDIO_OUTPUT_START => {
                self.tts_speaking.store(true, Ordering::SeqCst);
            }
            MSG_AUDIO_OUTPUT_END => {
                self.tts_speaking.store(false, Ordering::SeqCst);
            }
            MSG_STOP => {
                self.stop_requested.store(true, Ordering::SeqCst);
                self.tts_speaking.store(false, Ordering::SeqCst);
            }
            _ => {}
        }
        Vec::new()
    }
}

fn spawn_audio_loop(
    sender: BusSender,
    tts_speaking: Arc<AtomicBool>,
    stop_requested: Arc<AtomicBool>,
) {
    std::thread::spawn(move || {
        let mut vosk = match VoskEngine::load() {
            Ok(v) => {
                info!("ovos-dinkum-listener: loaded Vosk Russian model");
                v
            }
            Err(e) => {
                error!("ovos-dinkum-listener: {e}");
                let _ = sender.emit(OvosMessage::plugin_error("ovos-dinkum-listener", e));
                return;
            }
        };

        loop {
            let settings = load_plugin_settings("ovos-dinkum-listener");
            let custom_wake = settings
                .get("wake_word")
                .and_then(|v| v.as_str())
                .unwrap_or("атри")
                .to_string();
            let whisper_enabled = settings
                .get("whisper_enabled")
                .and_then(|v| v.as_bool())
                .unwrap_or(true);

            let mut child = match Command::new("/usr/lib/atrios/atri-aec")
                .arg("--dual-vqe")
                .stdin(Stdio::null())
                .stdout(Stdio::piped())
                .stderr(Stdio::null())
                .spawn()
            {
                Ok(c) => c,
                Err(e) => {
                    error!("failed to spawn atri-aec --dual-vqe: {e}");
                    let _ = sender.emit(OvosMessage::plugin_error(
                        "ovos-dinkum-listener",
                        format!("atri-aec spawn failed: {e}"),
                    ));
                    std::thread::sleep(Duration::from_secs(3));
                    continue;
                }
            };
            let mut stdout = child.stdout.take().expect("piped stdout");
            info!("ovos-dinkum-listener: connected to atri-aec --dual-vqe (YNDX-0053 MAX)");

            let mut raw = vec![0u8; DUAL_FRAME_BYTES];
            let mut pre_roll: VecDeque<Vec<u8>> = VecDeque::with_capacity(PRE_ROLL_FRAMES);
            let mut utt: Vec<u8> = Vec::with_capacity(SR * 2 * 3);
            let mut noise_rms: f32 = 80.0;
            let mut speaking = false;
            let mut speech_hits: u32 = 0;
            let mut silence_frames: u32 = 0;
            let mut peak_rms: f32 = 0.0;
            let mut active_until: Option<Instant> = None;

            loop {
                if stop_requested.swap(false, Ordering::SeqCst) {
                    active_until = None;
                    speaking = false;
                    utt.clear();
                    trigger_light(&["ring", "off"]);
                    trigger_light(&["screen", "clock"]);
                }
                if let Some(deadline) = active_until {
                    if Instant::now() >= deadline && !speaking {
                        active_until = None;
                        trigger_light(&["ring", "off"]);
                        trigger_light(&["screen", "clock"]);
                    }
                }
                if stdout.read_exact(&mut raw).is_err() {
                    warn!("atri-aec stream ended; restarting capture");
                    let _ = child.kill();
                    let _ = child.wait();
                    break;
                }
                let is_tts = tts_speaking.load(Ordering::Relaxed);
                let use_bnr = is_tts || music_active();
                let mono = deinterleave_dual_vqe(&raw, use_bnr);
                let rms = frame_rms(&mono);

                let start_thr = if use_bnr {
                    (noise_rms * 2.6).max(260.0)
                } else if whisper_enabled {
                    (noise_rms * 1.85).max(115.0)
                } else {
                    (noise_rms * 2.3).max(200.0)
                };
                let keep_thr = (start_thr * 0.55).max(75.0);

                if !speaking {
                    if rms >= start_thr {
                        speech_hits += 1;
                        if speech_hits >= 2 {
                            speaking = true;
                            silence_frames = 0;
                            peak_rms = rms;
                            utt.clear();
                            for pr in &pre_roll {
                                push_bounded_utterance(&mut utt, pr);
                            }
                            push_bounded_utterance(&mut utt, &mono);
                            if active_until.is_some() {
                                let _ = sender.emit(OvosMessage::new(
                                    MSG_RECORD_BEGIN,
                                    json!({"mode": "active"}),
                                ));
                            }
                        }
                    } else {
                        speech_hits = 0;
                        noise_rms = noise_rms * 0.96 + rms * 0.04;
                        if pre_roll.len() == PRE_ROLL_FRAMES {
                            pre_roll.pop_front();
                        }
                        pre_roll.push_back(mono);
                    }
                    continue;
                }

                if rms > peak_rms {
                    peak_rms = rms;
                }
                let hit_max = push_bounded_utterance(&mut utt, &mono);
                if rms < keep_thr {
                    silence_frames += 1;
                } else {
                    silence_frames = 0;
                }

                let end_frames = if active_until.is_some() { 14 } else { 11 }; // 352-448 ms
                if !hit_max && silence_frames < end_frames {
                    continue;
                }

                speaking = false;
                let dur_ms = (utt.len() * 1000) / (SR * 2);
                if dur_ms < 320 {
                    utt.clear();
                    continue;
                }

                let is_whisper = !use_bnr && peak_rms < 1100.0;
                let t0 = Instant::now();
                let mut text = if active_until.is_some() {
                    vosk.decode_full(&utt)
                } else {
                    vosk.decode_fast(&utt)
                };
                if text.is_empty() && whisper_enabled && is_whisper {
                    let boosted = boost_whisper_pcm(&utt);
                    text = vosk.decode_full(&boosted);
                }
                let stt_ms = t0.elapsed().as_millis() as u64;
                let clean = normalize_ru(&text.replace("[unk]", ""));

                if clean.is_empty() {
                    utt.clear();
                    continue;
                }

                // 1. Instant stop / barge-in always wins
                if is_stop_phrase(&clean) {
                    let full_ok = vosk.decode_full(&utt);
                    if is_stop_phrase(&full_ok) || is_whisper {
                        info!(%clean, stt_ms, "ovos-dinkum-listener: instant stop/barge-in");
                        active_until = None;
                        let _ = sender.emit(OvosMessage::stop());
                        trigger_light(&["ring", "off"]);
                        trigger_light(&["screen", "clock"]);
                        utt.clear();
                        continue;
                    }
                }

                // 2. While TTS is speaking, only barge-in stop or explicit wake interrupts
                if is_tts {
                    let (matched, exact, _) = match_wake(&clean, &custom_wake);
                    if matched && exact {
                        info!(%clean, "ovos-dinkum-listener: wake barge-in during TTS");
                        let _ = sender.emit(OvosMessage::stop());
                        active_until = Some(Instant::now() + Duration::from_secs(ACTIVE_WINDOW_SECS));
                        let _ = sender.emit(OvosMessage::new(
                            MSG_WAKEWORD,
                            json!({"wake_word": custom_wake, "whisper": is_whisper}),
                        ));
                        trigger_light(&["ring", "amber", "30"]);
                        trigger_light(&["screen", "eyes", "listening"]);
                    }
                    utt.clear();
                    continue;
                }

                // 3. Active command window (already woke up)
                if active_until.is_some() {
                    active_until = None;
                    let _ = sender.emit(OvosMessage::new(
                        MSG_RECORD_END,
                        json!({"duration_ms": dur_ms, "peak_rms": peak_rms}),
                    ));
                    trigger_light(&["ring", "amber_thinking", "30"]);
                    trigger_light(&["screen", "eyes", "thinking"]);
                    info!(%clean, stt_ms, is_whisper, "ovos-dinkum-listener: emitting active utterance");
                    let _ = sender.emit(OvosMessage::new(
                        MSG_UTTERANCE,
                        json!({
                            "utterances": [clean],
                            "lang": "ru-ru",
                            "whisper": is_whisper,
                            "stt_ms": stt_ms,
                            "peak_rms": peak_rms,
                            "noise_rms": noise_rms,
                        }),
                    ));
                    utt.clear();
                    continue;
                }

                // 4. WakeSpotter mode: strict counter-activation for wake & quick commands
                if is_quick_command(&clean) {
                    let full = normalize_ru(&vosk.decode_full(&utt));
                    if is_quick_command(&full) || (is_whisper && is_quick_command(&clean)) {
                        let cmd_text = if is_quick_command(&full) { full } else { clean };
                        info!(%cmd_text, stt_ms, is_whisper, "ovos-dinkum-listener: quick command");
                        let _ = sender.emit(OvosMessage::new(
                            MSG_UTTERANCE,
                            json!({
                                "utterances": [cmd_text],
                                "lang": "ru-ru",
                                "whisper": is_whisper,
                                "quick_command": true,
                                "stt_ms": stt_ms,
                            }),
                        ));
                    }
                    utt.clear();
                    continue;
                }

                let (matched, exact, rest) = match_wake(&clean, &custom_wake);
                if matched {
                    let confirmed_rest = if exact && rest.is_empty() {
                        Some(String::new())
                    } else {
                        let full = normalize_ru(&vosk.decode_full(&utt));
                        let (fm, _, frest) = match_wake(&full, &custom_wake);
                        if fm { Some(frest) } else { None }
                    };
                    if let Some(cmd_rest) = confirmed_rest {
                        info!(%clean, %cmd_rest, stt_ms, is_whisper, "ovos-dinkum-listener: wake confirmed");
                        let _ = sender.emit(OvosMessage::new(
                            MSG_WAKEWORD,
                            json!({"wake_word": custom_wake, "whisper": is_whisper}),
                        ));
                        if cmd_rest.is_empty() {
                            active_until = Some(Instant::now() + Duration::from_secs(ACTIVE_WINDOW_SECS));
                            trigger_light(&["ring", "amber", "30"]);
                            trigger_light(&["screen", "eyes", "listening"]);
                            let _ = sender.emit(OvosMessage::new(
                                MSG_RECORD_BEGIN,
                                json!({"whisper": is_whisper, "mode": "active"}),
                            ));
                        } else {
                            trigger_light(&["ring", "amber_thinking", "30"]);
                            trigger_light(&["screen", "eyes", "thinking"]);
                            let _ = sender.emit(OvosMessage::new(
                                MSG_UTTERANCE,
                                json!({
                                    "utterances": [cmd_rest],
                                    "lang": "ru-ru",
                                    "whisper": is_whisper,
                                    "stt_ms": stt_ms,
                                }),
                            ));
                        }
                    }
                }
                utt.clear();
            }
        }
    });
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env().unwrap_or_else(|_| "info".into()),
        )
        .init();
    let (sender, rx) = bus_channel();
    let tts_speaking = Arc::new(AtomicBool::new(false));
    let stop_requested = Arc::new(AtomicBool::new(false));
    spawn_audio_loop(
        sender,
        Arc::clone(&tts_speaking),
        Arc::clone(&stop_requested),
    );
    let plugin = ListenerPlugin {
        tts_speaking,
        stop_requested,
    };
    run_service_plugin(plugin, rx).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn audio_buffer_enforces_hard_upper_bound() {
        let mut buf = Vec::new();
        let chunk = vec![0u8; 4096];
        let mut hit = false;
        for _ in 0..100 {
            if push_bounded_utterance(&mut buf, &chunk) {
                hit = true;
                break;
            }
        }
        assert!(hit);
        assert_eq!(buf.len(), MAX_UTTERANCE_BYTES);
    }

    #[test]
    fn dual_vqe_deinterleave_selects_clean_or_bnr_channel() {
        let stereo = [1u8, 0, 9, 0, 2, 0, 10, 0];
        assert_eq!(deinterleave_dual_vqe(&stereo, false), vec![1, 0, 2, 0]);
        assert_eq!(deinterleave_dual_vqe(&stereo, true), vec![9, 0, 10, 0]);
    }

    #[test]
    fn wake_and_negative_words_behave_strictly() {
        assert_eq!(match_wake("атри", "атри"), (true, true, "".into()));
        assert_eq!(
            match_wake("атри который час", "атри"),
            (true, true, "который час".into())
        );
        assert_eq!(match_wake("арти", "атри"), (true, false, "".into()));
        for neg in ["арбуз", "армия", "адрес", "апрель", "андрей", "артем"] {
            assert_eq!(match_wake(neg, "атри").0, false, "false wake on {neg}");
        }
        for neg_q in ["пиши", "дыши", "гром", "короче"] {
            assert!(!is_quick_command(neg_q), "false quick command on {neg_q}");
        }
        assert!(is_quick_command("тише"));
        assert!(is_quick_command("громче"));
        assert!(is_stop_phrase("стоп"));
    }

    #[test]
    fn whisper_boost_raises_quiet_signal_without_clipping() {
        let mut quiet = Vec::new();
        for i in 0..512 {
            let s = ((i as f32 * 0.1).sin() * 200.0) as i16;
            quiet.extend_from_slice(&s.to_le_bytes());
        }
        let before = frame_rms(&quiet);
        let after = frame_rms(&boost_whisper_pcm(&quiet));
        assert!(after > before * 5.0);
        assert!(after < 20000.0);
    }
}
