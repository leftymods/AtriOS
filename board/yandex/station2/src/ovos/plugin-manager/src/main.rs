use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::sync::{Arc, Mutex};
use std::error::Error;
use std::ffi::OsStr;
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Write};
use std::os::unix::fs::{DirBuilderExt, MetadataExt, OpenOptionsExt};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use tokio::net::TcpStream;
use tokio::process::Command;
use tokio::sync::watch;
use tokio::task::JoinSet;
use tokio::time::{interval, sleep, Instant};
use tracing::{error, info, warn};

const REGISTRY_DIR: &str = "/usr/share/atrios/ovos/plugins.d";
const PLUGIN_DIR: &str = "/usr/lib/atrios/ovos/plugins";
const STATE_FILE: &str = "/var/lib/atrios/ovos/plugins-enabled.json";
const SETTINGS_FILE: &str = "/var/lib/atrios/ovos/plugin-settings.json";
const RUNTIME_STATUS_FILE: &str = "/var/run/atrios/ovos-plugins-status.json";
const BUS_HOST: &str = "127.0.0.1";
const BUS_PORT: u16 = 8181;
const PLUGIN_API_VERSION: u32 = 1;
const MAX_MANIFEST_BYTES: u64 = 64 * 1024;
const MAX_PLUGIN_BYTES: u64 = 64 * 1024 * 1024;
const MAX_PLUGINS: usize = 32;
const ALLOWED_CAPABILITIES: &[&str] = &[
    "audio.capture", "audio.playback", "wake_word", "vad", "stt", "tts",
    "intent", "transformer", "word_correction",
];

type AppResult<T> = Result<T, Box<dyn Error + Send + Sync>>;

#[derive(Debug, Clone, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
struct Manifest {
    schema: u32,
    id: String,
    name: String,
    version: String,
    api_version: u32,
    binary: String,
    capabilities: Vec<String>,
    default_enabled: bool,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct PluginState {
    schema: u32,
    enabled: Vec<String>,
}

#[derive(Debug, Clone)]
struct Plugin {
    manifest: Manifest,
    binary: PathBuf,
    enabled: bool,
}

#[derive(Debug, Clone, Default, Deserialize, Serialize)]
struct RuntimePluginStatus {
    running: bool,
    restarts: u32,
    #[serde(default)]
    last_error: String,
    #[serde(default)]
    updated_unix: u64,
}

#[derive(Debug, Default, Deserialize, Serialize)]
struct PluginSettingsStore {
    schema: u32,
    #[serde(default)]
    plugins: BTreeMap<String, BTreeMap<String, serde_json::Value>>,
}

fn validate_manifest_fields(manifest: &Manifest) -> Result<(), String> {
    if manifest.schema != 1 || manifest.api_version != PLUGIN_API_VERSION {
        return Err("unsupported schema or plugin API version".into());
    }
    if !valid_token(&manifest.id, 64)
        || manifest.name.trim().is_empty()
        || manifest.name.len() > 128
        || manifest.version.trim().is_empty()
        || manifest.version.len() > 64
    {
        return Err("invalid plugin metadata".into());
    }
    if !valid_token(&manifest.binary, 128) || manifest.binary == "." || manifest.binary == ".." {
        return Err("binary must be a plain filename, not a path".into());
    }
    if manifest.capabilities.iter().any(|cap| !ALLOWED_CAPABILITIES.contains(&cap.as_str())) {
        return Err("unknown capability".into());
    }
    let unique_caps: BTreeSet<_> = manifest.capabilities.iter().collect();
    if unique_caps.len() != manifest.capabilities.len() {
        return Err("duplicate capability".into());
    }
    Ok(())
}

fn read_runtime_status() -> BTreeMap<String, RuntimePluginStatus> {
    fs::read_to_string(RUNTIME_STATUS_FILE)
        .ok()
        .and_then(|raw| serde_json::from_str(&raw).ok())
        .unwrap_or_default()
}

fn write_runtime_status(map: &BTreeMap<String, RuntimePluginStatus>) {
    let _ = fs::create_dir_all("/var/run/atrios");
    if let Ok(json) = serde_json::to_string_pretty(map) {
        let tmp = format!("{RUNTIME_STATUS_FILE}.tmp");
        if fs::write(&tmp, json).is_ok() {
            let _ = fs::rename(&tmp, RUNTIME_STATUS_FILE);
        }
    }
}

fn read_settings() -> PluginSettingsStore {
    fs::read_to_string(SETTINGS_FILE)
        .ok()
        .and_then(|raw| serde_json::from_str(&raw).ok())
        .unwrap_or(PluginSettingsStore { schema: 1, plugins: BTreeMap::new() })
}

fn write_settings(store: &PluginSettingsStore) -> AppResult<()> {
    let parent = state_directory(true)?.ok_or("failed to create plugin state directory")?;
    let path = parent.join("plugin-settings.json");
    let nonce = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos();
    let temp = parent.join(format!(".plugin-settings.{}.{}.tmp", std::process::id(), nonce));
    let mut file = OpenOptions::new().write(true).create_new(true).mode(0o600).open(&temp)?;
    serde_json::to_writer(&mut file, store)?;
    file.write_all(b"
")?;
    file.sync_all()?;
    fs::rename(&temp, path)?;
    File::open(parent)?.sync_all()?;
    Ok(())
}

fn set_plugin_setting(id: &str, key: &str, raw_value: &str) -> AppResult<()> {
    if !valid_token(id, 64) { return Err("invalid plugin id".into()); }
    if !valid_token(key, 64) { return Err("invalid setting key".into()); }
    if raw_value.len() > 256 { return Err("setting value too long".into()); }
    let mut store = read_settings();
    store.schema = 1;
    let parsed: serde_json::Value = match raw_value {
        "true" => serde_json::Value::Bool(true),
        "false" => serde_json::Value::Bool(false),
        _ => if let Ok(n) = raw_value.parse::<i64>() {
            serde_json::Value::Number(n.into())
        } else {
            serde_json::Value::String(raw_value.to_string())
        },
    };
    store.plugins.entry(id.to_string()).or_default().insert(key.to_string(), parsed);
    write_settings(&store)?;
    println!("Updated setting {id}.{key}={raw_value}");
    Ok(())
}

fn valid_token(value: &str, max: usize) -> bool {
    let bytes = value.as_bytes();
    !bytes.is_empty() && bytes.len() <= max && bytes[0].is_ascii_alphanumeric()
        && bytes.iter().all(|b| b.is_ascii_alphanumeric() || matches!(*b, b'_' | b'-' | b'.'))
}

fn secure_root_file(path: &Path, metadata: &fs::Metadata, max_size: u64) -> AppResult<()> {
    if !metadata.is_file() || metadata.uid() != 0 {
        return Err(format!("{} must be a root-owned regular file", path.display()).into());
    }
    if metadata.mode() & 0o022 != 0 {
        return Err(format!("{} is writable by group or other", path.display()).into());
    }
    if metadata.len() > max_size {
        return Err(format!("{} exceeds the size limit", path.display()).into());
    }
    Ok(())
}

fn state_directory(create: bool) -> AppResult<Option<PathBuf>> {
    let parent = Path::new("/var/lib/atrios");
    let metadata = match fs::symlink_metadata(parent) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound && !create => return Ok(None),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
            fs::DirBuilder::new().mode(0o755).create(parent)?;
            fs::symlink_metadata(parent)?
        }
        Err(error) => return Err(error.into()),
    };
    if metadata.file_type().is_symlink() || !metadata.is_dir() || metadata.uid() != 0
        || metadata.mode() & 0o022 != 0 {
        return Err(format!("{} must be a root-owned, non-writable directory", parent.display()).into());
    }

    let directory = parent.join("ovos");
    let metadata = match fs::symlink_metadata(&directory) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound && !create => return Ok(None),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
            fs::DirBuilder::new().mode(0o700).create(&directory)?;
            fs::symlink_metadata(&directory)?
        }
        Err(error) => return Err(error.into()),
    };
    if metadata.file_type().is_symlink() || !metadata.is_dir() || metadata.uid() != 0
        || metadata.mode() & 0o077 != 0 {
        return Err(format!("{} must be a root-owned mode-0700 directory", directory.display()).into());
    }
    Ok(Some(directory))
}

fn read_state() -> AppResult<Option<PluginState>> {
    let Some(directory) = state_directory(false)? else { return Ok(None); };
    let path = directory.join(Path::new(STATE_FILE).file_name().ok_or("invalid plugin state path")?);
    let metadata = match fs::symlink_metadata(&path) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(error.into()),
    };
    if metadata.file_type().is_symlink() {
        return Err(format!("{} must not be a symlink", path.display()).into());
    }
    secure_root_file(&path, &metadata, MAX_MANIFEST_BYTES)?;
    if metadata.mode() & 0o077 != 0 {
        return Err(format!("{} must be mode 0600", path.display()).into());
    }
    let state: PluginState = serde_json::from_reader(File::open(&path)?)?;
    if state.schema != 1 {
        return Err(format!("unsupported plugin state schema {}", state.schema).into());
    }
    let mut seen = HashSet::new();
    for id in &state.enabled {
        if !valid_token(id, 64) || !seen.insert(id) {
            return Err("plugin state contains an invalid or duplicate id".into());
        }
    }
    Ok(Some(state))
}

fn resolve_binary(name: &str) -> AppResult<PathBuf> {
    if !valid_token(name, 128) || name == "." || name == ".." {
        return Err("binary must be a plain filename, not a path".into());
    }
    let directory = fs::canonicalize(PLUGIN_DIR)?;
    let path = Path::new(PLUGIN_DIR).join(name);
    let metadata = fs::symlink_metadata(&path)?;
    if metadata.file_type().is_symlink() {
        return Err(format!("plugin binary {} must not be a symlink", path.display()).into());
    }
    secure_root_file(&path, &metadata, MAX_PLUGIN_BYTES)?;
    if metadata.mode() & 0o111 == 0 {
        return Err(format!("plugin binary {} is not executable", path.display()).into());
    }
    let resolved = fs::canonicalize(&path)?;
    if resolved.parent() != Some(directory.as_path()) {
        return Err(format!("plugin binary {} escapes its installation directory", path.display()).into());
    }
    let mut header = [0u8; 20];
    File::open(&resolved)?.read_exact(&mut header)?;
    if &header[0..4] != b"\x7fELF" || header[4] != 2 || header[5] != 1
        || u16::from_le_bytes([header[18], header[19]]) != 183 {
        return Err(format!("plugin {} must be an AArch64 ELF executable", path.display()).into());
    }
    Ok(resolved)
}

fn load_registry() -> AppResult<Vec<Plugin>> {
    let mut manifests = Vec::new();
    match fs::read_dir(REGISTRY_DIR) {
        Ok(entries) => {
            for entry in entries {
                let path = entry?.path();
                if path.extension() != Some(OsStr::new("json")) { continue; }
                let metadata = fs::symlink_metadata(&path)?;
                if metadata.file_type().is_symlink() {
                    return Err(format!("manifest {} must not be a symlink", path.display()).into());
                }
                secure_root_file(&path, &metadata, MAX_MANIFEST_BYTES)?;
                let manifest: Manifest = serde_json::from_reader(File::open(&path)?)?;
                if manifest.schema != 1 || manifest.api_version != PLUGIN_API_VERSION {
                    return Err(format!("{} has an unsupported schema or plugin API version", path.display()).into());
                }
                if !valid_token(&manifest.id, 64) || manifest.name.trim().is_empty()
                    || manifest.name.len() > 128 || manifest.version.trim().is_empty()
                    || manifest.version.len() > 64 {
                    return Err(format!("{} has invalid plugin metadata", path.display()).into());
                }
                if manifest.capabilities.iter().any(|cap| !ALLOWED_CAPABILITIES.contains(&cap.as_str())) {
                    return Err(format!("{} requests an unknown capability", path.display()).into());
                }
                let unique_caps: BTreeSet<_> = manifest.capabilities.iter().collect();
                if unique_caps.len() != manifest.capabilities.len() {
                    return Err(format!("{} repeats a capability", path.display()).into());
                }
                let binary = resolve_binary(&manifest.binary)?;
                if manifests.len() >= MAX_PLUGINS {
                    return Err(format!("plugin count exceeds the limit of {MAX_PLUGINS}").into());
                }
                manifests.push((manifest, binary));
            }
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(Vec::new()),
        Err(error) => return Err(error.into()),
    }

    manifests.sort_by(|a, b| a.0.id.cmp(&b.0.id));
    for pair in manifests.windows(2) {
        if pair[0].0.id == pair[1].0.id {
            return Err(format!("duplicate plugin id {}", pair[0].0.id).into());
        }
    }

    let state = read_state()?;
    let enabled_ids: HashSet<String> = state.as_ref()
        .map(|state| state.enabled.iter().cloned().collect()).unwrap_or_default();
    if let Some(state) = &state {
        let installed: HashSet<&str> = manifests.iter().map(|(m, _)| m.id.as_str()).collect();
        if let Some(unknown) = state.enabled.iter().find(|id| !installed.contains(id.as_str())) {
            warn!(plugin = %unknown, "ignoring enabled state for a plugin that is not installed");
        }
    }
    Ok(manifests.into_iter().map(|(manifest, binary)| {
        let enabled = state.as_ref()
            .map(|_| enabled_ids.contains(&manifest.id))
            .unwrap_or(manifest.default_enabled);
        Plugin { manifest, binary, enabled }
    }).collect())
}

fn write_state(ids: &BTreeSet<String>) -> AppResult<()> {
    let parent = state_directory(true)?.ok_or("failed to create plugin state directory")?;
    let path = parent.join(Path::new(STATE_FILE).file_name().ok_or("invalid plugin state path")?);
    let nonce = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos();
    let temp = parent.join(format!(".plugins-enabled.{}.{}.tmp", std::process::id(), nonce));
    let state = PluginState { schema: 1, enabled: ids.iter().cloned().collect() };
    let mut file = OpenOptions::new().write(true).create_new(true).mode(0o600).open(&temp)?;
    serde_json::to_writer(&mut file, &state)?;
    file.write_all(b"\n")?;
    file.sync_all()?;
    fs::rename(&temp, path)?;
    File::open(parent)?.sync_all()?;
    Ok(())
}

fn change_state(id: &str, enable: bool) -> AppResult<()> {
    if !valid_token(id, 64) { return Err("invalid plugin id".into()); }
    let plugins = load_registry()?;
    if enable && !plugins.iter().any(|plugin| plugin.manifest.id == id) {
        return Err(format!("plugin {id} is not installed").into());
    }
    let mut ids: BTreeSet<String> = read_state()?.map(|state| state.enabled.into_iter().collect())
        .unwrap_or_else(|| plugins.iter().filter(|plugin| plugin.enabled)
            .map(|plugin| plugin.manifest.id.clone()).collect());
    if enable { ids.insert(id.to_string()); } else { ids.remove(id); }
    write_state(&ids)?;
    println!("Plugin {id} {}. The running manager reloads within 2 seconds.",
        if enable { "enabled" } else { "disabled" });
    Ok(())
}

fn print_plugins(plugins: &[Plugin]) {
    if plugins.is_empty() {
        println!("No OVOS Rust plugins are installed.");
        return;
    }
    for plugin in plugins {
        println!("{}\t{}\t{}\t{}\t{}",
            if plugin.enabled { "enabled" } else { "disabled" },
            plugin.manifest.id, plugin.manifest.version, plugin.manifest.name,
            plugin.manifest.capabilities.join(","));
    }
}

fn print_plugins_json(plugins: &[Plugin]) {
    let runtime = read_runtime_status();
    let settings = read_settings();
    let entries: Vec<_> = plugins.iter().map(|plugin| {
        let rt = runtime.get(&plugin.manifest.id).cloned().unwrap_or_default();
        let cfg = settings.plugins.get(&plugin.manifest.id).cloned().unwrap_or_default();
        serde_json::json!({
            "id": plugin.manifest.id,
            "name": plugin.manifest.name,
            "version": plugin.manifest.version,
            "api_version": plugin.manifest.api_version,
            "capabilities": plugin.manifest.capabilities,
            "enabled": plugin.enabled,
            "running": rt.running,
            "restarts": rt.restarts,
            "last_error": rt.last_error,
            "settings": cfg
        })
    }).collect();
    println!("{}", serde_json::json!({
        "type": "ovos_plugins",
        "available": true,
        "plugins": entries
    }));
}

async fn wait_for_bus() -> AppResult<()> {
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    for _ in 0..150 {
        if let Ok(mut stream) = TcpStream::connect((BUS_HOST, BUS_PORT)).await {
            let request = b"GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
            if stream.write_all(request).await.is_ok() {
                let mut response = Vec::new();
                let _ = stream.read_to_end(&mut response).await;
                if response.starts_with(b"HTTP/1.1 200") || response.starts_with(b"HTTP/1.0 200") {
                    return Ok(());
                }
            }
        }
        sleep(Duration::from_millis(200)).await;
    }
    Err("OVOS MessageBus did not become healthy on 127.0.0.1:8181".into())
}

fn update_status(
    state: &Arc<Mutex<BTreeMap<String, RuntimePluginStatus>>>,
    id: &str,
    running: bool,
    inc_restart: bool,
    err: Option<String>,
) {
    if let Ok(mut guard) = state.lock() {
        let entry = guard.entry(id.to_string()).or_default();
        entry.running = running;
        if inc_restart {
            entry.restarts = entry.restarts.saturating_add(1);
        }
        if let Some(msg) = err {
            entry.last_error = msg;
        } else if running {
            entry.last_error.clear();
        }
        entry.updated_unix = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0);
        write_runtime_status(&guard);
    }
}

async fn supervise_plugin(
    plugin: Plugin,
    mut shutdown: watch::Receiver<bool>,
    status_map: Arc<Mutex<BTreeMap<String, RuntimePluginStatus>>>,
) {
    let mut delay = 1u64;
    loop {
        if *shutdown.borrow() { return; }
        let mut command = Command::new(&plugin.binary);
        command.env_clear()
            .env("OVOS_PLUGIN_ID", &plugin.manifest.id)
            .env("OVOS_BUS_HOST", BUS_HOST)
            .env("OVOS_BUS_PORT", BUS_PORT.to_string())
            .env("OVOS_BUS_ROUTE", "/core")
            .env("OVOS_PLUGIN_CAPABILITIES", plugin.manifest.capabilities.join(","))
            .env("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
            .current_dir("/")
            .stdin(std::process::Stdio::null())
            .stdout(std::process::Stdio::inherit())
            .stderr(std::process::Stdio::inherit())
            .kill_on_drop(true);
        let mut child = match command.spawn() {
            Ok(child) => {
                info!(plugin = %plugin.manifest.id, "started OVOS Rust plugin");
                update_status(&status_map, &plugin.manifest.id, true, false, None);
                child
            }
            Err(error) => {
                error!(plugin = %plugin.manifest.id, %error, "failed to start plugin");
                update_status(&status_map, &plugin.manifest.id, false, true, Some(format!("spawn failed: {error}")));
                tokio::select! {
                    _ = sleep(Duration::from_secs(delay)) => {},
                    result = shutdown.changed() => { if result.is_ok() && *shutdown.borrow() { return; } }
                }
                delay = (delay * 2).min(60);
                continue;
            }
        };
        let started = Instant::now();
        tokio::select! {
            result = child.wait() => {
                match result {
                    Ok(status) => {
                        warn!(plugin = %plugin.manifest.id, %status, "plugin exited; restarting");
                        update_status(&status_map, &plugin.manifest.id, false, true, Some(format!("exited: {status}")));
                    }
                    Err(error) => {
                        error!(plugin = %plugin.manifest.id, %error, "failed waiting for plugin");
                        update_status(&status_map, &plugin.manifest.id, false, true, Some(format!("wait failed: {error}")));
                    }
                }
            }
            result = shutdown.changed() => {
                if result.is_ok() && *shutdown.borrow() {
                    let _ = child.kill().await;
                    let _ = child.wait().await;
                    update_status(&status_map, &plugin.manifest.id, false, false, None);
                    return;
                }
            }
        }
        if started.elapsed() >= Duration::from_secs(60) { delay = 1; }
        tokio::select! {
            _ = sleep(Duration::from_secs(delay)) => {},
            result = shutdown.changed() => { if result.is_ok() && *shutdown.borrow() { return; } }
        }
        delay = (delay * 2).min(60);
    }
}

async fn wait_for_shutdown() -> std::io::Result<()> {
    let mut terminate = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())?;
    tokio::select! {
        result = tokio::signal::ctrl_c() => result,
        _ = terminate.recv() => Ok(()),
    }
}

async fn run_daemon() -> AppResult<()> {
    loop {
        match wait_for_bus().await {
            Ok(()) => break,
            Err(error) => {
                warn!(%error, "OVOS MessageBus is unavailable; retrying startup");
                sleep(Duration::from_secs(2)).await;
            }
        }
    }
    let mut active = load_registry()?.into_iter().filter(|plugin| plugin.enabled).collect::<Vec<_>>();
    info!(count = active.len(), "OVOS Rust plugin supervisor ready");
    let (mut shutdown_tx, initial_rx) = watch::channel(false);
    let status_map = Arc::new(Mutex::new(BTreeMap::<String, RuntimePluginStatus>::new()));
    let mut tasks = JoinSet::new();
    for plugin in &active { tasks.spawn(supervise_plugin(plugin.clone(), initial_rx.clone(), Arc::clone(&status_map))); }

    let shutdown = wait_for_shutdown();
    tokio::pin!(shutdown);
    let mut reload = interval(Duration::from_secs(2));
    loop {
        tokio::select! {
            result = &mut shutdown => {
                result?;
                let _ = shutdown_tx.send(true);
                while tasks.join_next().await.is_some() {}
                return Ok(());
            }
            _ = reload.tick() => {
                match load_registry() {
                    Ok(mut current) => {
                        current.retain(|plugin| plugin.enabled);
                        current.sort_by(|a, b| a.manifest.id.cmp(&b.manifest.id));
                        let mut previous = active.clone();
                        previous.sort_by(|a, b| a.manifest.id.cmp(&b.manifest.id));
                        let before: Vec<_> = previous.iter().map(|p| (&p.manifest, &p.binary)).collect();
                        let after: Vec<_> = current.iter().map(|p| (&p.manifest, &p.binary)).collect();
                        if before != after {
                            let _ = shutdown_tx.send(true);
                            while tasks.join_next().await.is_some() {}
                            let (new_tx, new_rx) = watch::channel(false);
                            for plugin in &current {
                                tasks.spawn(supervise_plugin(plugin.clone(), new_rx.clone(), Arc::clone(&status_map)));
                            }
                            active = current;
                            shutdown_tx = new_tx;
                            info!(count = active.len(), "reloaded OVOS Rust plugin set");
                        }
                    }
                    Err(error) => error!(%error, "plugin registry reload rejected; retaining current plugins"),
                }
            }
        }
    }
}
#[tokio::main]
async fn main() -> AppResult<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::try_from_default_env()
            .unwrap_or_else(|_| "info".into()))
        .init();
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() || (args.len() == 1 && args[0] == "run") {
        return run_daemon().await;
    }
    match args.as_slice() {
        [list, format] if list == "--list" && format == "--json" => {
            print_plugins_json(&load_registry()?);
            Ok(())
        }
        [arg] if arg == "--list" || arg == "--validate" => {
            let plugins = load_registry()?;
            if arg == "--list" { print_plugins(&plugins); }
            else { println!("Validated {} OVOS Rust plugin manifest(s).", plugins.len()); }
            Ok(())
        }
        [arg, id] if arg == "--enable" || arg == "--disable" =>
            change_state(id, arg == "--enable"),
        [arg, id, key, val] if arg == "--set-setting" =>
            set_plugin_setting(id, key, val),
        _ => {
            eprintln!("Usage: atrios-ovos-plugin-manager [run|--list|--validate|--enable ID|--disable ID]");
            std::process::exit(2);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_manifest() -> Manifest {
        Manifest {
            schema: 1,
            id: "ovos-dinkum-listener".into(),
            name: "OVOS Dinkum Listener".into(),
            version: "0.1.0".into(),
            api_version: 1,
            binary: "ovos-dinkum-listener".into(),
            capabilities: vec!["audio.capture".into(), "vad".into(), "wake_word".into(), "stt".into()],
            default_enabled: true,
        }
    }

    #[test]
    fn manifest_validation_accepts_valid_and_rejects_bad_fields() {
        assert!(validate_manifest_fields(&sample_manifest()).is_ok());

        let mut bad_schema = sample_manifest();
        bad_schema.schema = 2;
        assert!(validate_manifest_fields(&bad_schema).is_err());

        let mut bad_bin = sample_manifest();
        bad_bin.binary = "../evil".into();
        assert!(validate_manifest_fields(&bad_bin).is_err());

        let mut bad_cap = sample_manifest();
        bad_cap.capabilities.push("shell.exec".into());
        assert!(validate_manifest_fields(&bad_cap).is_err());

        let mut dup_cap = sample_manifest();
        dup_cap.capabilities.push("vad".into());
        assert!(validate_manifest_fields(&dup_cap).is_err());
    }

    #[test]
    fn status_tracker_records_errors_and_clears_on_recovery() {
        let map = Arc::new(Mutex::new(BTreeMap::new()));
        update_status(&map, "ovos-tts-audio", false, true, Some("aplay missing".into()));
        {
            let g = map.lock().unwrap();
            let s = &g["ovos-tts-audio"];
            assert!(!s.running);
            assert_eq!(s.restarts, 1);
            assert_eq!(s.last_error, "aplay missing");
        }
        update_status(&map, "ovos-tts-audio", true, false, None);
        {
            let g = map.lock().unwrap();
            let s = &g["ovos-tts-audio"];
            assert!(s.running);
            assert!(s.last_error.is_empty());
        }
    }
}
