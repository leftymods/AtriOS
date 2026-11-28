//! Local on-device Russian Neural ASR using GigaAM-v2-CTC INT8 via libonnxruntime.so.
//!
//! Used as a 100% offline local fallback when Silero VAD v5 detects quiet/whispered
//! speech on clean `vqe_0` audio that Kaldi/Vosk (`vosk-model-small-ru`) cannot decode.
//! Zero cloud dependency: runs completely on-device on Amlogic S905X3 (ARM64).

use std::ffi::{CStr, CString};
use std::fs;
use std::os::raw::{c_char, c_int, c_void};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::ptr;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};
use std::time::Instant;

use crate::json::Json;

const ORT_LIB_PATHS: &[&str] = &[
    "/usr/lib/atrios/libonnxruntime.so",
    "/usr/lib/libonnxruntime.so",
    "libonnxruntime.so",
];

const MODEL_DIR: &str = "/var/lib/atrios/models/gigaam-v2-ctc";
const MODEL_FILE: &str = "model.int8.onnx";
const MODEL_ARCHIVE_URL: &str =
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-nemo-ctc-giga-am-v2-russian-2025-04-19.tar.bz2";
const MODEL_ARCHIVE_MEMBER: &str =
    "sherpa-onnx-nemo-ctc-giga-am-v2-russian-2025-04-19/model.int8.onnx";
const MIN_MODEL_BYTES: u64 = 100 * 1024 * 1024; // ~226 MB expected

// Exact OrtApi vtable indices verified via offsetof(OrtApi, ...) against
// board/yandex/station2/src/third_party/onnxruntime_c_api.h (ORT_API_VERSION = 28).
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
const IDX_GET_DIMENSIONS_COUNT: usize = 61;
const IDX_GET_DIMENSIONS: usize = 62;
const IDX_GET_TENSOR_TYPE_AND_SHAPE: usize = 65;
const IDX_CREATE_CPU_MEM_INFO: usize = 69;
const IDX_RELEASE_ENV: usize = 92;
const IDX_RELEASE_STATUS: usize = 93;
const IDX_RELEASE_MEM_INFO: usize = 94;
const IDX_RELEASE_SESSION: usize = 95;
const IDX_RELEASE_VALUE: usize = 96;
const IDX_RELEASE_TENSOR_TYPE_AND_SHAPE_INFO: usize = 99;
const IDX_RELEASE_SESSION_OPTIONS: usize = 100;

const ONNX_TENSOR_FLOAT: c_int = 1;
const ONNX_TENSOR_INT64: c_int = 7;
const ORT_ARENA_ALLOCATOR: c_int = 0;
const ORT_MEM_TYPE_DEFAULT: c_int = 0;
const ORT_LOGGING_ERROR: c_int = 3;
const ORT_ENABLE_ALL: c_int = 99;

const SAMPLE_RATE: f32 = 16000.0;
const FRAME_LEN: usize = 400; // 25 ms @ 16 kHz
const FRAME_SHIFT: usize = 160; // 10 ms @ 16 kHz
const FFT_BINS: usize = FRAME_LEN / 2 + 1; // 201
const NUM_MELS: usize = 64;
const BLANK_ID: usize = 33;

const VOCAB: [char; 33] = [
    ' ', 'а', 'б', 'в', 'г', 'д', 'е', 'ж', 'з', 'и', 'й', 'к', 'л', 'м', 'н', 'о', 'п', 'р', 'с',
    'т', 'у', 'ф', 'х', 'ц', 'ч', 'ш', 'щ', 'ъ', 'ы', 'ь', 'э', 'ю', 'я',
];

#[repr(C)]
struct OrtApiBase {
    get_api: unsafe extern "C" fn(version: u32) -> *const *const c_void,
    get_version_string: unsafe extern "C" fn() -> *const c_char,
}

type OrtGetApiBaseFn = unsafe extern "C" fn() -> *const OrtApiBase;

type FnGetErrorMessage = unsafe extern "C" fn(status: *const c_void) -> *const c_char;
type FnReleaseStatus = unsafe extern "C" fn(status: *mut c_void);
type FnCreateEnv =
    unsafe extern "C" fn(log_level: c_int, logid: *const c_char, out: *mut *mut c_void) -> *mut c_void;
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
type FnCreateCpuMemInfo =
    unsafe extern "C" fn(alloc_type: c_int, mem_type: c_int, out: *mut *mut c_void) -> *mut c_void;
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
type FnGetTensorMutableData =
    unsafe extern "C" fn(val: *mut c_void, out: *mut *mut c_void) -> *mut c_void;
type FnGetTensorTypeAndShape =
    unsafe extern "C" fn(val: *const c_void, out: *mut *mut c_void) -> *mut c_void;
type FnGetDimensionsCount =
    unsafe extern "C" fn(info: *const c_void, out: *mut usize) -> *mut c_void;
type FnGetDimensions =
    unsafe extern "C" fn(info: *const c_void, dim_values: *mut i64, dim_len: usize) -> *mut c_void;
type FnReleaseTensorTypeAndShapeInfo = unsafe extern "C" fn(info: *mut c_void);
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
    get_tensor_type_and_shape: FnGetTensorTypeAndShape,
    get_dimensions_count: FnGetDimensionsCount,
    get_dimensions: FnGetDimensions,
    release_tensor_type_and_shape_info: FnReleaseTensorTypeAndShapeInfo,
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
            get_tensor_type_and_shape: std::mem::transmute(*tbl.add(IDX_GET_TENSOR_TYPE_AND_SHAPE)),
            get_dimensions_count: std::mem::transmute(*tbl.add(IDX_GET_DIMENSIONS_COUNT)),
            get_dimensions: std::mem::transmute(*tbl.add(IDX_GET_DIMENSIONS)),
            release_tensor_type_and_shape_info: std::mem::transmute(
                *tbl.add(IDX_RELEASE_TENSOR_TYPE_AND_SHAPE_INFO),
            ),
            release_value: std::mem::transmute(*tbl.add(IDX_RELEASE_VALUE)),
            run: std::mem::transmute(*tbl.add(IDX_RUN)),
        }
    }

    unsafe fn check(&self, status: *mut c_void) -> Result<(), String> {
        if status.is_null() {
            return Ok(());
        }
        let msg_ptr = (self.get_error_message)(status);
        let msg = if msg_ptr.is_null() {
            "ORT error".to_string()
        } else {
            CStr::from_ptr(msg_ptr).to_string_lossy().into_owned()
        };
        (self.release_status)(status);
        Err(msg)
    }
}

extern "C" {
    fn dlopen(filename: *const c_char, flag: c_int) -> *mut c_void;
    fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
}

const RTLD_NOW: c_int = 2;
const RTLD_GLOBAL: c_int = 0x100;

struct GigaAmSession {
    fns: OrtFns,
    env: *mut c_void,
    sess: *mut c_void,
    mem_info: *mut c_void,
    in_audio_name: CString,
    in_len_name: CString,
    out_logprobs_name: CString,
}

unsafe impl Send for GigaAmSession {}

impl Drop for GigaAmSession {
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

impl GigaAmSession {
    fn load(model_path: &Path) -> Result<Self, String> {
        let mut handle: *mut c_void = ptr::null_mut();
        for &p in ORT_LIB_PATHS {
            let cpath = CString::new(p).map_err(|_| "bad lib path")?;
            let h = unsafe { dlopen(cpath.as_ptr(), RTLD_NOW | RTLD_GLOBAL) };
            if !h.is_null() {
                handle = h;
                break;
            }
        }
        if handle.is_null() {
            return Err("libonnxruntime.so not found".to_string());
        }
        let sym = CString::new("OrtGetApiBase").unwrap();
        let raw = unsafe { dlsym(handle, sym.as_ptr()) };
        if raw.is_null() {
            return Err("OrtGetApiBase symbol missing".to_string());
        }
        let get_base: OrtGetApiBaseFn = unsafe { std::mem::transmute(raw) };
        let base = unsafe { get_base() };
        if base.is_null() {
            return Err("OrtGetApiBase returned null".to_string());
        }

        let mut api_tbl: *const *const c_void = ptr::null();
        for &ver in &[28u32, 24, 20, 17, 16, 14, 10] {
            let t = unsafe { ((*base).get_api)(ver) };
            if !t.is_null() {
                api_tbl = t;
                break;
            }
        }
        if api_tbl.is_null() {
            return Err("OrtApi version negotiation failed".to_string());
        }
        let fns = unsafe { OrtFns::from_table(api_tbl) };

        let mut env: *mut c_void = ptr::null_mut();
        let logid = CString::new("gigaam_asr").unwrap();
        unsafe { fns.check((fns.create_env)(ORT_LOGGING_ERROR, logid.as_ptr(), &mut env))? };

        let mut opts: *mut c_void = ptr::null_mut();
        if let Err(e) = unsafe { fns.check((fns.create_session_options)(&mut opts)) } {
            unsafe { (fns.release_env)(env) };
            return Err(e);
        }
        let _ = unsafe { fns.check((fns.set_opt_level)(opts, ORT_ENABLE_ALL)) };
        let _ = unsafe { fns.check((fns.set_intra_threads)(opts, 3)) };
        let _ = unsafe { fns.check((fns.set_inter_threads)(opts, 1)) };

        let c_model = CString::new(model_path.to_string_lossy().as_bytes())
            .map_err(|_| "bad model path")?;
        let mut sess: *mut c_void = ptr::null_mut();
        let sess_res = unsafe { fns.check((fns.create_session)(env, c_model.as_ptr(), opts, &mut sess)) };
        unsafe { (fns.release_session_options)(opts) };
        if let Err(e) = sess_res {
            unsafe { (fns.release_env)(env) };
            return Err(e);
        }

        let mut mem_info: *mut c_void = ptr::null_mut();
        if let Err(e) = unsafe {
            fns.check((fns.create_cpu_mem_info)(
                ORT_ARENA_ALLOCATOR,
                ORT_MEM_TYPE_DEFAULT,
                &mut mem_info,
            ))
        } {
            unsafe {
                (fns.release_session)(sess);
                (fns.release_env)(env);
            }
            return Err(e);
        }

        Ok(Self {
            fns,
            env,
            sess,
            mem_info,
            in_audio_name: CString::new("features").unwrap(),
            in_len_name: CString::new("feature_lengths").unwrap(),
            out_logprobs_name: CString::new("log_probs").unwrap(),
        })
    }

    fn run_features(&mut self, feats: &mut [f32], num_frames: usize) -> Result<String, String> {
        let audio_shape: [i64; 3] = [1, NUM_MELS as i64, num_frames as i64];
        let mut len_buf: [i64; 1] = [num_frames as i64];
        let len_shape: [i64; 1] = [1];

        let mut val_audio: *mut c_void = ptr::null_mut();
        let mut val_len: *mut c_void = ptr::null_mut();
        unsafe {
            self.fns.check((self.fns.create_tensor_with_data)(
                self.mem_info,
                feats.as_mut_ptr() as *mut c_void,
                feats.len() * std::mem::size_of::<f32>(),
                audio_shape.as_ptr(),
                audio_shape.len(),
                ONNX_TENSOR_FLOAT,
                &mut val_audio,
            ))?;
            if let Err(e) = self.fns.check((self.fns.create_tensor_with_data)(
                self.mem_info,
                len_buf.as_mut_ptr() as *mut c_void,
                len_buf.len() * std::mem::size_of::<i64>(),
                len_shape.as_ptr(),
                len_shape.len(),
                ONNX_TENSOR_INT64,
                &mut val_len,
            )) {
                (self.fns.release_value)(val_audio);
                return Err(e);
            }
        }

        let input_names = [self.in_audio_name.as_ptr(), self.in_len_name.as_ptr()];
        let inputs: [*const c_void; 2] = [val_audio, val_len];
        let output_names = [self.out_logprobs_name.as_ptr()];
        let mut outputs: [*mut c_void; 1] = [ptr::null_mut()];

        let run_res = unsafe {
            self.fns.check((self.fns.run)(
                self.sess,
                ptr::null(),
                input_names.as_ptr(),
                inputs.as_ptr(),
                2,
                output_names.as_ptr(),
                1,
                outputs.as_mut_ptr(),
            ))
        };

        unsafe {
            (self.fns.release_value)(val_audio);
            (self.fns.release_value)(val_len);
        }
        run_res?;

        let out_val = outputs[0];
        if out_val.is_null() {
            return Err("GigaAM returned null output".to_string());
        }

        let decode_res = unsafe {
            let mut info: *mut c_void = ptr::null_mut();
            let res = (|| -> Result<String, String> {
                self.fns.check((self.fns.get_tensor_type_and_shape)(out_val, &mut info))?;
                let mut ndims: usize = 0;
                self.fns.check((self.fns.get_dimensions_count)(info, &mut ndims))?;
                if ndims != 3 {
                    return Err(format!("unexpected logprobs rank {ndims}"));
                }
                let mut dims = [0i64; 3];
                self.fns.check((self.fns.get_dimensions)(info, dims.as_mut_ptr(), 3))?;
                let t_out = dims[1].max(0) as usize;
                let vocab = dims[2].max(0) as usize;
                if t_out == 0 || vocab < VOCAB.len() {
                    return Ok(String::new());
                }
                let mut data_ptr: *mut c_void = ptr::null_mut();
                self.fns.check((self.fns.get_tensor_mutable_data)(out_val, &mut data_ptr))?;
                if data_ptr.is_null() {
                    return Err("null logprobs data".to_string());
                }
                let slice = std::slice::from_raw_parts(data_ptr as *const f32, t_out * vocab);
                Ok(ctc_greedy_decode(slice, t_out, vocab))
            })();
            if !info.is_null() {
                (self.fns.release_tensor_type_and_shape_info)(info);
            }
            (self.fns.release_value)(out_val);
            res
        };
        decode_res
    }
}

struct FbankTables {
    hann: [f32; FRAME_LEN],
    cos_tbl: Vec<f32>, // [FFT_BINS * FRAME_LEN]
    sin_tbl: Vec<f32>, // [FFT_BINS * FRAME_LEN]
    mel_fb: Vec<(usize, Vec<f32>)>, // per mel bin: (start_fft_bin, weights)
}

fn hz_to_mel_kaldi(hz: f32) -> f32 {
    1127.0 * (1.0 + hz / 700.0).ln()
}

fn fbank_tables() -> &'static FbankTables {
    static TABLES: OnceLock<FbankTables> = OnceLock::new();
    TABLES.get_or_init(|| {
        let mut hann = [0.0f32; FRAME_LEN];
        let denom = (FRAME_LEN - 1) as f32;
        for n in 0..FRAME_LEN {
            hann[n] = 0.5 - 0.5 * (2.0 * std::f32::consts::PI * (n as f32) / denom).cos();
        }
        let mut cos_tbl = vec![0.0f32; FFT_BINS * FRAME_LEN];
        let mut sin_tbl = vec![0.0f32; FFT_BINS * FRAME_LEN];
        let two_pi_over_n = 2.0 * std::f32::consts::PI / (FRAME_LEN as f32);
        for k in 0..FFT_BINS {
            let base = k * FRAME_LEN;
            let wk = two_pi_over_n * (k as f32);
            for n in 0..FRAME_LEN {
                let ang = wk * (n as f32);
                cos_tbl[base + n] = ang.cos();
                sin_tbl[base + n] = ang.sin();
            }
        }
        let mel_low = hz_to_mel_kaldi(0.0);
        let mel_high = hz_to_mel_kaldi(SAMPLE_RATE * 0.5);
        let mel_delta = (mel_high - mel_low) / ((NUM_MELS + 1) as f32);
        let bin_hz = SAMPLE_RATE / (FRAME_LEN as f32); // 40 Hz
        let mut mel_fb = Vec::with_capacity(NUM_MELS);
        for b in 0..NUM_MELS {
            let left = mel_low + (b as f32) * mel_delta;
            let center = left + mel_delta;
            let right = center + mel_delta;
            let mut start_k = FFT_BINS;
            let mut weights = Vec::new();
            for k in 0..FFT_BINS {
                let m = hz_to_mel_kaldi((k as f32) * bin_hz);
                let up = (m - left) / (center - left);
                let down = (right - m) / (right - center);
                let w = up.min(down).max(0.0);
                if w > 0.0 {
                    if start_k == FFT_BINS {
                        start_k = k;
                    }
                    weights.push(w);
                } else if start_k != FFT_BINS {
                    break;
                }
            }
            if start_k == FFT_BINS {
                start_k = 0;
            }
            mel_fb.push((start_k, weights));
        }
        FbankTables {
            hann,
            cos_tbl,
            sin_tbl,
            mel_fb,
        }
    })
}

/// Compute 64-bin Kaldi log-mel features laid out as `[1, 64, num_frames]` (`feats[m * T + t]`).
pub fn compute_log_mel_features(pcm: &[u8]) -> (Vec<f32>, usize) {
    let n_samples = pcm.len() / 2;
    let mut samples = Vec::with_capacity(n_samples.max(FRAME_LEN));
    let mut peak = 0.0f32;
    for chunk in pcm.chunks_exact(2) {
        let s = i16::from_le_bytes([chunk[0], chunk[1]]) as f32 / 32768.0;
        let a = s.abs();
        if a > peak {
            peak = a;
        }
        samples.push(s);
    }
    // Gently normalize very quiet whispers into GigaAM's acoustic sweet spot
    // without over-amplifying pure room silence.
    if peak > 0.0025 && peak < 0.28 {
        let gain = (0.40 / peak).min(24.0);
        for s in &mut samples {
            *s = (*s * gain).clamp(-0.98, 0.98);
        }
    }
    if samples.len() < FRAME_LEN {
        samples.resize(FRAME_LEN, 0.0);
    }

    let num_frames = (samples.len() - FRAME_LEN) / FRAME_SHIFT + 1;
    let tbl = fbank_tables();
    let mut feats = vec![0.0f32; NUM_MELS * num_frames];
    let mut windowed = [0.0f32; FRAME_LEN];
    let mut power = [0.0f32; FFT_BINS];

    for t in 0..num_frames {
        let off = t * FRAME_SHIFT;
        let frame = &samples[off..off + FRAME_LEN];
        for n in 0..FRAME_LEN {
            windowed[n] = frame[n] * tbl.hann[n];
        }
        for k in 0..FFT_BINS {
            let base = k * FRAME_LEN;
            let c_row = &tbl.cos_tbl[base..base + FRAME_LEN];
            let s_row = &tbl.sin_tbl[base..base + FRAME_LEN];
            let mut re = 0.0f32;
            let mut im = 0.0f32;
            for n in 0..FRAME_LEN {
                let x = windowed[n];
                re += x * c_row[n];
                im -= x * s_row[n];
            }
            power[k] = re * re + im * im;
        }
        for (m, (start_k, weights)) in tbl.mel_fb.iter().enumerate() {
            let mut energy = 0.0f32;
            for (i, &w) in weights.iter().enumerate() {
                energy += power[*start_k + i] * w;
            }
            feats[m * num_frames + t] = energy.max(1.1920929e-7_f32).ln();
        }
    }
    (feats, num_frames)
}

pub fn ctc_greedy_decode(logprobs: &[f32], t_out: usize, vocab: usize) -> String {
    let mut out = String::new();
    let mut prev: usize = usize::MAX;
    for t in 0..t_out {
        let row = &logprobs[t * vocab..(t + 1) * vocab];
        let mut best_id = 0usize;
        let mut best_val = f32::NEG_INFINITY;
        for (i, &v) in row.iter().enumerate() {
            if v > best_val {
                best_val = v;
                best_id = i;
            }
        }
        if best_id != BLANK_ID && best_id != prev {
            if let Some(&ch) = VOCAB.get(best_id) {
                out.push(ch);
            }
        }
        prev = best_id;
    }
    out.split_whitespace().collect::<Vec<_>>().join(" ")
}

fn model_path() -> PathBuf {
    if let Ok(p) = std::env::var("ATRI_GIGAAM_MODEL") {
        if !p.trim().is_empty() {
            return PathBuf::from(p);
        }
    }
    let usr = PathBuf::from("/usr/share/atrios/models/gigaam-v2-ctc").join(MODEL_FILE);
    if usr.is_file() {
        return usr;
    }
    PathBuf::from(MODEL_DIR).join(MODEL_FILE)
}

pub fn is_model_ready() -> bool {
    fs::metadata(model_path())
        .map(|m| m.is_file() && m.len() >= MIN_MODEL_BYTES)
        .unwrap_or(false)
}

static SESSION: OnceLock<Mutex<Option<GigaAmSession>>> = OnceLock::new();
static DOWNLOADING: AtomicBool = AtomicBool::new(false);
static LAST_INFER_MS: AtomicU64 = AtomicU64::new(0);
static TOTAL_INFER_COUNT: AtomicU64 = AtomicU64::new(0);

fn session_mutex() -> &'static Mutex<Option<GigaAmSession>> {
    SESSION.get_or_init(|| Mutex::new(None))
}

/// One-time background download of the local GigaAM-v2-CTC INT8 ONNX model (~226 MB)
/// into `/var/lib/atrios/models/gigaam-v2-ctc/model.int8.onnx` (persists across OTAs).
/// Once present on disk, inference is 100% local and offline.
pub fn preload_session_bg() {
    if !is_model_ready() {
        return;
    }
    std::thread::spawn(|| {
        let path = model_path();
        if let Ok(mut guard) = session_mutex().lock() {
            if guard.is_none() {
                let t_load = Instant::now();
                match GigaAmSession::load(&path) {
                    Ok(sess) => {
                        println!(
                            "[gigaam-asr] preloaded local GigaAM-v2-CTC INT8 in {} ms",
                            t_load.elapsed().as_millis()
                        );
                        *guard = Some(sess);
                    }
                    Err(e) => eprintln!("[gigaam-asr] preload error: {e}"),
                }
            }
        }
    });
}

pub fn ensure_model_downloaded_bg() {
    if is_model_ready() {
        preload_session_bg();
        return;
    }
    if DOWNLOADING.swap(true, Ordering::SeqCst) {
        return;
    }
    std::thread::spawn(|| {
        let dir = Path::new(MODEL_DIR);
        let _ = fs::create_dir_all(dir);
        let final_path = dir.join(MODEL_FILE);
        let part_model = dir.join("model.int8.onnx.part");
        let part_arc = dir.join("gigaam.tar.bz2.part");
        println!("[gigaam-asr] downloading local Russian neural whisper model (~226 MB) once...");
        let dl_ok = Command::new("wget")
            .args(["-q", "-O"])
            .arg(&part_arc)
            .arg(MODEL_ARCHIVE_URL)
            .status()
            .map(|s| s.success())
            .unwrap_or(false);
        if dl_ok {
            let extract_cmd = format!(
                "tar -xjf {} -C {} {} && mv -f {}/{} {} && rm -rf {}/sherpa-onnx-nemo-ctc-giga-am-v2-russian-2025-04-19",
                part_arc.display(),
                dir.display(),
                MODEL_ARCHIVE_MEMBER,
                dir.display(),
                MODEL_ARCHIVE_MEMBER,
                final_path.display(),
                dir.display()
            );
            let ext_ok = Command::new("sh")
                .args(["-c", &extract_cmd])
                .status()
                .map(|s| s.success())
                .unwrap_or(false);
            let _ = fs::remove_file(&part_arc);
            if ext_ok && is_model_ready() {
                println!(
                    "[gigaam-asr] local model ready at {} (100% offline)",
                    final_path.display()
                );
                preload_session_bg();
            } else {
                let _ = fs::remove_file(&part_model);
                eprintln!("[gigaam-asr] extract failed; will retry on next restart");
            }
        } else {
            let _ = fs::remove_file(&part_arc);
            eprintln!("[gigaam-asr] download failed; will retry on next restart");
        }
        DOWNLOADING.store(false, Ordering::SeqCst);
    });
}

/// Transcribe short 16 kHz mono PCM16 audio locally using GigaAM-v2-CTC INT8 ONNX.
/// Returns `Ok(String)` (may be empty if silence/noise), or `Err` if model not ready.
pub fn transcribe_pcm16(pcm: &[u8]) -> Result<String, String> {
    if pcm.len() < FRAME_LEN * 2 {
        return Ok(String::new());
    }
    let path = model_path();
    if !is_model_ready() {
        ensure_model_downloaded_bg();
        return Err("gigaam model not ready yet".to_string());
    }
    let t0 = Instant::now();
    let (mut feats, num_frames) = compute_log_mel_features(pcm);
    let mut guard = session_mutex().lock().map_err(|_| "gigaam lock poisoned")?;
    if guard.is_none() {
        let t_load = Instant::now();
        let sess = GigaAmSession::load(&path)?;
        println!(
            "[gigaam-asr] loaded local GigaAM-v2-CTC INT8 in {} ms",
            t_load.elapsed().as_millis()
        );
        *guard = Some(sess);
    }
    let text = guard.as_mut().unwrap().run_features(&mut feats, num_frames)?;
    let elapsed_ms = t0.elapsed().as_millis() as u64;
    LAST_INFER_MS.store(elapsed_ms, Ordering::Relaxed);
    TOTAL_INFER_COUNT.fetch_add(1, Ordering::Relaxed);
    if !text.is_empty() {
        println!("[gigaam-asr] local decoded in {} ms: {:?}", elapsed_ms, text);
    }
    Ok(text)
}

#[allow(dead_code)]
pub fn status_json() -> Json {
    Json::Obj([
        ("ready".to_string(), Json::Bool(is_model_ready())),
        (
            "downloading".to_string(),
            Json::Bool(DOWNLOADING.load(Ordering::Relaxed)),
        ),
        (
            "last_infer_ms".to_string(),
            Json::Num(LAST_INFER_MS.load(Ordering::Relaxed) as f64),
        ),
        (
            "total_calls".to_string(),
            Json::Num(TOTAL_INFER_COUNT.load(Ordering::Relaxed) as f64),
        ),
    ].into_iter().collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ctc_decode_collapses_repeats_and_blanks() {
        // "тише" -> т(19), и(9), ш(25), е(6)
        let seq = [33, 19, 19, 33, 9, 25, 25, 33, 6, 33];
        let vocab = 34;
        let mut logprobs = vec![-10.0f32; seq.len() * vocab];
        for (t, &id) in seq.iter().enumerate() {
            logprobs[t * vocab + id] = 0.0;
        }
        assert_eq!(ctc_greedy_decode(&logprobs, seq.len(), vocab), "тише");
    }

    #[test]
    fn log_mel_features_shape_and_finite() {
        // 0.5 sec of quiet 440 Hz sine wave (whisper amplitude)
        let mut pcm = Vec::with_capacity(8000 * 2);
        for i in 0..8000 {
            let v = (2.0 * std::f32::consts::PI * 440.0 * (i as f32) / 16000.0).sin() * 400.0;
            pcm.extend_from_slice(&(v as i16).to_le_bytes());
        }
        let (feats, frames) = compute_log_mel_features(&pcm);
        assert_eq!(frames, (8000 - 400) / 160 + 1);
        assert_eq!(feats.len(), NUM_MELS * frames);
        assert!(feats.iter().all(|x| x.is_finite()));
    }
}
