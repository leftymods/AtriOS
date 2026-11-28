use atrios_ovos_plugin_sdk::{
    bus_channel, run_service_plugin, BusSender, OvosMessage, OvosPlugin, MSG_STOP, MSG_UTTERANCE,
};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use tracing::info;

struct IntentRouterPlugin {
    sender: BusSender,
    timer_cancel: Arc<AtomicBool>,
}

fn adjust_volume(delta: i32) {
    let sign = if delta >= 0 { format!("{}%+", delta) } else { format!("{}%-", -delta) };
    let _ = Command::new("/usr/bin/amixer")
        .args(["-q", "sset", "Master", &sign])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
}

fn control_ymusic(arg: &str) {
    let _ = Command::new("/usr/lib/atrios/ymusic")
        .arg(arg)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn();
}

fn local_time_ru() -> String {
    let out = Command::new("/bin/date")
        .arg("+%H:%M")
        .output()
        .ok()
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .unwrap_or_default();
    let parts: Vec<&str> = out.trim().split(':').collect();
    if parts.len() == 2 {
        let h: u32 = parts[0].parse().unwrap_or(0);
        let m: u32 = parts[1].parse().unwrap_or(0);
        format!("Сейчас {} часов {} минут", h, m)
    } else {
        let secs = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0);
        let h = (secs / 3600) % 24;
        let m = (secs / 60) % 60;
        format!("Сейчас {} часов {} минут", h, m)
    }
}

pub fn parse_timer_seconds(text: &str) -> Option<u64> {
    let words: Vec<&str> = text.split_whitespace().collect();
    if !words.iter().any(|w| w.starts_with("таймер")) {
        return None;
    }
    let mut num: Option<u64> = None;
    let mut mult: u64 = 60;
    for w in &words {
        if let Ok(n) = w.parse::<u64>() {
            num = Some(n);
        } else {
            match *w {
                "один" | "одну" | "одна" => num = Some(1),
                "два" | "две" => num = Some(2),
                "три" => num = Some(3),
                "четыре" => num = Some(4),
                "пять" => num = Some(5),
                "десять" => num = Some(10),
                "пятнадцать" => num = Some(15),
                "двадцать" => num = Some(20),
                "тридцать" => num = Some(30),
                _ => {}
            }
        }
        if w.starts_with("секунд") {
            mult = 1;
        } else if w.starts_with("минут") {
            mult = 60;
        } else if w.starts_with("час") {
            mult = 3600;
        }
    }
    num.filter(|&n| n > 0 && n <= 3600).map(|n| n * mult)
}

#[derive(Debug, PartialEq, Eq)]
pub enum RoutedIntent {
    Stop,
    VolumeDelta(i32),
    Music(&'static str),
    SpeakTime,
    SetTimer(u64),
    SpeakStatic(&'static str),
    Fallback,
}

pub fn route_russian_utterance(text: &str, quick_only: bool) -> RoutedIntent {
    let t = text.trim().to_lowercase().replace('ё', "е");
    if matches!(t.as_str(), "стоп" | "хватит" | "замолчи" | "отмена") {
        return RoutedIntent::Stop;
    }
    if t.contains("тише") || t.contains("потише") {
        return RoutedIntent::VolumeDelta(-10);
    }
    if t.contains("громче") || t.contains("погромче") {
        return RoutedIntent::VolumeDelta(10);
    }
    if t == "пауза" || t.contains("поставь на паузу") {
        return RoutedIntent::Music("pause");
    }
    if t == "продолжи" || t == "играй" || t.contains("включи музыку") {
        return RoutedIntent::Music("play");
    }
    if t == "дальше" || t == "следующий" || t.contains("следующий трек") {
        return RoutedIntent::Music("next");
    }
    if t == "назад" || t == "предыдущий" {
        return RoutedIntent::Music("prev");
    }
    if quick_only {
        return RoutedIntent::Fallback;
    }
    if t.contains("который час") || t.contains("сколько времени") || t == "время" {
        return RoutedIntent::SpeakTime;
    }
    if let Some(secs) = parse_timer_seconds(&t) {
        return RoutedIntent::SetTimer(secs);
    }
    if t.contains("привет") || t.contains("здравствуй") {
        return RoutedIntent::SpeakStatic("Привет! Я на связи.");
    }
    if t.contains("ты тут") || t.contains("проверка") || t.contains("как дела") {
        return RoutedIntent::SpeakStatic("Я тут, слушаю тебя.");
    }
    if t.contains("помощь") || t.contains("что ты умеешь") {
        return RoutedIntent::SpeakStatic("Я могу сказать время, поставить таймер и управлять громкостью или музыкой.");
    }
    RoutedIntent::Fallback
}

impl OvosPlugin for IntentRouterPlugin {
    fn id(&self) -> &'static str {
        "ovos-intent-router"
    }

    fn handle(&mut self, message: &OvosMessage) -> Vec<OvosMessage> {
        if message.message_type == MSG_STOP {
            self.timer_cancel.store(true, Ordering::SeqCst);
            return Vec::new();
        }
        if message.message_type != MSG_UTTERANCE {
            return Vec::new();
        }
        let utterance = message
            .data
            .get("utterances")
            .and_then(|u| u.as_array())
            .and_then(|a| a.first())
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .trim();
        if utterance.is_empty() {
            return Vec::new();
        }
        let whisper = message
            .data
            .get("whisper")
            .and_then(|v| v.as_bool())
            .unwrap_or(false);
        let quick = message
            .data
            .get("quick_command")
            .and_then(|v| v.as_bool())
            .unwrap_or(false);

        let intent = route_russian_utterance(utterance, quick);
        info!(%utterance, ?intent, whisper, "ovos-intent-router: matched intent");

        match intent {
            RoutedIntent::Stop => vec![OvosMessage::stop()],
            RoutedIntent::VolumeDelta(d) => {
                adjust_volume(d);
                Vec::new()
            }
            RoutedIntent::Music(cmd) => {
                control_ymusic(cmd);
                Vec::new()
            }
            RoutedIntent::SpeakTime => vec![OvosMessage::speak(local_time_ru(), "ru-ru", whisper)],
            RoutedIntent::SetTimer(secs) => {
                let cancel = Arc::new(AtomicBool::new(false));
                self.timer_cancel = Arc::clone(&cancel);
                let tx = self.sender.clone();
                std::thread::spawn(move || {
                    for _ in 0..secs {
                        if cancel.load(Ordering::Relaxed) {
                            return;
                        }
                        std::thread::sleep(Duration::from_secs(1));
                    }
                    if !cancel.load(Ordering::Relaxed) {
                        let _ = tx.emit(OvosMessage::speak("Таймер завершён!", "ru-ru", false));
                    }
                });
                let reply = if secs < 60 {
                    format!("Ставлю таймер на {} секунд", secs)
                } else {
                    format!("Ставлю таймер на {} минут", secs / 60)
                };
                vec![OvosMessage::speak(reply, "ru-ru", whisper)]
            }
            RoutedIntent::SpeakStatic(s) => vec![OvosMessage::speak(s, "ru-ru", whisper)],
            RoutedIntent::Fallback => {
                if quick {
                    Vec::new()
                } else {
                    vec![OvosMessage::speak("Не поняла команду", "ru-ru", whisper)]
                }
            }
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
    let plugin = IntentRouterPlugin {
        sender,
        timer_cancel: Arc::new(AtomicBool::new(false)),
    };
    run_service_plugin(plugin, rx).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn routes_core_russian_intents() {
        assert_eq!(route_russian_utterance("стоп", false), RoutedIntent::Stop);
        assert_eq!(route_russian_utterance("тише", true), RoutedIntent::VolumeDelta(-10));
        assert_eq!(route_russian_utterance("громче", true), RoutedIntent::VolumeDelta(10));
        assert_eq!(route_russian_utterance("дальше", true), RoutedIntent::Music("next"));
        assert_eq!(route_russian_utterance("который час", false), RoutedIntent::SpeakTime);
        assert_eq!(route_russian_utterance("поставь таймер на 5 минут", false), RoutedIntent::SetTimer(300));
        assert_eq!(route_russian_utterance("поставь таймер на 10 секунд", false), RoutedIntent::SetTimer(10));
        assert_eq!(route_russian_utterance("привет", false), RoutedIntent::SpeakStatic("Привет! Я на связи."));
    }
}
