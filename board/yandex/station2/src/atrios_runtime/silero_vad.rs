//! Neural Silero VAD v5 (ONNX) via dynamically loaded `/usr/lib/atrios/libonnxruntime.so`.
//!
//! Fail-safe design:
//! - Uses `dlopen` + exact verified `OrtApi` vtable indices (ORT_API_VERSION = 28) — zero build-time
//!   linker dependencies, works cleanly with single-command `rustc` builds and host unit tests.
//! - If `libonnxruntime.so` or `silero_vad.onnx` is absent, or `/var/lib/atrios/silero_vad_disable`
//!   exists, returns `None` and `atriai.rs` falls back seamlessly to RMS VAD.
//! - Real-time watchdog: 8 consecutive hops > 25 ms automatically disable neural VAD.

use crate::sys::{dlopen, dlsym, RTLD_NOW};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::path::Path;
use std::sync::{Mutex, OnceLock};
use std::time::Instant;

const ORT_LIB_PATHS: &[&str] = &[
    "/usr/lib/atrios/libonnxruntime.so",
    "/usr/lib/libonnxruntime.so",
    "/usr/lib/aarch64-linux-gnu/libonnxruntime.so",
];

const MODEL_PATHS: &[&str] = &[
    "/usr/share/atrios/models/silero/silero_vad.onnx",
    "/var/lib/atrios/models/silero_vad.onnx",
];

const DISABLE_FLAG_PATH: &str = "/var/lib/atrios/silero_vad_disable";

const SILERO_SR: i64 = 16000;
const SILERO_HOP: usize = 512; // 32 ms @ 16 kHz
const SILERO_CTX: usize = 64;  // v5 context prepended to each 512-sample hop
const SILERO_IN_LEN: usize = SILERO_CTX + SILERO_HOP; // 576
const SILERO_STATE_LEN: usize = 2 * 1 * 128; // [2, 1, 128] = 256 floats

// Exact OrtApi vtable indices verified against board/yandex/station2/src/third_party/onnxruntime_c_api.h
const IDX_GET_ERROR_MESSAGE: usize = 2;
const IDX_CREATE_ENV: usize = 3;
const IDX_CREATE_SESSION: usize = 7;
const IDX_RUN: usize = 9;
const IDX_CREATE_SESSION_OPTIONS: usize = 10;
const IDX_SET_OPT_LEVEL: usize = 23;
const IDX_SET_INTRA_THREADS: usize = 24;
const IDX_SET_INTER_THREADS: usize = 25;
const IDX_CREATE_TENSOR_WITH_DATA: usize = 49;
const IDX_GET_TENSOR_MUTABLE_DATA: usize = 51;
const IDX_CREATE_CPU_MEM_INFO: usize = 69;
const IDX_RELEASE_ENV: usize = 92;
const IDX_RELEASE_STATUS: usize = 93;
const IDX_RELEASE_MEM_INFO: usize = 94;
const IDX_RELEASE_SESSION: usize = 95;
const IDX_RELEASE_VALUE: usize = 96;
const IDX_RELEASE_SESSION_OPTIONS: usize = 100;

const ONNX_TENSOR_FLOAT: c_int = 1;
const ONNX_TENSOR_INT64: c_int = 7;
const ORT_ARENA_ALLOCATOR: c_int = 0;
const ORT_MEM_TYPE_DEFAULT: c_int = 0;
const ORT_LOGGING_ERROR: c_int = 3;
const ORT_ENABLE_ALL: c_int = 99;

#[repr(C)]
struct OrtApiBase {
    get_api: unsafe extern "C" fn(version: u32) -> *const *const c_void,
    get_version_string: unsafe extern "C" fn() -> *const c_char,
}

type OrtGetApiBaseFn = unsafe extern "C" fn() -> *const OrtApiBase;

type FnGetErrorMessage = unsafe extern "C" fn(status: *const c_void) -> *const c_char;
type FnReleaseStatus = unsafe extern "C" fn(status: *mut c_void);
type FnCreateEnv = unsafe extern "C" fn(log_level: c_int, logid: *const c_char, out: *mut *mut c_void) -> *mut c_void;
type FnReleaseEnv = unsafe extern "C" fn(env: *mut c_void);
type FnCreateSessionOptions = unsafe extern "C" fn(out: *mut *mut c_void) -> *mut c_void;
type FnReleaseSessionOptions = unsafe extern "C" fn(opts: *mut c_void);
type FnSetOptLevel = unsafe extern "C" fn(opts: *mut c_void, level: c_int) -> *mut c_void;
type FnSetThreads = unsafe extern "C" fn(opts: *mut c_void, threads: c_int) -> *mut c_void;
type FnCreateSession = unsafe extern "C" fn(
    env: *const c_void,
    model_path: *const c_char,
    opts: *const c_void,
    out: *mut *mut c_void,
) -> *mut c_void;
type FnReleaseSession = unsafe extern "C" fn(sess: *mut c_void);
type FnCreateCpuMemInfo = unsafe extern "C" fn(
    alloc_type: c_int,
    mem_type: c_int,
    out: *mut *mut c_void,
) -> *mut c_void;
type FnReleaseMemInfo = unsafe extern "C" fn(info: *mut c_void);
type FnCreateTensorWithData = unsafe extern "C" fn(
    info: *const c_void,
    p_data: *mut c_void,
    p_data_len: usize,
    shape: *const i64,
    shape_len: usize,
    elem_type: c_int,
    out: *mut *mut c_void,
) -> *mut c_void;
type FnGetTensorMutableData = unsafe extern "C" fn(val: *mut c_void, out: *mut *mut c_void) -> *mut c_void;
type FnReleaseValue = unsafe extern "C" fn(val: *mut c_void);
type FnRun = unsafe extern "C" fn(
    sess: *mut c_void,
    run_opts: *const c_void,
    input_names: *const *const c_char,
    inputs: *const *const c_void,
    input_len: usize,
    output_names: *const *const c_char,
    output_names_len: usize,
    outputs: *mut *mut c_void,
) -> *mut c_void;

struct OrtFns {
    get_error_message: FnGetErrorMessage,
    release_status: FnReleaseStatus,
    create_env: FnCreateEnv,
    release_env: FnReleaseEnv,
    create_session_options: FnCreateSessionOptions,
    release_session_options: FnReleaseSessionOptions,
    set_opt_level: FnSetOptLevel,
    set_intra_threads: FnSetThreads,
    set_inter_threads: FnSetThreads,
    create_session: FnCreateSession,
    release_session: FnReleaseSession,
    create_cpu_mem_info: FnCreateCpuMemInfo,
    release_mem_info: FnReleaseMemInfo,
    create_tensor_with_data: FnCreateTensorWithData,
    get_tensor_mutable_data: FnGetTensorMutableData,
    release_value: FnReleaseValue,
    run: FnRun,
}

impl OrtFns {
    unsafe fn from_table(tbl: *const *const c_void) -> Self {
        Self {
            get_error_message: std::mem::transmute(*tbl.add(IDX_GET_ERROR_MESSAGE)),
            release_status: std::mem::transmute(*tbl.add(IDX_RELEASE_STATUS)),
            create_env: std::mem::transmute(*tbl.add(IDX_CREATE_ENV)),
            release_env: std::mem::transmute(*tbl.add(IDX_RELEASE_ENV)),
            create_session_options: std::mem::transmute(*tbl.add(IDX_CREATE_SESSION_OPTIONS)),
            release_session_options: std::mem::transmute(*tbl.add(IDX_RELEASE_SESSION_OPTIONS)),
            set_opt_level: std::mem::transmute(*tbl.add(IDX_SET_OPT_LEVEL)),
            set_intra_threads: std::mem::transmute(*tbl.add(IDX_SET_INTRA_THREADS)),
            set_inter_threads: std::mem::transmute(*tbl.add(IDX_SET_INTER_THREADS)),
            create_session: std::mem::transmute(*tbl.add(IDX_CREATE_SESSION)),
            release_session: std::mem::transmute(*tbl.add(IDX_RELEASE_SESSION)),
            create_cpu_mem_info: std::mem::transmute(*tbl.add(IDX_CREATE_CPU_MEM_INFO)),
            release_mem_info: std::mem::transmute(*tbl.add(IDX_RELEASE_MEM_INFO)),
            create_tensor_with_data: std::mem::transmute(*tbl.add(IDX_CREATE_TENSOR_WITH_DATA)),
            get_tensor_mutable_data: std::mem::transmute(*tbl.add(IDX_GET_TENSOR_MUTABLE_DATA)),
            release_value: std::mem::transmute(*tbl.add(IDX_RELEASE_VALUE)),
            run: std::mem::transmute(*tbl.add(IDX_RUN)),
        }
    }

    unsafe fn check(&self, st: *mut c_void, ctx: &str) -> Result<(), String> {
        if st.is_null() {
            return Ok(());
        }
        let msg_ptr = (self.get_error_message)(st);
        let msg = if msg_ptr.is_null() {
            "unknown ORT error".to_string()
        } else {
            CStr::from_ptr(msg_ptr).to_string_lossy().into_owned()
        };
        (self.release_status)(st);
        Err(format!("{ctx}: {msg}"))
    }
}

struct SileroEngine {
    fns: OrtFns,
    env: *mut c_void,
    sess: *mut c_void,
    mem_info: *mut c_void,
    // Pre-allocated buffers (zero per-hop heap allocations)
    in_buf: [f32; SILERO_IN_LEN],
    ctx_buf: [f32; SILERO_CTX],
    state_buf: [f32; SILERO_STATE_LEN],
    sr_buf: [i64; 1],
    pending: Vec<f32>,
    last_prob: f32,
    consec_overruns: u32,
    disabled: bool,
    // Cached CStrings for input/output tensor names
    c_input: CString,
    c_state: CString,
    c_sr: CString,
    c_output: CString,
    c_staten: CString,
}

unsafe impl Send for SileroEngine {}

impl Drop for SileroEngine {
    fn drop(&mut self) {
        unsafe {
            if !self.mem_info.is_null() {
                (self.fns.release_mem_info)(self.mem_info);
            }
            if !self.sess.is_null() {
                (self.fns.release_session)(self.sess);
            }
            if !self.env.is_null() {
                (self.fns.release_env)(self.env);
            }
        }
    }
}

impl SileroEngine {
    fn load() -> Result<Self, String> {
        if Path::new(DISABLE_FLAG_PATH).exists()
            || std::env::var("ATRIOS_SILERO_VAD_DISABLE").as_deref() == Ok("1")
        {
            return Err("disabled by flag".into());
        }
        let model_path = MODEL_PATHS
            .iter()
            .copied()
            .find(|p| Path::new(p).exists())
            .ok_or_else(|| "silero_vad.onnx not found".to_string())?;

        let mut handle: *mut c_void = std::ptr::null_mut();
        for lib in ORT_LIB_PATHS {
            if !Path::new(lib).exists() {
                continue;
            }
            let c_lib = CString::new(*lib).unwrap();
            let h = unsafe { dlopen(c_lib.as_ptr(), RTLD_NOW) };
            if !h.is_null() {
                handle = h;
                break;
            }
        }
        if handle.is_null() {
            return Err("libonnxruntime.so not found or failed to load".into());
        }

        let sym_name = CString::new("OrtGetApiBase").unwrap();
        let raw_base_fn = unsafe { dlsym(handle, sym_name.as_ptr()) };
        if raw_base_fn.is_null() {
            return Err("Missing OrtGetApiBase symbol".into());
        }
        let get_base: OrtGetApiBaseFn = unsafe { std::mem::transmute(raw_base_fn) };
        let base_ptr = unsafe { get_base() };
        if base_ptr.is_null() {
            return Err("OrtGetApiBase returned null".into());
        }
        // Try ORT API 28 first (matching board libonnxruntime.so), then fallback versions
        let mut api_tbl: *const *const c_void = std::ptr::null();
        for ver in [28u32, 22, 17] {
            let tbl = unsafe { ((*base_ptr).get_api)(ver) };
            if !tbl.is_null() {
                api_tbl = tbl;
                break;
            }
        }
        if api_tbl.is_null() {
            return Err("OrtGetApi returned null".into());
        }
        let fns = unsafe { OrtFns::from_table(api_tbl) };

        let mut env: *mut c_void = std::ptr::null_mut();
        let c_logid = CString::new("silero_vad").unwrap();
        unsafe { fns.check((fns.create_env)(ORT_LOGGING_ERROR, c_logid.as_ptr(), &mut env), "CreateEnv") }?;

        let mut opts: *mut c_void = std::ptr::null_mut();
        if let Err(e) = unsafe { fns.check((fns.create_session_options)(&mut opts), "CreateSessionOptions") } {
            unsafe { (fns.release_env)(env) };
            return Err(e);
        }
        let _ = unsafe { fns.check((fns.set_intra_threads)(opts, 1), "SetIntraOpNumThreads") };
        let _ = unsafe { fns.check((fns.set_inter_threads)(opts, 1), "SetInterOpNumThreads") };
        let _ = unsafe { fns.check((fns.set_opt_level)(opts, ORT_ENABLE_ALL), "SetOptLevel") };

        let c_model = CString::new(model_path).unwrap();
        let mut sess: *mut c_void = std::ptr::null_mut();
        let sess_res = unsafe { fns.check((fns.create_session)(env, c_model.as_ptr(), opts, &mut sess), "CreateSession") };
        unsafe { (fns.release_session_options)(opts) };
        if let Err(e) = sess_res {
            unsafe { (fns.release_env)(env) };
            return Err(e);
        }

        let mut mem_info: *mut c_void = std::ptr::null_mut();
        if let Err(e) = unsafe {
            fns.check(
                (fns.create_cpu_mem_info)(ORT_ARENA_ALLOCATOR, ORT_MEM_TYPE_DEFAULT, &mut mem_info),
                "CreateCpuMemoryInfo",
            )
        } {
            unsafe {
                (fns.release_session)(sess);
                (fns.release_env)(env);
            }
            return Err(e);
        }

        println!("[silero-vad] loaded neural VAD v5 ({model_path})");
        Ok(Self {
            fns,
            env,
            sess,
            mem_info,
            in_buf: [0.0f32; SILERO_IN_LEN],
            ctx_buf: [0.0f32; SILERO_CTX],
            state_buf: [0.0f32; SILERO_STATE_LEN],
            sr_buf: [SILERO_SR],
            pending: Vec::with_capacity(1024),
            last_prob: 0.0,
            consec_overruns: 0,
            disabled: false,
            c_input: CString::new("input").unwrap(),
            c_state: CString::new("state").unwrap(),
            c_sr: CString::new("sr").unwrap(),
            c_output: CString::new("output").unwrap(),
            c_staten: CString::new("stateN").unwrap(),
        })
    }

    fn reset_state(&mut self) {
        self.ctx_buf.fill(0.0);
        self.state_buf.fill(0.0);
        self.pending.clear();
        self.last_prob = 0.0;
    }

    fn run_hop(&mut self, samples512: &[f32]) -> Result<f32, String> {
        // Silero v5 input = [64 context samples | 512 new samples]
        self.in_buf[..SILERO_CTX].copy_from_slice(&self.ctx_buf);
        self.in_buf[SILERO_CTX..].copy_from_slice(samples512);
        // Save last 64 samples as context for next hop
        self.ctx_buf.copy_from_slice(&samples512[SILERO_HOP - SILERO_CTX..]);

        let in_shape: [i64; 2] = [1, SILERO_IN_LEN as i64];
        let st_shape: [i64; 3] = [2, 1, 128];
        let sr_shape: [i64; 1] = [1];

        let mut v_in: *mut c_void = std::ptr::null_mut();
        let mut v_st: *mut c_void = std::ptr::null_mut();
        let mut v_sr: *mut c_void = std::ptr::null_mut();

        unsafe {
            self.fns.check(
                (self.fns.create_tensor_with_data)(
                    self.mem_info,
                    self.in_buf.as_mut_ptr() as *mut c_void,
                    SILERO_IN_LEN * std::mem::size_of::<f32>(),
                    in_shape.as_ptr(),
                    in_shape.len(),
                    ONNX_TENSOR_FLOAT,
                    &mut v_in,
                ),
                "CreateTensor(input)",
            )?;
            if let Err(e) = self.fns.check(
                (self.fns.create_tensor_with_data)(
                    self.mem_info,
                    self.state_buf.as_mut_ptr() as *mut c_void,
                    SILERO_STATE_LEN * std::mem::size_of::<f32>(),
                    st_shape.as_ptr(),
                    st_shape.len(),
                    ONNX_TENSOR_FLOAT,
                    &mut v_st,
                ),
                "CreateTensor(state)",
            ) {
                (self.fns.release_value)(v_in);
                return Err(e);
            }
            if let Err(e) = self.fns.check(
                (self.fns.create_tensor_with_data)(
                    self.mem_info,
                    self.sr_buf.as_mut_ptr() as *mut c_void,
                    std::mem::size_of::<i64>(),
                    sr_shape.as_ptr(),
                    sr_shape.len(),
                    ONNX_TENSOR_INT64,
                    &mut v_sr,
                ),
                "CreateTensor(sr)",
            ) {
                (self.fns.release_value)(v_st);
                (self.fns.release_value)(v_in);
                return Err(e);
            }

            let in_names = [self.c_input.as_ptr(), self.c_state.as_ptr(), self.c_sr.as_ptr()];
            let in_vals = [v_in as *const c_void, v_st as *const c_void, v_sr as *const c_void];
            let out_names = [self.c_output.as_ptr(), self.c_staten.as_ptr()];
            let mut out_vals: [*mut c_void; 2] = [std::ptr::null_mut(), std::ptr::null_mut()];

            let t0 = Instant::now();
            let run_res = self.fns.check(
                (self.fns.run)(
                    self.sess,
                    std::ptr::null(),
                    in_names.as_ptr(),
                    in_vals.as_ptr(),
                    3,
                    out_names.as_ptr(),
                    2,
                    out_vals.as_mut_ptr(),
                ),
                "Run",
            );
            let elapsed_us = t0.elapsed().as_micros();

            (self.fns.release_value)(v_sr);
            (self.fns.release_value)(v_st);
            (self.fns.release_value)(v_in);

            if let Err(e) = run_res {
                for ov in out_vals {
                    if !ov.is_null() {
                        (self.fns.release_value)(ov);
                    }
                }
                return Err(e);
            }

            let mut prob = 0.0f32;
            let mut p_ptr: *mut c_void = std::ptr::null_mut();
            if !out_vals[0].is_null()
                && self
                    .fns
                    .check((self.fns.get_tensor_mutable_data)(out_vals[0], &mut p_ptr), "GetData(output)")
                    .is_ok()
                && !p_ptr.is_null()
            {
                prob = *(p_ptr as *const f32);
            }
            let mut s_ptr: *mut c_void = std::ptr::null_mut();
            if !out_vals[1].is_null()
                && self
                    .fns
                    .check((self.fns.get_tensor_mutable_data)(out_vals[1], &mut s_ptr), "GetData(stateN)")
                    .is_ok()
                && !s_ptr.is_null()
            {
                std::ptr::copy_nonoverlapping(
                    s_ptr as *const f32,
                    self.state_buf.as_mut_ptr(),
                    SILERO_STATE_LEN,
                );
            }
            for ov in out_vals {
                if !ov.is_null() {
                    (self.fns.release_value)(ov);
                }
            }

            // Watchdog: 8 consecutive hops > 25ms disables neural VAD cleanly
            if elapsed_us > 25_000 {
                self.consec_overruns += 1;
                if self.consec_overruns >= 8 {
                    self.disabled = true;
                    eprintln!("[silero-vad] watchdog triggered (last hop {elapsed_us} us); falling back to RMS VAD");
                }
            } else {
                self.consec_overruns = 0;
            }
            Ok(prob.clamp(0.0, 1.0))
        }
    }

    fn feed_pcm16(&mut self, pcm: &[u8]) -> Option<f32> {
        if self.disabled {
            return None;
        }
        for pair in pcm.chunks_exact(2) {
            let s = i16::from_le_bytes([pair[0], pair[1]]) as f32 / 32768.0;
            self.pending.push(s.clamp(-1.0, 1.0));
        }
        let mut ran = false;
        let mut max_p = 0.0f32;
        while self.pending.len() >= SILERO_HOP {
            let hop: [f32; SILERO_HOP] = self.pending[..SILERO_HOP].try_into().unwrap();
            self.pending.drain(..SILERO_HOP);
            match self.run_hop(&hop) {
                Ok(p) => {
                    ran = true;
                    if p > max_p {
                        max_p = p;
                    }
                    self.last_prob = p;
                }
                Err(e) => {
                    eprintln!("[silero-vad] inference error ({e}); disabling neural VAD");
                    self.disabled = true;
                    return None;
                }
            }
        }
        if self.disabled {
            None
        } else if ran {
            Some(max_p)
        } else {
            Some(self.last_prob)
        }
    }
}

static ENGINE: OnceLock<Option<Mutex<SileroEngine>>> = OnceLock::new();

pub fn init() -> bool {
    ENGINE
        .get_or_init(|| match SileroEngine::load() {
            Ok(e) => Some(Mutex::new(e)),
            Err(e) => {
                println!("[silero-vad] unavailable ({e}); using RMS VAD");
                None
            }
        })
        .is_some()
}

pub fn reset() {
    if let Some(Some(mtx)) = ENGINE.get() {
        if let Ok(mut g) = mtx.lock() {
            g.reset_state();
        }
    }
}

/// Feed a 16 kHz mono PCM16 chunk and return neural speech probability `p_speech ∈ [0.0, 1.0]`,
/// or `None` if Silero VAD is unavailable/disabled (caller transparently uses RMS VAD).
pub fn process_pcm16(pcm: &[u8]) -> Option<f32> {
    let mtx = ENGINE.get()?.as_ref()?;
    let mut g = mtx.lock().ok()?;
    g.feed_pcm16(pcm)
}

/// Dual-threshold hysteresis profile for Option 3 (Silero VAD v5 + fast streaming Wake-Spotter).
#[derive(Clone, Copy, Debug)]
pub struct SileroProfile {
    pub start_prob: f32,
    pub keep_prob: f32,
    pub min_start_hits: usize,
    pub end_silence_ms: f64,
}

pub fn profile_for(is_wake_mode: bool, music_on: bool, active_silence_cfg: f64) -> SileroProfile {
    match (is_wake_mode, music_on) {
        (true, false) => SileroProfile {
            start_prob: 0.42,
            keep_prob: 0.20,
            min_start_hits: 2,
            end_silence_ms: 320.0,
        },
        (true, true) => SileroProfile {
            start_prob: 0.50,
            keep_prob: 0.24,
            min_start_hits: 2,
            end_silence_ms: 340.0,
        },
        (false, false) => SileroProfile {
            start_prob: 0.34,
            keep_prob: 0.16,
            min_start_hits: 1,
            end_silence_ms: active_silence_cfg.clamp(380.0, 900.0),
        },
        (false, true) => SileroProfile {
            start_prob: 0.40,
            keep_prob: 0.18,
            min_start_hits: 1,
            end_silence_ms: active_silence_cfg.clamp(420.0, 950.0),
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn silero_hysteresis_profiles_stay_ordered() {
        for wake in [true, false] {
            for music in [true, false] {
                let p = profile_for(wake, music, 550.0);
                assert!(p.keep_prob > 0.0 && p.keep_prob < p.start_prob && p.start_prob < 1.0);
                assert!(p.min_start_hits >= 1);
                assert!(p.end_silence_ms >= 300.0);
            }
        }
    }
}
