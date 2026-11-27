use std::fs;
use std::path::Path;
use std::sync::Mutex;

use crate::https::request_json;
use crate::json::{obj, Json};
use crate::sys::{atomic_write_bytes, now_unix_i64};

const CONFIG: &str = "/var/lib/atrios/weather.json";
const FIELDS: [&str; 8] = ["wind", "humidity", "pressure", "uv", "water", "sun", "moon", "feels_like"];
static SAVE_LOCK: Mutex<()> = Mutex::new(());

fn read_config() -> Json {
    fs::read_to_string(CONFIG).ok()
        .and_then(|data| Json::parse(&data).ok())
        .unwrap_or(Json::Null)
}

pub fn settings() -> Json {
    let config = read_config();
    obj(vec![
        ("city", Json::Str(config.get_str("city").unwrap_or("").into())),
        ("options", options(config.get("options"))),
    ])
}

fn options(values: Option<&Json>) -> Json {
    obj(FIELDS.iter().map(|&key| {
        let checked = values.and_then(|v| v.get(key)) == Some(&Json::Bool(true));
        (key, Json::Bool(checked))
    }).collect())
}

fn load_json(url: &str, cancel: Option<&dyn Fn() -> bool>) -> Result<Json, String> {
    request_json(url, None, &[], None, 5, 96 * 1024, cancel)
}

fn coordinates(location: &Json) -> Option<(f64, f64)> {
    let lat = location.get("latitude")?.as_f64()?;
    let lon = location.get("longitude")?.as_f64()?;
    ((-90.0..=90.0).contains(&lat) && (-180.0..=180.0).contains(&lon)).then_some((lat, lon))
}

pub fn save(city: &str, extras: &Json) -> Result<(), String> {
    let city = city.trim();
    if city.chars().count() > 80 || city.chars().any(char::is_control) {
        return Err("Введите название города до 80 символов".into());
    }
    let Some(fields) = extras.as_obj().filter(|fields| {
        fields.iter().all(|(k, v)| FIELDS.contains(&k.as_str()) && matches!(v, Json::Bool(_)))
    }) else {
        return Err("Неверные параметры погоды".into());
    };
    let _guard = SAVE_LOCK.lock().map_err(|_| "Настройки погоды заняты")?;
    let current = read_config();
    let mut location = Vec::new();
    if !city.is_empty() {
        let (name, lat, lon) = if let Some((lat, lon)) = coordinates(&current).filter(|_| current.get_str("city") == Some(city)) {
            (city.to_string(), lat, lon)
        } else {
            let query = crate::ymusic::url_encode(city);
            let url = format!("https://geocoding-api.open-meteo.com/v1/search?name={query}&count=1&language=ru");
            let matches = load_json(&url, None).map_err(|_| "Сервис поиска города недоступен")?;
            let found = matches.get("results").and_then(Json::as_arr)
                .and_then(|results| results.first()).ok_or("Город не найден")?;
            let (lat, lon) = coordinates(found).ok_or("Сервис не вернул координаты города")?;
            let name = found.get_str("name").unwrap_or(city);
            let label = found.get_str("country").filter(|country| !country.is_empty())
                .map(|country| format!("{name}, {country}")).unwrap_or_else(|| name.to_string());
            (label, lat, lon)
        };
        location.extend([("city", Json::Str(name)), ("latitude", Json::Num(lat)), ("longitude", Json::Num(lon))]);
    }
    let mut values = options(current.get("options"));
    if let Json::Obj(ref mut choices) = values {
        choices.extend(fields.clone());
    }
    location.push(("options", values));
    atomic_write_bytes(Path::new(CONFIG), obj(location).to_string_compact().as_bytes(), 0o600, true)
        .map_err(|_| "Не удалось сохранить настройки погоды".into())
}

pub fn is_query(phrase: &str) -> bool {
    matches!(phrase, "погода" | "погода сегодня" | "какая погода" | "какая сейчас погода"
        | "какая погода сегодня" | "расскажи погоду" | "скажи погоду" | "прогноз погоды")
}

pub fn forecast(cancel: Option<&dyn Fn() -> bool>) -> Result<String, String> {
    let config = read_config();
    let (lat, lon) = coordinates(&config).ok_or("Выберите город в настройках погоды.")?;
    let city = config.get_str("city").unwrap_or("");
    let extras = config.get("options");
    let checked = |key| extras.and_then(|v| v.get(key)) == Some(&Json::Bool(true));
    let mut current = vec!["temperature_2m", "weather_code"];
    if checked("feels_like") { current.push("apparent_temperature"); }
    if checked("humidity") { current.push("relative_humidity_2m"); }
    if checked("pressure") { current.push("surface_pressure"); }
    if checked("wind") { current.extend(["wind_speed_10m", "wind_direction_10m"]); }
    let mut daily = Vec::new();
    if checked("uv") { daily.push("uv_index_max"); }
    if checked("sun") { daily.extend(["sunrise", "sunset"]); }
    let mut url = format!(
        "https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}&wind_speed_unit=ms&timezone=auto&forecast_days=1&current={}",
        current.join(",")
    );
    if !daily.is_empty() {
        url.push_str("&daily=");
        url.push_str(&daily.join(","));
    }
    let report = load_json(&url, cancel)?;
    let mut forecast = format_report(city, &report)?;
    if checked("moon") {
        let days_since_new_moon = (now_unix_i64() as f64 - 947_182_440.0) / 86400.0;
        let fraction = (days_since_new_moon / 29.530588).rem_euclid(1.0);
        let phase = ["новолуние", "растущий серп", "первая четверть", "растущая Луна",
            "полнолуние", "убывающая Луна", "последняя четверть", "убывающий серп"];
        forecast.push_str(&format!(" Фаза Луны приблизительно: {}.", phase[((fraction * 8.0).round() as usize) % 8]));
    }
    if checked("water") && !cancel.is_some_and(|cancelled| cancelled()) {
        let url = format!("https://marine-api.open-meteo.com/v1/marine?latitude={lat}&longitude={lon}&current=sea_surface_temperature&cell_selection=nearest");
        if let Ok(data) = load_json(&url, cancel) {
            if let Some((report_lat, report_lon)) = coordinates(&data) {
                let delta_lon = ((report_lon - lon + 180.0).rem_euclid(360.0) - 180.0) * lat.to_radians().cos();
                let delta_lat = report_lat - lat;
                let distance_km = (delta_lat * delta_lat + delta_lon * delta_lon).sqrt() * 111.0;
                if distance_km <= 30.0 {
                    if let Some(value) = data.get("current").and_then(|c| c.get("sea_surface_temperature")).and_then(Json::as_f64) {
                        forecast.push_str(&format!(" Температура моря {value:.0} градусов."));
                    }
                }
            }
        }
    }
    Ok(forecast)
}

fn format_report(city: &str, report: &Json) -> Result<String, String> {
    let current = report.get("current").ok_or("Нет данных о текущей погоде.")?;
    let current_value = |key| current.get(key).and_then(Json::as_f64);
    let temperature = current_value("temperature_2m").ok_or("Нет данных о температуре.")?;
    let condition = match current.get("weather_code").and_then(Json::as_i64) {
        Some(0) => "Ясно.",
        Some(1..=3) => "Облачно.",
        Some(45 | 48) => "Туман.",
        Some(51..=67 | 80..=82) => "Дождь.",
        Some(71..=77 | 85..=86) => "Снег.",
        Some(95..=99) => "Гроза.",
        _ => "",
    };
    let mut text = format!("{city}. Сейчас {temperature:.0} градусов.");
    if !condition.is_empty() { text.push_str(&format!(" {condition}")); }
    if let Some(value) = current_value("apparent_temperature") { text.push_str(&format!(" Ощущается как {value:.0}.")); }
    if let Some(value) = current_value("relative_humidity_2m") { text.push_str(&format!(" Влажность {value:.0} процентов.")); }
    if let Some(value) = current_value("surface_pressure") { text.push_str(&format!(" Давление {:.0} миллиметров ртутного столба.", value * 0.750062)); }
    if let Some(speed) = current_value("wind_speed_10m") {
        let directions = ["северный", "северо-восточный", "восточный", "юго-восточный", "южный", "юго-западный", "западный", "северо-западный"];
        let direction = current_value("wind_direction_10m").map(|v| directions[((v / 45.0).round() as usize) % 8]).unwrap_or("");
        text.push_str(&format!(" Ветер {direction} {speed:.0} метров в секунду."));
    }
    let daily = report.get("daily");
    let first = |key| daily?.get(key)?.as_arr()?.first();
    if let Some(uv) = first("uv_index_max").and_then(Json::as_f64) { text.push_str(&format!(" Максимальный УФ-индекс {uv:.0}.")); }
    for (key, label) in [("sunrise", "Восход"), ("sunset", "Закат")] {
        if let Some(time) = first(key).and_then(Json::as_str).and_then(|v| v.split_once('T')).map(|(_, time)| time) {
            text.push_str(&format!(" {label} в {time}."));
        }
    }
    Ok(text)
}

#[cfg(test)]
#[test]
fn formats_only_available_weather_metrics() {
    let response = Json::parse(r#"{"current":{"temperature_2m":-3.5,"weather_code":71,"relative_humidity_2m":80},"daily":{"sunrise":["2026-11-27T08:00"],"uv_index_max":[null]}}"#).unwrap();
    let report = format_report("Москва", &response).unwrap();
    assert!(report.contains("Снег. Влажность 80"));
    assert!(report.contains("Восход в 08:00"));
    assert!(!report.contains("УФ"));
    assert!(is_query("какая погода сегодня"));
    assert!(!is_query("какая погода была вчера"));
}
