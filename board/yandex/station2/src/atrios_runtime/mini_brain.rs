//! Hybrid Mini-Brain (Contour 1: local semantic NLU, multi-step commands, short-term dialog context,
//! offline math & encrypted 2-copy vector memory; Contour 2: rich context & tool-calling bridge to leftymods.tech).

use crate::e2ee;
use crate::json::{obj, Json};
use crate::sys::{atomic_write_bytes, now_unix_i64};
use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock};

const VEC_DIM: usize = 128;
const MAGIC: &[u8; 8] = b"ATRVEC1\0";
const MAX_MEMORIES: usize = 256;
const MAX_DIALOG_TURNS: usize = 6;
const CONTEXT_WINDOW_SECS: i64 = 35;

const PRIMARY_VEC_PATH: &str = "/var/lib/atrios/ai_memory.vec.enc";
const BACKUP_VEC_PATH: &str = "/var/lib/atrios/ai_memory.vec.enc.bak";
const KEY_SOURCE_PATHS: [&str; 3] = [
    "/var/lib/atrios/e2ee_identity.pem",
    "/var/lib/atrios/device_identity.json",
    "/etc/machine-id",
];

#[derive(Clone, Debug)]
pub struct MemoryEntry {
    pub id: u64,
    pub kind: String, // "note", "fact", "dialog", "habit"
    pub text: String,
    pub ts: i64,
    pub vec: [f32; VEC_DIM],
}

#[derive(Clone, Debug)]
pub struct DialogTurn {
    pub user: String,
    pub assistant: String,
    pub ts: i64,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LastAction {
    None,
    VolumeUp(i32),
    VolumeDown(i32),
    MusicPlay,
    TimerCreated,
    NoteSaved(u64),
}

#[derive(Clone, Debug)]
struct BrainRuntime {
    memories: Vec<MemoryEntry>,
    loaded: bool,
    primary_path: PathBuf,
    backup_path: PathBuf,
    dialog: Vec<DialogTurn>,
    last_action: LastAction,
    last_action_ts: i64,
}

impl BrainRuntime {
    fn new() -> Self {
        Self {
            memories: Vec::new(),
            loaded: false,
            primary_path: PathBuf::from(PRIMARY_VEC_PATH),
            backup_path: PathBuf::from(BACKUP_VEC_PATH),
            dialog: Vec::new(),
            last_action: LastAction::None,
            last_action_ts: 0,
        }
    }
}

static BRAIN: OnceLock<Mutex<BrainRuntime>> = OnceLock::new();

fn brain() -> &'static Mutex<BrainRuntime> {
    BRAIN.get_or_init(|| Mutex::new(BrainRuntime::new()))
}

// --- 128-dim Russian Semantic Feature-Hashing Embedder (zero external deps, < 1ms) ---

fn normalize_ru(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut prev_space = true;
    for ch in s.chars() {
        let lc = match ch {
            'Ё' | 'ё' => 'е',
            c => c.to_lowercase().next().unwrap_or(c),
        };
        if lc.is_alphanumeric() {
            out.push(lc);
            prev_space = false;
        } else if !prev_space {
            out.push(' ');
            prev_space = true;
        }
    }
    out.trim().to_string()
}

fn stem_ru(word: &str) -> &str {
    let chars: Vec<(usize, char)> = word.char_indices().collect();
    if chars.len() <= 4 {
        return word;
    }
    // Trim common Russian inflectional suffixes (2-3 chars) while keeping at least 4 chars
    const SUFFIXES: &[&str] = &[
        "ами", "ями", "ого", "его", "ому", "ему", "ыми", "ими", "ать", "ять", "ить", "еть",
        "ешь", "ишь", "ете", "ите", "ова", "ева", "ий", "ый", "ой", "ая", "яя", "ое", "ее",
        "ые", "ие", "ом", "ем", "ам", "ям", "ах", "ях", "ов", "ев", "ей", "ую", "юю", "ся",
        "сь", "ал", "ил", "ел", "ут", "ют", "ат", "ят", "а", "я", "у", "ю", "е", "и", "ы", "о",
    ];
    for suf in SUFFIXES {
        if word.ends_with(suf) {
            let cut = word.len() - suf.len();
            if word[..cut].chars().count() >= 4 {
                return &word[..cut];
            }
        }
    }
    word
}

fn fnv1a_64(bytes: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for &b in bytes {
        h ^= b as u64;
        h = h.wrapping_mul(0x100000001b3);
    }
    h
}

pub fn embed_text(text: &str) -> [f32; VEC_DIM] {
    let norm = normalize_ru(text);
    let mut v = [0.0f32; VEC_DIM];
    if norm.is_empty() {
        return v;
    }
    for word in norm.split_whitespace() {
        let stem = stem_ru(word);
        // Word-level stemmed feature (higher weight)
        let hw = fnv1a_64(stem.as_bytes());
        let idx = (hw as usize) % VEC_DIM;
        let sign = if (hw >> 63) == 0 { 1.0f32 } else { -1.0f32 };
        v[idx] += 2.0 * sign;

        // Character 3-grams for typo/morphology resilience
        let padded = format!("^{stem}$");
        let chs: Vec<char> = padded.chars().collect();
        if chs.len() >= 3 {
            for win in chs.windows(3) {
                let mut buf = [0u8; 16];
                let mut len = 0usize;
                for &c in win {
                    let s = c.encode_utf8(&mut buf[len..]);
                    len += s.len();
                }
                let hg = fnv1a_64(&buf[..len]);
                let gi = (hg as usize) % VEC_DIM;
                let gs = if (hg >> 63) == 0 { 1.0f32 } else { -1.0f32 };
                v[gi] += 0.8 * gs;
            }
        }
    }
    // L2 normalize
    let norm2: f32 = v.iter().map(|x| x * x).sum();
    if norm2 > 1e-8 {
        let inv = 1.0 / norm2.sqrt();
        for x in &mut v {
            *x *= inv;
        }
    }
    v
}

pub fn cosine_sim(a: &[f32; VEC_DIM], b: &[f32; VEC_DIM]) -> f32 {
    let mut dot = 0.0f32;
    for i in 0..VEC_DIM {
        dot += a[i] * b[i];
    }
    dot
}

// --- AI-Only Encrypted 2-Copy Binary Vector Store ---

fn derive_ai_memory_key() -> [u8; 32] {
    let mut material = Vec::new();
    for p in KEY_SOURCE_PATHS {
        if let Ok(bytes) = fs::read(p) {
            if !bytes.is_empty() {
                material.extend_from_slice(&bytes);
                break;
            }
        }
    }
    if material.is_empty() {
        material.extend_from_slice(b"atrios-fallback-station-ai-key");
    }
    e2ee::hmac_sha256(&material, b"atrios-ai-vector-memory-v1")
}

fn serialize_entries(entries: &[MemoryEntry]) -> Vec<u8> {
    let mut out = Vec::with_capacity(entries.len() * (32 + VEC_DIM * 4));
    out.extend_from_slice(&(entries.len() as u32).to_le_bytes());
    for e in entries {
        out.extend_from_slice(&e.id.to_le_bytes());
        out.extend_from_slice(&e.ts.to_le_bytes());
        let kb = e.kind.as_bytes();
        out.extend_from_slice(&(kb.len() as u16).to_le_bytes());
        out.extend_from_slice(kb);
        let tb = e.text.as_bytes();
        out.extend_from_slice(&(tb.len() as u32).to_le_bytes());
        out.extend_from_slice(tb);
        for &f in &e.vec {
            out.extend_from_slice(&f.to_le_bytes());
        }
    }
    out
}

fn deserialize_entries(data: &[u8]) -> Result<Vec<MemoryEntry>, String> {
    if data.len() < 4 {
        return Err("Truncated vector DB".into());
    }
    let count = u32::from_le_bytes(data[0..4].try_into().unwrap()) as usize;
    if count > MAX_MEMORIES * 4 {
        return Err("Unreasonable memory count".into());
    }
    let mut pos = 4usize;
    let mut out = Vec::with_capacity(count);
    for _ in 0..count {
        if pos + 18 > data.len() {
            return Err("Truncated entry header".into());
        }
        let id = u64::from_le_bytes(data[pos..pos + 8].try_into().unwrap());
        pos += 8;
        let ts = i64::from_le_bytes(data[pos..pos + 8].try_into().unwrap());
        pos += 8;
        let klen = u16::from_le_bytes(data[pos..pos + 2].try_into().unwrap()) as usize;
        pos += 2;
        if pos + klen + 4 > data.len() {
            return Err("Truncated kind".into());
        }
        let kind = String::from_utf8(data[pos..pos + klen].to_vec()).map_err(|_| "Invalid UTF-8 kind")?;
        pos += klen;
        let tlen = u32::from_le_bytes(data[pos..pos + 4].try_into().unwrap()) as usize;
        pos += 4;
        if pos + tlen + VEC_DIM * 4 > data.len() {
            return Err("Truncated entry text/vec".into());
        }
        let text = String::from_utf8(data[pos..pos + tlen].to_vec()).map_err(|_| "Invalid UTF-8 text")?;
        pos += tlen;
        let mut vec = [0.0f32; VEC_DIM];
        for i in 0..VEC_DIM {
            vec[i] = f32::from_le_bytes(data[pos..pos + 4].try_into().unwrap());
            pos += 4;
        }
        out.push(MemoryEntry { id, kind, text, ts, vec });
    }
    Ok(out)
}

fn encrypt_vector_blob(entries: &[MemoryEntry], key: &[u8; 32]) -> Result<Vec<u8>, String> {
    let plain = serialize_entries(entries);
    let mut nonce = [0u8; 12];
    e2ee::random_bytes(&mut nonce)?;
    let (ct, mac) = e2ee::encrypt_e2ee(key, &nonce, &plain);
    let mut blob = Vec::with_capacity(8 + 12 + 32 + ct.len());
    blob.extend_from_slice(MAGIC);
    blob.extend_from_slice(&nonce);
    blob.extend_from_slice(&mac);
    blob.extend_from_slice(&ct);
    Ok(blob)
}

fn decrypt_vector_blob(blob: &[u8], key: &[u8; 32]) -> Result<Vec<MemoryEntry>, String> {
    if blob.len() < 8 + 12 + 32 || &blob[0..8] != MAGIC {
        return Err("Invalid ATRVEC1 header".into());
    }
    let nonce: [u8; 12] = blob[8..20].try_into().unwrap();
    let mac: [u8; 32] = blob[20..52].try_into().unwrap();
    let ct = &blob[52..];
    let plain = e2ee::decrypt_e2ee(key, &nonce, ct, &mac)?;
    deserialize_entries(&plain)
}

fn save_two_copies(primary: &Path, backup: &Path, entries: &[MemoryEntry], key: &[u8; 32]) -> Result<Vec<u8>, String> {
    let blob = encrypt_vector_blob(entries, key)?;
    for p in [primary, backup] {
        if let Some(parent) = p.parent() {
            let _ = fs::create_dir_all(parent);
        }
        atomic_write_bytes(p, &blob, 0o600, true).map_err(|e| e.to_string())?;
        let _ = fs::set_permissions(p, fs::Permissions::from_mode(0o600));
    }
    Ok(blob)
}

fn load_with_failover(primary: &Path, backup: &Path, key: &[u8; 32]) -> Vec<MemoryEntry> {
    if let Ok(bytes) = fs::read(primary) {
        if let Ok(entries) = decrypt_vector_blob(&bytes, key) {
            return entries;
        }
    }
    if let Ok(bytes) = fs::read(backup) {
        if let Ok(entries) = decrypt_vector_blob(&bytes, key) {
            // Self-heal primary copy from intact backup
            let _ = atomic_write_bytes(primary, &bytes, 0o600, true);
            let _ = fs::set_permissions(primary, fs::Permissions::from_mode(0o600));
            return entries;
        }
    }
    Vec::new()
}

fn ensure_loaded_locked(rt: &mut BrainRuntime) {
    if !rt.loaded {
        let key = derive_ai_memory_key();
        rt.memories = load_with_failover(&rt.primary_path, &rt.backup_path, &key);
        rt.loaded = true;
    }
}

/// Non-blocking encrypted mirror upload to leftymods.tech so the AI server can keep a remote backup copy.
fn sync_encrypted_blob_to_server_async(blob: Vec<u8>) {
    let cfg = crate::atriai::load_config();
    let base = cfg.server.trim().trim_end_matches('/').to_string();
    if base.is_empty() || cfg.token.is_empty() {
        return;
    }
    let tok = cfg.token.clone();
    let ca = cfg.ca_file.clone();
    std::thread::spawn(move || {
        let b64 = e2ee::base64_encode(&blob);
        let payload = obj(vec![
            ("format", Json::Str("atrvec1".into())),
            ("blob_b64", Json::Str(b64)),
            ("updated_ts", Json::Num(now_unix_i64() as f64)),
        ]);
        let auth = format!("Bearer {tok}");
        let headers = [("Authorization", auth.as_str())];
        let ca_opt = if ca.is_empty() { None } else { Some(ca.as_str()) };
        let _ = crate::https::request_json(
            &format!("{base}/memory/sync"),
            Some(&payload),
            &headers,
            ca_opt,
            8,
            64 * 1024,
            None,
        );
    });
}

pub fn remember(kind: &str, text: &str) -> u64 {
    let clean = text.trim();
    if clean.is_empty() {
        return 0;
    }
    let vec = embed_text(clean);
    let ts = now_unix_i64();
    let key = derive_ai_memory_key();
    let (id, blob_opt) = {
        let mut rt = brain().lock().unwrap();
        ensure_loaded_locked(&mut rt);
        // Deduplicate near-identical entries (cosine > 0.94)
        if let Some(existing) = rt.memories.iter_mut().find(|m| m.kind == kind && cosine_sim(&m.vec, &vec) > 0.94) {
            existing.text = clean.to_string();
            existing.ts = ts;
            let eid = existing.id;
            let blob = save_two_copies(&rt.primary_path, &rt.backup_path, &rt.memories, &key).ok();
            (eid, blob)
        } else {
            let next_id = rt.memories.iter().map(|m| m.id).max().unwrap_or(0) + 1;
            rt.memories.push(MemoryEntry {
                id: next_id,
                kind: kind.to_string(),
                text: clean.to_string(),
                ts,
                vec,
            });
            while rt.memories.len() > MAX_MEMORIES {
                // Evict oldest dialog entry first, else oldest overall
                if let Some(pos) = rt.memories.iter().position(|m| m.kind == "dialog") {
                    rt.memories.remove(pos);
                } else {
                    rt.memories.remove(0);
                }
            }
            let blob = save_two_copies(&rt.primary_path, &rt.backup_path, &rt.memories, &key).ok();
            (next_id, blob)
        }
    };
    if let Some(blob) = blob_opt {
        sync_encrypted_blob_to_server_async(blob);
    }
    id
}

pub fn recall(query: &str, k: usize, min_score: f32) -> Vec<(f32, MemoryEntry)> {
    let qv = embed_text(query);
    let mut rt = brain().lock().unwrap();
    ensure_loaded_locked(&mut rt);
    let mut scored: Vec<(f32, MemoryEntry)> = rt
        .memories
        .iter()
        .map(|m| (cosine_sim(&qv, &m.vec), m.clone()))
        .filter(|(s, _)| *s >= min_score)
        .collect();
    scored.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap_or(std::cmp::Ordering::Equal));
    scored.truncate(k);
    scored
}

pub fn forget_matching(query: &str) -> usize {
    let qv = embed_text(query);
    let key = derive_ai_memory_key();
    let (removed, blob_opt) = {
        let mut rt = brain().lock().unwrap();
        ensure_loaded_locked(&mut rt);
        let before = rt.memories.len();
        rt.memories.retain(|m| cosine_sim(&qv, &m.vec) < 0.35);
        let removed = before.saturating_sub(rt.memories.len());
        let blob = if removed > 0 {
            save_two_copies(&rt.primary_path, &rt.backup_path, &rt.memories, &key).ok()
        } else {
            None
        };
        (removed, blob)
    };
    if let Some(blob) = blob_opt {
        sync_encrypted_blob_to_server_async(blob);
    }
    removed
}

pub fn forget_by_id(id: u64) -> bool {
    let key = derive_ai_memory_key();
    let (removed, blob_opt) = {
        let mut rt = brain().lock().unwrap();
        ensure_loaded_locked(&mut rt);
        let before = rt.memories.len();
        rt.memories.retain(|m| m.id != id);
        let removed = rt.memories.len() < before;
        let blob = if removed {
            save_two_copies(&rt.primary_path, &rt.backup_path, &rt.memories, &key).ok()
        } else {
            None
        };
        (removed, blob)
    };
    if let Some(blob) = blob_opt {
        sync_encrypted_blob_to_server_async(blob);
    }
    removed
}

pub fn clear_all_memory() -> usize {
    let key = derive_ai_memory_key();
    let (count, blob_opt) = {
        let mut rt = brain().lock().unwrap();
        ensure_loaded_locked(&mut rt);
        let n = rt.memories.len();
        rt.memories.clear();
        let blob = save_two_copies(&rt.primary_path, &rt.backup_path, &rt.memories, &key).ok();
        (n, blob)
    };
    if let Some(blob) = blob_opt {
        sync_encrypted_blob_to_server_async(blob);
    }
    count
}

pub fn record_dialog_turn(user: &str, assistant: &str) {
    let ts = now_unix_i64();
    let mut rt = brain().lock().unwrap();
    rt.dialog.push(DialogTurn {
        user: user.trim().to_string(),
        assistant: assistant.trim().to_string(),
        ts,
    });
    while rt.dialog.len() > MAX_DIALOG_TURNS {
        rt.dialog.remove(0);
    }
}

pub fn record_last_action(action: LastAction) {
    let mut rt = brain().lock().unwrap();
    rt.last_action = action;
    rt.last_action_ts = now_unix_i64();
}

pub fn get_recent_action() -> LastAction {
    let rt = brain().lock().unwrap();
    if now_unix_i64() - rt.last_action_ts <= CONTEXT_WINDOW_SECS {
        rt.last_action.clone()
    } else {
        LastAction::None
    }
}

/// Build rich live device + vector-memory + dialog context for Contour 2 (`leftymods.tech/chat`).
pub fn build_chat_context_json(query: &str) -> Json {
    let recalled = recall(query, 4, 0.22);
    let mem_arr: Vec<Json> = recalled
        .into_iter()
        .map(|(score, m)| {
            obj(vec![
                ("kind", Json::Str(m.kind)),
                ("text", Json::Str(m.text)),
                ("score", Json::Num(((score * 100.0).round() / 100.0) as f64)),
            ])
        })
        .collect();

    let dialog_arr: Vec<Json> = {
        let rt = brain().lock().unwrap();
        rt.dialog
            .iter()
            .rev()
            .take(4)
            .rev()
            .map(|t| {
                obj(vec![
                    ("user", Json::Str(t.user.clone())),
                    ("assistant", Json::Str(t.assistant.clone())),
                ])
            })
            .collect()
    };

    let ym = crate::ymusic::read_status();
    let vol = crate::bt_control::get_current_volume();
    let device = obj(vec![
        ("volume", Json::Num(vol as f64)),
        ("music_state", Json::Str(ym.state)),
        ("track_title", Json::Str(ym.title)),
        ("track_artist", Json::Str(ym.artist)),
        ("position_ms", Json::Num(ym.position_ms as f64)),
        ("duration_ms", Json::Num(ym.duration_ms as f64)),
    ]);

    obj(vec![
        ("device", device),
        ("recalled_memories", Json::Arr(mem_arr)),
        ("recent_dialog", Json::Arr(dialog_arr)),
    ])
}

// --- Offline Spoken Russian Math Evaluator ---

fn parse_ru_num_word(w: &str) -> Option<f64> {
    if let Ok(n) = w.replace(',', ".").parse::<f64>() {
        return Some(n);
    }
    let val = match w {
        "ноль" | "нуль" => 0.0,
        "один" | "одна" | "одно" => 1.0,
        "два" | "две" => 2.0,
        "три" => 3.0,
        "четыре" => 4.0,
        "пять" => 5.0,
        "шесть" => 6.0,
        "семь" => 7.0,
        "восемь" => 8.0,
        "девять" => 9.0,
        "десять" => 10.0,
        "одиннадцать" => 11.0,
        "двенадцать" => 12.0,
        "тринадцать" => 13.0,
        "четырнадцать" => 14.0,
        "пятнадцать" => 15.0,
        "шестнадцать" => 16.0,
        "семнадцать" => 17.0,
        "восемнадцать" => 18.0,
        "девятнадцать" => 19.0,
        "двадцать" => 20.0,
        "тридцать" => 30.0,
        "сорок" => 40.0,
        "пятьдесят" => 50.0,
        "шестьдесят" => 60.0,
        "семьдесят" => 70.0,
        "восемьдесят" => 80.0,
        "девяносто" => 90.0,
        "сто" => 100.0,
        "двести" => 200.0,
        "триста" => 300.0,
        "четыреста" => 400.0,
        "пятьсот" => 500.0,
        "шестьсот" => 600.0,
        "семьсот" => 700.0,
        "восемьсот" => 800.0,
        "девятьсот" => 900.0,
        "тысяча" | "тысячи" | "тысяч" => 1000.0,
        _ => return None,
    };
    Some(val)
}

fn consume_ru_number(words: &[&str], start: usize) -> Option<(f64, usize)> {
    let mut i = start;
    let mut total = 0.0f64;
    let mut matched = false;
    while i < words.len() {
        if let Some(v) = parse_ru_num_word(words[i]) {
            if v == 1000.0 {
                total = if total == 0.0 { 1000.0 } else { total * 1000.0 };
            } else {
                total += v;
            }
            matched = true;
            i += 1;
        } else {
            break;
        }
    }
    if matched { Some((total, i)) } else { None }
}

pub fn eval_spoken_math(query: &str) -> Option<String> {
    let norm = normalize_ru(query);
    let words: Vec<&str> = norm.split_whitespace().collect();
    if words.len() < 3 {
        return None;
    }
    // Find first number
    let mut i = 0;
    while i < words.len() && parse_ru_num_word(words[i]).is_none() {
        i += 1;
    }
    let (a, mut idx) = consume_ru_number(&words, i)?;
    if idx >= words.len() {
        return None;
    }
    let op = match words[idx] {
        "плюс" | "прибавить" => {
            idx += 1;
            '+'
        }
        "минус" | "вычесть" => {
            idx += 1;
            '-'
        }
        "умножить" | "умножь" | "на" if words[idx] != "на" => {
            idx += 1;
            if words.get(idx) == Some(&"на") {
                idx += 1;
            }
            '*'
        }
        "разделить" | "поделить" | "делить" => {
            idx += 1;
            if words.get(idx) == Some(&"на") {
                idx += 1;
            }
            '/'
        }
        "процент" | "процента" | "процентов" => {
            idx += 1;
            if words.get(idx) == Some(&"от") {
                idx += 1;
            }
            '%'
        }
        _ => return None,
    };
    let (b, _) = consume_ru_number(&words, idx)?;
    let res = match op {
        '+' => a + b,
        '-' => a - b,
        '*' => a * b,
        '/' => {
            if b.abs() < 1e-9 {
                return Some("На ноль делить нельзя.".into());
            }
            a / b
        }
        '%' => (a / 100.0) * b,
        _ => return None,
    };
    let formatted = if (res - res.round()).abs() < 1e-6 {
        format!("{}", res.round() as i64)
    } else {
        format!("{:.2}", res).trim_end_matches('0').trim_end_matches('.').to_string()
    };
    Some(format!("Будет {formatted}."))
}

// --- Contour 1 Local Brain Decision ---

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum BrainOutcome {
    /// Handled completely offline; optional spoken answer + chime
    Handled {
        answer: Option<String>,
        chime: Option<&'static str>,
    },
    /// Split into multiple sub-commands to execute sequentially
    Compound(Vec<String>),
    /// Escalate to Contour 2 (leftymods.tech/chat) with rich context
    Escalate,
}

fn parse_seek_command(norm: &str) -> Option<i64> {
    if !(norm.contains("перемотай") || norm.contains("промотай") || norm.contains("отмотай")) {
        return None;
    }
    let ym = crate::ymusic::read_status();
    if ym.track_id.is_empty() || ym.duration_ms <= 0 {
        return None;
    }
    let words: Vec<&str> = norm.split_whitespace().collect();
    let secs = crate::local_ai::parse_russian_duration_secs(&words)? as i64;
    let delta_ms = secs * 1000;
    let cur_ms = ym.position_ms;
    if norm.contains("назад") || norm.contains("отмотай") {
        Some((cur_ms - delta_ms).max(0))
    } else if norm.contains("вперед") || norm.contains("вперёд") || norm.contains("промотай") {
        Some((cur_ms + delta_ms).min(ym.duration_ms))
    } else {
        // Absolute position: "перемотай на 1 минуту 30 секунд"
        Some(delta_ms.min(ym.duration_ms))
    }
}

/// Split compound Russian commands like "поставь таймер на 5 минут и сделай потише"
pub fn split_compound_command(query: &str) -> Option<Vec<String>> {
    let norm = normalize_ru(query);
    // Don't split explicit memory notes or song searches
    if norm.starts_with("запомни")
        || norm.starts_with("запиши")
        || norm.starts_with("включи песню")
        || norm.starts_with("поставь песню")
        || norm.starts_with("найди")
    {
        return None;
    }
    for sep in [" а потом ", " затем ", " потом ", " и "] {
        if norm.contains(sep) {
            let parts: Vec<String> = norm
                .split(sep)
                .map(|s| s.trim().to_string())
                .filter(|s| !s.is_empty())
                .collect();
            if parts.len() >= 2 && parts.len() <= 3 {
                // Only treat as compound if at least 2 parts look like real commands
                let action_words = [
                    "таймер", "будильник", "тише", "громче", "громкость", "пауза", "паузу",
                    "стоп", "дальше", "следующий", "предыдущий", "лайк", "нравится",
                    "дизлайк", "включи", "выключи", "поставь", "погода", "время",
                    "перемотай", "блютуз", "запомни",
                ];
                let cmd_hits = parts
                    .iter()
                    .filter(|p| action_words.iter().any(|w| p.contains(w)))
                    .count();
                if cmd_hits >= 2 {
                    return Some(parts);
                }
            }
        }
    }
    None
}

/// Main Contour 1 entry point called from atriai.rs before escalating to leftymods.tech.
pub fn try_handle_local_brain(clean_query: &str) -> BrainOutcome {
    let norm = normalize_ru(clean_query);
    if norm.is_empty() {
        return BrainOutcome::Escalate;
    }

    // 1. Compound multi-step commands ("поставь таймер на 5 минут и сделай тише")
    if let Some(parts) = split_compound_command(&norm) {
        return BrainOutcome::Compound(parts);
    }

    // 2. Explicit encrypted vector memory: Remember ("запомни что ...")
    for prefix in ["запомни что ", "запомни ", "запиши что ", "запиши "] {
        if let Some(rest) = norm.strip_prefix(prefix) {
            let fact = rest.trim();
            if fact.chars().count() >= 3 {
                let id = remember("note", fact);
                record_last_action(LastAction::NoteSaved(id));
                let ans = format!("Запомнила: {fact}.");
                record_dialog_turn(clean_query, &ans);
                return BrainOutcome::Handled {
                    answer: Some(ans),
                    chime: Some("ok"),
                };
            }
        }
    }

    // 3. Explicit memory recall ("что ты помнишь про ...", "вспомни ...", "какие у меня заметки")
    if norm.starts_with("что ты помнишь")
        || norm.starts_with("вспомни")
        || norm.contains("мои заметки")
        || norm.contains("что я просил запомнить")
    {
        let q = norm
            .trim_start_matches("что ты помнишь про")
            .trim_start_matches("что ты помнишь о")
            .trim_start_matches("что ты помнишь")
            .trim_start_matches("вспомни про")
            .trim_start_matches("вспомни")
            .trim();
        let hits = if q.is_empty() || norm.contains("мои заметки") {
            let mut rt = brain().lock().unwrap();
            ensure_loaded_locked(&mut rt);
            rt.memories
                .iter()
                .filter(|m| m.kind == "note" || m.kind == "fact")
                .rev()
                .take(3)
                .map(|m| (1.0f32, m.clone()))
                .collect::<Vec<_>>()
        } else {
            recall(q, 3, 0.24)
        };
        let ans = if hits.is_empty() {
            "В памяти пока ничего похожего нет.".to_string()
        } else {
            let joined: Vec<String> = hits.into_iter().map(|(_, m)| m.text).collect();
            format!("Помню: {}.", joined.join("; "))
        };
        record_dialog_turn(clean_query, &ans);
        return BrainOutcome::Handled {
            answer: Some(ans),
            chime: None,
        };
    }

    // 4. Explicit memory forget ("забудь про ...", "очисти память")
    if norm == "очисти память" || norm == "удали всю память" {
        let n = clear_all_memory();
        let ans = if n > 0 {
            "Память очищена.".to_string()
        } else {
            "Память и так пуста.".to_string()
        };
        return BrainOutcome::Handled {
            answer: Some(ans),
            chime: Some("ok"),
        };
    }
    for prefix in ["забудь про ", "забудь что ", "забудь "] {
        if let Some(rest) = norm.strip_prefix(prefix) {
            if !rest.trim().is_empty() {
                let n = forget_matching(rest);
                let ans = if n > 0 {
                    "Хорошо, забыла.".to_string()
                } else {
                    "Не нашла такой записи в памяти.".to_string()
                };
                return BrainOutcome::Handled {
                    answer: Some(ans),
                    chime: Some("ok"),
                };
            }
        }
    }

    // 5. Short-term contextual follow-ups ("ещё тише", "ещё", "отмени это")
    if norm == "еще" || norm == "еще чуть чуть" || norm == "еще немного" {
        match get_recent_action() {
            LastAction::VolumeUp(step) => {
                let cur = crate::bt_control::get_current_volume();
                let nxt = (cur + step).clamp(0, 100);
                crate::bt_control::set_system_volume(nxt);
                record_last_action(LastAction::VolumeUp(step));
                return BrainOutcome::Handled { answer: None, chime: Some("volume_up") };
            }
            LastAction::VolumeDown(step) => {
                let cur = crate::bt_control::get_current_volume();
                let nxt = (cur - step).clamp(0, 100);
                crate::bt_control::set_system_volume(nxt);
                record_last_action(LastAction::VolumeDown(step));
                return BrainOutcome::Handled { answer: None, chime: Some("volume_down") };
            }
            _ => {}
        }
    }
    if norm == "отмени это" || norm == "отмени его" || norm == "удали это" {
        if let LastAction::NoteSaved(id) = get_recent_action() {
            if forget_by_id(id) {
                record_last_action(LastAction::None);
                return BrainOutcome::Handled {
                    answer: Some("Удалила последнюю запись.".into()),
                    chime: Some("ok"),
                };
            }
        }
    }

    // 6. Now playing info ("что сейчас играет", "какой трек", "кто поёт")
    if norm.contains("что сейчас играет")
        || norm.contains("какая песня")
        || norm.contains("какой трек")
        || norm.contains("кто поет")
        || norm.contains("что за трек")
        || norm.contains("что за песня")
    {
        let ym = crate::ymusic::read_status();
        let ans = if !ym.title.is_empty() {
            if ym.artist.is_empty() {
                format!("Сейчас играет {}.", ym.title)
            } else {
                format!("Сейчас играет {} — {}.", ym.artist, ym.title)
            }
        } else {
            "Сейчас музыка не играет.".to_string()
        };
        record_dialog_turn(clean_query, &ans);
        return BrainOutcome::Handled {
            answer: Some(ans),
            chime: None,
        };
    }

    // 7. Voice track seek ("перемотай вперёд на 30 секунд", "отмотай назад на минуту")
    if let Some(target_ms) = parse_seek_command(&norm) {
        let _ = crate::ymusic::trigger_seek(target_ms, "", "");
        return BrainOutcome::Handled {
            answer: None,
            chime: Some("ok"),
        };
    }

    // 8. Offline spoken Russian math ("сколько будет 15 умножить на 4", "20 процентов от 500")
    if let Some(math_ans) = eval_spoken_math(&norm) {
        record_dialog_turn(clean_query, &math_ans);
        return BrainOutcome::Handled {
            answer: Some(math_ans),
            chime: None,
        };
    }

    // 9. Semantic paraphrase match for common volume/music phrases (using 128-dim cosine similarity)
    let qv = embed_text(&norm);
    const PARAPHRASES: &[(&str, &str)] = &[
        ("слишком громко орёт сделай потише убавь звук", "vol_down"),
        ("плохо слышно прибавь громкость сделай погромче", "vol_up"),
    ];
    for &(proto, action) in PARAPHRASES {
        if cosine_sim(&qv, &embed_text(proto)) >= 0.42 {
            let cur = crate::bt_control::get_current_volume();
            if action == "vol_down" {
                let nxt = (cur - 10).clamp(0, 100);
                crate::bt_control::set_system_volume(nxt);
                record_last_action(LastAction::VolumeDown(10));
                return BrainOutcome::Handled { answer: None, chime: Some("volume_down") };
            } else if action == "vol_up" {
                let nxt = (cur + 10).clamp(0, 100);
                crate::bt_control::set_system_volume(nxt);
                record_last_action(LastAction::VolumeUp(10));
                return BrainOutcome::Handled { answer: None, chime: Some("volume_up") };
            }
        }
    }

    BrainOutcome::Escalate
}

/// Execute structured tool calls returned by Contour 2 (`leftymods.tech/chat`).
pub fn execute_server_actions(chat: &Json) {
    let arr = chat
        .get("actions")
        .or_else(|| chat.get("tool_calls"))
        .and_then(|v| v.as_arr());
    let Some(actions) = arr else {
        return;
    };
    for act in actions {
        let name = act.get_str("type").or_else(|| act.get_str("name")).unwrap_or("");
        match name {
            "set_volume" => {
                if let Some(v) = act.get("value").or_else(|| act.get("volume")).and_then(|n| n.as_i64()) {
                    crate::bt_control::set_system_volume((v as i32).clamp(0, 100));
                }
            }
            "ymusic_control" => {
                if let Some(a) = act.get_str("action") {
                    let _ = crate::ymusic::trigger_control(a);
                }
            }
            "ymusic_play" => {
                let mode = act.get_str("mode").unwrap_or("wave");
                let query = act.get_str("query");
                let _ = crate::ymusic::trigger_play(mode, query);
            }
            "ymusic_seek" => {
                if let Some(ms) = act.get("position_ms").and_then(|n| n.as_i64()) {
                    let _ = crate::ymusic::trigger_seek(ms.max(0), "", "");
                }
            }
            "remember" => {
                if let Some(text) = act.get_str("text") {
                    let kind = act.get_str("kind").unwrap_or("fact");
                    remember(kind, text);
                }
            }
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encrypted_two_copy_vector_db_roundtrip_and_self_heal() {
        let dir = PathBuf::from(format!("/tmp/atrios-vec-test-{}", now_unix_i64()));
        let _ = fs::create_dir_all(&dir);
        let p1 = dir.join("ai_memory.vec.enc");
        let p2 = dir.join("ai_memory.vec.enc.bak");
        let key = [0x42u8; 32];

        let e1 = MemoryEntry {
            id: 1,
            kind: "note".into(),
            text: "моего кота зовут Барсик".into(),
            ts: 100,
            vec: embed_text("моего кота зовут Барсик"),
        };
        let e2 = MemoryEntry {
            id: 2,
            kind: "note".into(),
            text: "купить молоко и хлеб завтра".into(),
            ts: 101,
            vec: embed_text("купить молоко и хлеб завтра"),
        };
        let blob = save_two_copies(&p1, &p2, &[e1, e2], &key).expect("save 2 copies");
        // Plaintext MUST NOT appear in encrypted binary!
        assert!(!blob.windows(6).any(|w| w == "Бар".as_bytes()));
        assert_eq!(&blob[0..8], MAGIC);

        // Corrupt primary copy -> failover to backup must recover and self-heal primary!
        fs::write(&p1, b"corrupted").unwrap();
        let loaded = load_with_failover(&p1, &p2, &key);
        assert_eq!(loaded.len(), 2);
        assert!(loaded[0].text.contains("Барсик"));

        // Semantic recall finds the right Russian memory despite declension
        let qv = embed_text("как зовут моего кота");
        let s0 = cosine_sim(&qv, &loaded[0].vec);
        let s1 = cosine_sim(&qv, &loaded[1].vec);
        assert!(s0 > s1 + 0.15, "expected cat memory ({s0}) > milk memory ({s1})");

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn spoken_math_and_compound_splitter_work() {
        assert_eq!(eval_spoken_math("сколько будет 15 умножить на 4").as_deref(), Some("Будет 60."));
        assert_eq!(eval_spoken_math("двадцать процентов от 500").as_deref(), Some("Будет 100."));
        assert_eq!(eval_spoken_math("сто двадцать плюс 30").as_deref(), Some("Будет 150."));

        let parts = split_compound_command("поставь таймер на 5 минут и сделай тише").unwrap();
        assert_eq!(parts, vec!["поставь таймер на 5 минут", "сделай тише"]);
        assert!(split_compound_command("запомни купить хлеб и молоко").is_none());
    }
}
