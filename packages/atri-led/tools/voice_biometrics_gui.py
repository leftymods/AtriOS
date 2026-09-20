#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Yandex Station Max - Система Распознавания Владельца "Свой / Чужой"
Полный алгоритмический стек биометрии с расширенной калибровкой:
- Предобработка звука Kaldi 16-bit PCM (features_config.json)
- Покадровый трекер тона связок (Voicing / F0 Pitch Harmonics 60-650 Гц)
- Нейросетевая биометрия ECAPA-TDNN (body.tflite + head.tflite, 512-d embeddings)
- Поддержка пения (Adaptive Singing Threshold & Singing Enrollment)
- Акустический детектор кашля владельца (Transient Friction & Cough Filter)
- Контрастный арбитраж сигналов в реальном времени
"""

import os
import sys
import time
import queue
import json
import threading
import wave
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import numpy as np
import sounddevice as sd
from ai_edge_litert.interpreter import Interpreter

# --- Аудио параметры по спецификации Quasar (Kaldi Fbank) ---
SAMPLE_RATE = 16000
FRAME_LEN = 320    # 20 ms
FRAME_SHIFT = 160  # 10 ms
CHUNK_FRAMES = 40  # 400 ms
DEFAULT_OWNER_THRESHOLD = 0.78  # Порог допуска для обычной речи
SINGING_OWNER_THRESHOLD = 0.65  # Адаптивный порог допуска для пения

# 80-полосный мел-фильтрбанк по Kaldi спецификации
def hz_to_mel(hz):
    return 1127.0 * np.log(1.0 + hz / 700.0)

def mel_to_hz(mel):
    return 700.0 * (np.exp(mel / 1127.0) - 1.0)

def build_mel_filterbank(sr=16000, n_fft=512, n_mels=80):
    mel_min = hz_to_mel(0.0)
    mel_max = hz_to_mel(8000.0)
    mel_points = np.linspace(mel_min, mel_max, n_mels + 2)
    hz_points = mel_to_hz(mel_points)
    bins = np.floor((n_fft + 1) * hz_points / sr).astype(int)

    fb = np.zeros((n_mels, n_fft // 2 + 1), dtype=np.float32)
    for m in range(1, n_mels + 1):
        f_m_minus = bins[m - 1]
        f_m = bins[m]
        f_m_plus = bins[m + 1]
        for k in range(f_m_minus, f_m):
            if f_m != f_m_minus:
                fb[m - 1, k] = (k - f_m_minus) / (f_m - f_m_minus)
        for k in range(f_m, f_m_plus):
            if f_m_plus != f_m:
                fb[m - 1, k] = (f_m_plus - k) / (f_m_plus - f_m)
    return fb

MEL_FB = build_mel_filterbank()
HAMMING_WIN = np.hamming(FRAME_LEN).astype(np.float32)

def extract_fbank(audio_float):
    """
    Извлечение мел-фильтрбанков по стандарту Kaldi (features_config.json):
    Нейросеть обучена на 16-битном PCM масштабе [-32768, 32767].
    """
    if len(audio_float) < FRAME_LEN:
        return np.zeros((0, 80), dtype=np.float32)

    sig = audio_float * 32768.0
    sig = sig - np.mean(sig) # remove-dc-offset
    sig = np.append(sig[0], sig[1:] - 0.97 * sig[:-1]) # preemphasis 0.97

    n_frames = (len(sig) - FRAME_LEN) // FRAME_SHIFT + 1
    feats = []
    for i in range(n_frames):
        st = i * FRAME_SHIFT
        frame = sig[st:st+FRAME_LEN] * HAMMING_WIN
        spec = np.abs(np.fft.rfft(frame, 512)) ** 2
        mel_e = np.dot(MEL_FB, spec)
        feats.append(np.log(np.maximum(mel_e, 1e-10)))
    return np.array(feats, dtype=np.float32)

def trim_active_audio(audio_float, sr=16000, threshold=0.005, pad_ms=120):
    """
    Выделяет активный звуковой сегмент, обрезая тишину до и после звука.
    Гарантирует минимальную длину 0.3s (4800 семплов) для нейросети ECAPA-TDNN.
    """
    if len(audio_float) < 320:
        return audio_float

    pad_samples = int(pad_ms * sr / 1000)
    frame_len = 320
    n_frames = len(audio_float) // frame_len
    active = np.zeros(len(audio_float), dtype=bool)

    for i in range(n_frames):
        st = i * frame_len
        f = audio_float[st:st+frame_len]
        if np.sqrt(np.mean(f**2)) > threshold:
            active[st:st+frame_len] = True

    if not np.any(active):
        return audio_float

    idx = np.where(active)[0]
    start = max(0, idx[0] - pad_samples)
    end = min(len(audio_float), idx[-1] + pad_samples)
    trimmed = audio_float[start:end]

    # Если звук короче 0.3 сек (например резкий кашель), дополняем нулями для TDNN
    min_samples = int(0.3 * sr)
    if len(trimmed) < min_samples:
        pad_needed = min_samples - len(trimmed)
        trimmed = np.pad(trimmed, (0, pad_needed), mode='constant')

    return trimmed

def check_cough_acoustic(audio_float, sr=16000):
    """
    Физико-акустический детектор кашля и покашливаний:
    - Резкий экспульсивный фронт (Crest factor > 2.8)
    - Длительность активного всплеска от 80 до 650 мс
    - Спектр горлового трения (Centroid > 650 Гц, ZCR > 0.04)
    - Отсутствие непрерывных гармоник голосовых связок (max_consec_voiced < 5)
    """
    if len(audio_float) < 320:
        return False, 0.0, 0.0

    segments_to_check = [audio_float]
    if len(audio_float) > int(0.5 * sr):
        segments_to_check.append(audio_float[-int(0.5 * sr):])

    for seg in segments_to_check:
        rms = float(np.sqrt(np.mean(seg**2)))
        if rms < 0.005:
            continue

        peak = float(np.max(np.abs(seg)))
        crest = peak / rms if rms > 0 else 0.0

        active_idx = np.where(np.abs(seg) > max(0.008, 0.15 * peak))[0]
        if len(active_idx) == 0:
            continue
        burst_dur = (active_idx[-1] - active_idx[0]) / sr

        spec = np.abs(np.fft.rfft(seg))
        freqs = np.fft.rfftfreq(len(seg), 1.0 / sr)
        sum_s = np.sum(spec)
        centroid = float(np.sum(freqs * spec) / sum_s) if sum_s > 0 else 0.0
        zcr = float(np.mean(np.abs(np.diff(np.sign(seg))))) / 2.0

        frame_len = 480
        frame_shift = 160
        min_lag = int(sr / 650)
        max_lag = int(sr / 60)
        consec = 0
        max_consec = 0
        n_voiced = 0
        n_frames = (len(seg) - frame_len) // frame_shift + 1

        for i in range(n_frames):
            st = i * frame_shift
            f = seg[st:st+frame_len]
            r0 = np.sum(f**2)
            if r0 < 5e-5:
                consec = 0
                continue
            corr = np.correlate(f, f, mode='full')[frame_len - 1:]
            search = corr[min_lag:min(max_lag, len(corr))]
            if len(search) == 0:
                consec = 0
                continue
            idx = np.argmax(search) + min_lag
            val = corr[idx] / r0
            if val > 0.33:
                consec += 1
                n_voiced += 1
                max_consec = max(max_consec, consec)
            else:
                consec = 0

        if crest >= 2.8 and 0.08 <= burst_dur <= 0.65 and centroid >= 650.0 and max_consec < 5 and n_voiced < 7:
            return True, crest, centroid

    return False, 0.0, 0.0

# Покадровая проверка на наличие человеческого голоса, пения и кашля
def check_human_voice(audio_float, sr=16000, frame_len=480, frame_shift=160):
    rms = float(np.sqrt(np.mean(audio_float**2)))
    if rms < 0.003:
        return False, 0.0, 0.0, "Тишина", False, False

    peak = float(np.max(np.abs(audio_float)))
    crest_factor = peak / rms if rms > 0 else 0.0

    voiced_pitches = []
    min_lag = int(sr / 650) # до 650 Гц (сопрано / высокое пение / фальцет)
    max_lag = int(sr / 60)  # от 60 Гц (глубокий бас)

    consec = 0
    max_consec = 0
    n_frames = (len(audio_float) - frame_len) // frame_shift + 1

    for i in range(n_frames):
        st = i * frame_shift
        frame = audio_float[st:st+frame_len]
        r0 = np.sum(frame**2)
        if r0 < 5e-5:
            consec = 0
            continue
        corr = np.correlate(frame, frame, mode='full')[frame_len - 1:]
        search = corr[min_lag:min(max_lag, len(corr))]
        if len(search) == 0:
            consec = 0
            continue
        peak_idx = np.argmax(search) + min_lag
        peak_val = corr[peak_idx] / r0
        if peak_val > 0.30:
            voiced_pitches.append(float(sr / peak_idx))
            consec += 1
            max_consec = max(max_consec, consec)
        else:
            consec = 0

    spec = np.abs(np.fft.rfft(audio_float))
    freqs = np.fft.rfftfreq(len(audio_float), 1.0 / sr)
    sum_spec = np.sum(spec)
    centroid = float(np.sum(freqs * spec) / sum_spec) if sum_spec > 0 else 0.0

    # Проверка кашля через специализированный акустический анализатор
    is_cough_ac, _, _ = check_cough_acoustic(audio_float, sr)

    is_voice = (len(voiced_pitches) >= 5 and max_consec >= 3)
    if not is_voice:
        return False, 0.0, round(centroid, 0), "Шум / Помеха", False, is_cough_ac

    pitch = float(np.median(voiced_pitches))
    pitch_std = float(np.std(voiced_pitches)) if len(voiced_pitches) > 1 else 0.0
    voicing_ratio = len(voiced_pitches) / max(1, n_frames)

    # Пение: устойчивые протяжные гласные (max_consec >= 16 фреймов = 160 мс)
    # ЛИБО стабильный тон мелодии (pitch_std < 12 Гц при плотном voicing) ЛИБО высокий регистр (> 220 Гц)
    is_singing = (max_consec >= 16 or (voicing_ratio >= 0.45 and pitch_std < 12.0) or pitch > 220.0)

    if pitch < 115:
        timbre = "Бас (низкий мужской)"
    elif pitch < 145:
        timbre = "Баритон (мужской средний)"
    elif pitch < 185:
        timbre = "Тенор (мужской высокий)"
    elif pitch < 225:
        timbre = "Альт (женский низкий)"
    elif pitch < 290:
        timbre = "Меццо-сопрано / Женский"
    else:
        timbre = "Сопрано / Высокий певческий"

    return True, round(pitch, 1), round(centroid, 0), timbre, is_singing, False

# Ядро биометрии
class BiometricsEngine:
    def __init__(self, model_dir=None):
        if model_dir and os.path.exists(os.path.join(model_dir, 'body.tflite')):
            base = model_dir
        else:
            candidates = [
                os.path.join(os.path.dirname(__file__), 'models'),
                os.path.join(os.path.dirname(__file__), 'tflite-online-ecapa-tdnn-frontend'),
                os.path.join(os.path.dirname(__file__), '../../lib/firmware/models'),
                'extracted_fs/vendor/quasar/bio_lingware/tflite-online-ecapa-tdnn-frontend',
                '../extracted_fs/vendor/quasar/bio_lingware/tflite-online-ecapa-tdnn-frontend',
                '/vendor/quasar/bio_lingware/tflite-online-ecapa-tdnn-frontend',
                'tools/models'
            ]
            base = None
            for c in candidates:
                if os.path.exists(os.path.join(c, 'body.tflite')) and os.path.exists(os.path.join(c, 'head.tflite')):
                    base = c
                    break
            if not base:
                base = 'extracted_fs/vendor/quasar/bio_lingware/tflite-online-ecapa-tdnn-frontend'

        self.body = Interpreter(model_path=os.path.join(base, 'body.tflite'))
        self.body.allocate_tensors()
        self.head = Interpreter(model_path=os.path.join(base, 'head.tflite'))
        self.head.allocate_tensors()

        self.owner_profile = None
        self.profile_file = os.path.join(os.path.dirname(__file__), 'owner_profile.json')
        if not os.path.exists(self.profile_file) and os.path.exists('tools/owner_profile.json'):
            self.profile_file = 'tools/owner_profile.json'
        self.load_profile()

    def get_embedding(self, audio_float):
        fbank = extract_fbank(audio_float)
        if len(fbank) < 15:
            return None

        if len(fbank) < CHUNK_FRAMES:
            padded = np.zeros((1, CHUNK_FRAMES, 80), dtype=np.float32)
            padded[0, :len(fbank), :] = fbank
            chunks = [padded]
        else:
            n_chunks = len(fbank) // CHUNK_FRAMES
            chunks = [fbank[i*CHUNK_FRAMES:(i+1)*CHUNK_FRAMES][np.newaxis, ...] for i in range(n_chunks)]

        inp_idx = self.body.get_input_details()[0]['index']
        out_idx = self.body.get_output_details()[0]['index']
        body_outputs = []
        for c in chunks:
            self.body.set_tensor(inp_idx, c)
            self.body.invoke()
            body_outputs.append(self.body.get_tensor(out_idx))

        all_body = np.concatenate(body_outputs, axis=1)

        head_in = self.head.get_input_details()[0]['index']
        head_out = self.head.get_output_details()[0]['index']
        self.head.resize_tensor_input(head_in, all_body.shape)
        self.head.allocate_tensors()
        self.head.set_tensor(head_in, all_body)
        self.head.invoke()
        emb = self.head.get_tensor(head_out).flatten()
        norm = np.linalg.norm(emb)
        return (emb / norm) if norm > 0 else emb

    def save_profile(self):
        if not self.owner_profile:
            return
        data = {
            "name": self.owner_profile["name"],
            "passport": self.owner_profile["passport"],
            "threshold": self.owner_profile["threshold"],
            "centroid": self.owner_profile["centroid"].tolist(),
            "sing_emb": self.owner_profile["sing_emb"].tolist() if self.owner_profile.get("sing_emb") is not None else None,
            "cough_emb": self.owner_profile["cough_emb"].tolist() if self.owner_profile.get("cough_emb") is not None else None,
            "noise_floor": float(self.owner_profile.get("noise_floor", 0.003))
        }
        with open(self.profile_file, 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

    def load_profile(self):
        if os.path.exists(self.profile_file):
            try:
                with open(self.profile_file, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                    if data["passport"].get("pitch", 0.0) == 0.0 and data["name"] != "Алиса (Яндекс)":
                        return
                    self.owner_profile = {
                        "name": data["name"],
                        "passport": data["passport"],
                        "threshold": data.get("threshold", DEFAULT_OWNER_THRESHOLD),
                        "centroid": np.array(data["centroid"], dtype=np.float32),
                        "sing_emb": np.array(data["sing_emb"], dtype=np.float32) if data.get("sing_emb") else None,
                        "cough_emb": np.array(data["cough_emb"], dtype=np.float32) if data.get("cough_emb") else None,
                        "noise_floor": float(data.get("noise_floor", 0.003))
                    }
            except Exception as e:
                print("Error loading owner profile:", e)

# Главное графическое приложение
class OwnerGuardApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Yandex Station Max — Защита Владельца 'Свой / Чужой'")
        self.root.geometry("1080x800")
        self.root.minsize(940, 700)
        self.root.configure(bg="#0c0e17")

        self.engine = BiometricsEngine()

        self.is_monitoring = False
        self.stream = None
        self.audio_q = queue.Queue()
        self.audio_buf = np.zeros(0, dtype=np.float32)
        self.last_speech_time = 0.0

        self.is_calibrating = False

        self._build_styles()
        self._build_ui()
        self._update_owner_ui()

        self.root.after(40, self._audio_loop)

    def _build_styles(self):
        self.style = ttk.Style()
        self.style.theme_use('clam')
        
        self.style.configure("TFrame", background="#0c0e17")
        self.style.configure("Card.TFrame", background="#151928", relief="flat")
        self.style.configure("Inner.TFrame", background="#1d2237", relief="flat")
        
        self.style.configure("TLabel", background="#0c0e17", foreground="#c0ccde", font=("Segoe UI", 10))
        self.style.configure("Card.TLabel", background="#151928", foreground="#c0ccde", font=("Segoe UI", 10))
        
        self.style.configure("TNotebook", background="#0c0e17", borderwidth=0)
        self.style.configure("TNotebook.Tab", background="#1a1e30", foreground="#8a99ad",
                             font=("Segoe UI", 11, "bold"), padding=[20, 10])
        self.style.map("TNotebook.Tab",
                       background=[("selected", "#00aaff"), ("active", "#252c46")],
                       foreground=[("selected", "#000000"), ("active", "#ffffff")])

        # Buttons
        self.style.configure("FullCalib.TButton", font=("Segoe UI", 12, "bold"), background="#00aaff", foreground="#000000", padding=9)
        self.style.map("FullCalib.TButton", background=[("active", "#33bbff")])

        self.style.configure("QuickCalib.TButton", font=("Segoe UI", 10, "bold"), background="#2b3350", foreground="#00e5ff", padding=8)
        self.style.map("QuickCalib.TButton", background=[("active", "#3a4468")])

        self.style.configure("Green.TButton", font=("Segoe UI", 12, "bold"), background="#00cc66", foreground="#000000", padding=8)
        self.style.map("Green.TButton", background=[("active", "#33ff88")])

        self.style.configure("Red.TButton", font=("Segoe UI", 12, "bold"), background="#ff3355", foreground="#ffffff", padding=8)
        self.style.map("Red.TButton", background=[("active", "#ff5577")])

        self.style.configure("Dark.TButton", font=("Segoe UI", 10), background="#252a42", foreground="#c0ccde", padding=6)
        self.style.map("Dark.TButton", background=[("active", "#363d5e")])

    def _build_ui(self):
        # 1. TOP HEADER
        top_header = tk.Frame(self.root, bg="#0c0e17")
        top_header.pack(fill="x", padx=20, pady=(12, 6))

        h_left = tk.Frame(top_header, bg="#0c0e17")
        h_left.pack(side="left")

        tk.Label(h_left, text="🛡 СИСТЕМА ЗАЩИТЫ ВЛАДЕЛЬЦА • СВОЙ / ЧУЖОЙ", bg="#0c0e17", fg="#00e5ff", font=("Segoe UI", 15, "bold")).pack(anchor="w")
        tk.Label(h_left, text="Голосовая биометрия ECAPA-TDNN с поддержкой пения и фильтрацией кашля", bg="#0c0e17", fg="#7a889b", font=("Segoe UI", 9)).pack(anchor="w")

        self.lbl_global_status = tk.Label(top_header, text="  СИСТЕМА ГОТОВА  ", bg="#152636", fg="#00e5ff", font=("Segoe UI", 10, "bold"), padx=10, pady=5)
        self.lbl_global_status.pack(side="right")

        # 2. PROMINENT MICROPHONE BAR
        mic_bar = tk.Frame(self.root, bg="#151928", highlightthickness=1, highlightbackground="#252a42")
        mic_bar.pack(fill="x", padx=20, pady=(4, 10))
        mb_inner = tk.Frame(mic_bar, bg="#151928", padx=14, pady=10)
        mb_inner.pack(fill="x")

        tk.Label(mb_inner, text="🎙 МИКРОФОН (ДЛЯ КАЛИБРОВКИ И ЭФИРА):", bg="#151928", fg="#00e5ff", font=("Segoe UI", 10, "bold")).pack(side="left", padx=(0, 10))
        
        self.combo_devices = ttk.Combobox(mb_inner, state="readonly", width=42, font=("Segoe UI", 10))
        self.combo_devices.pack(side="left", fill="x", expand=True, padx=(0, 10))
        self._populate_audio_devices()

        btn_refresh_mic = ttk.Button(mb_inner, text="🔄 Обновить", style="Dark.TButton", command=self._populate_audio_devices)
        btn_refresh_mic.pack(side="left", padx=(0, 12))

        tk.Label(mb_inner, text="Сигнал:", bg="#151928", fg="#7a889b", font=("Segoe UI", 9)).pack(side="left", padx=(0, 6))
        self.vu_meter = tk.Canvas(mb_inner, width=120, height=18, bg="#0d0e17", highlightthickness=1, highlightbackground="#252a42")
        self.vu_meter.pack(side="left")

        # 3. NOTEBOOK TABS
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill="both", expand=True, padx=20, pady=(0, 12))

        # --- TAB 1: КАЛИБРОВКА ГОЛОСА ВЛАДЕЛЬЦА ---
        self.tab_calib = ttk.Frame(self.notebook, style="Card.TFrame")
        self.notebook.add(self.tab_calib, text="   🎙 1. КАЛИБРОВКА ГОЛОСА ВЛАДЕЛЬЦА   ")
        self._build_tab_calib()

        # --- TAB 2: РАСПОЗНАВАНИЕ "СВОЙ / ЧУЖОЙ" В ЭФИРЕ ---
        self.tab_guard = ttk.Frame(self.notebook, style="Card.TFrame")
        self.notebook.add(self.tab_guard, text="   🛡 2. РАСПОЗНАВАНИЕ ЭФИРА (СВОЙ / ЧУЖОЙ)   ")
        self._build_tab_guard()

        # --- TAB 3: ТЕСТИРОВАНИЕ АУДИОФАЙЛОВ ---
        self.tab_files = ttk.Frame(self.notebook, style="Card.TFrame")
        self.notebook.add(self.tab_files, text="   📁 3. ПРОВЕРКА WAV ФАЙЛА   ")
        self._build_tab_files()

    # --- TAB 1: КАЛИБРОВКА ---
    def _build_tab_calib(self):
        c_scroll = tk.Frame(self.tab_calib, bg="#151928", padx=20, pady=16)
        c_scroll.pack(fill="both", expand=True)

        top_box = tk.Frame(c_scroll, bg="#1d2237", padx=16, pady=14, highlightthickness=1, highlightbackground="#2d3554")
        top_box.pack(fill="x", pady=(0, 12))

        row1 = tk.Frame(top_box, bg="#1d2237")
        row1.pack(fill="x", pady=(0, 10))

        tk.Label(row1, text="Имя владельца:", bg="#1d2237", fg="#c0ccde", font=("Segoe UI", 11, "bold")).pack(side="left", padx=(0, 12))
        self.ent_owner_name = ttk.Entry(row1, font=("Segoe UI", 11), width=30)
        self.ent_owner_name.insert(0, "Главный Пользователь")
        self.ent_owner_name.pack(side="left", fill="x", expand=True)

        btn_launch_row = tk.Frame(top_box, bg="#1d2237")
        btn_launch_row.pack(fill="x")

        self.btn_full_calib = ttk.Button(
            btn_launch_row, text="🚀 ПОЛНАЯ КАЛИБРОВКА (РЕЧЬ + ПЕНИЕ + КАШЕЛЬ + ШУМ)",
            style="FullCalib.TButton", command=lambda: self.start_guided_calibration(full_mode=True)
        )
        self.btn_full_calib.pack(side="left", fill="x", expand=True, padx=(0, 8))

        self.btn_quick_calib = ttk.Button(
            btn_launch_row, text="⚡ БЫСТРАЯ (3 ФРАЗЫ)",
            style="QuickCalib.TButton", command=lambda: self.start_guided_calibration(full_mode=False)
        )
        self.btn_quick_calib.pack(side="right")

        btn_single_row = tk.Frame(top_box, bg="#1d2237")
        btn_single_row.pack(fill="x", pady=(8, 0))

        self.btn_sing_calib = ttk.Button(
            btn_single_row, text="🎤 ЗАПИСАТЬ ТОЛЬКО НАПЕВ / ПЕНИЕ",
            style="QuickCalib.TButton", command=self.calibrate_singing_only
        )
        self.btn_sing_calib.pack(side="left", fill="x", expand=True, padx=(0, 8))

        self.btn_cough_calib = ttk.Button(
            btn_single_row, text="🤧 ЗАПИСАТЬ ТОЛЬКО КАШЕЛЬ",
            style="QuickCalib.TButton", command=self.calibrate_cough_only
        )
        self.btn_cough_calib.pack(side="right", fill="x", expand=True)

        # Интерактивный суфлер
        self.wizard_box = tk.Frame(c_scroll, bg="#0d0e17", padx=20, pady=16, highlightthickness=2, highlightbackground="#2d3554")
        self.wizard_box.pack(fill="x", pady=(0, 12))

        self.lbl_wizard_stage = tk.Label(self.wizard_box, text="МАСТЕР КАЛИБРОВКИ ГОТОВ", bg="#0d0e17", fg="#ffea79", font=("Segoe UI", 12, "bold"))
        self.lbl_wizard_stage.pack(pady=(0, 6))

        self.lbl_wizard_phrase = tk.Label(
            self.wizard_box,
            text="Выберите режим калибровки выше. В полной калибровке система зафиксирует вашу речь, голос при пении, фоновый шум и покашливание, чтобы безошибочно отличать вас от посторонних.",
            bg="#0d0e17", fg="#8a99ad", font=("Segoe UI", 12), wraplength=880, justify="center"
        )
        self.lbl_wizard_phrase.pack(pady=(0, 8))

        self.lbl_wizard_countdown = tk.Label(self.wizard_box, text="Ожидание старта", bg="#0d0e17", fg="#00e5ff", font=("Segoe UI", 16, "bold"))
        self.lbl_wizard_countdown.pack()

        # Расширенный паспорт владельца
        pass_card = tk.Frame(c_scroll, bg="#1d2237", padx=16, pady=12, highlightthickness=1, highlightbackground="#2d3554")
        pass_card.pack(fill="x", pady=(0, 12))

        tk.Label(pass_card, text="РАСШИРЕННЫЙ АКУСТИЧЕСКИЙ ПАСПОРТ ВЛАДЕЛЬЦА", bg="#1d2237", fg="#00e5ff", font=("Segoe UI", 11, "bold")).pack(anchor="w", pady=(0, 6))

        p_grid = tk.Frame(pass_card, bg="#1d2237")
        p_grid.pack(fill="x")

        self.lbl_p_name = tk.Label(p_grid, text="• Владелец: НЕ ЗАРЕГИСТРИРОВАН", bg="#1d2237", fg="#ff5577", font=("Segoe UI", 10, "bold"))
        self.lbl_p_name.grid(row=0, column=0, sticky="w", padx=(0, 20), pady=2)

        self.lbl_p_pitch = tk.Label(p_grid, text="• Основной тон (F0): -- Гц", bg="#1d2237", fg="#c0ccde", font=("Segoe UI", 10))
        self.lbl_p_pitch.grid(row=0, column=1, sticky="w", padx=(0, 20), pady=2)

        self.lbl_p_timbre = tk.Label(p_grid, text="• Тембр: --", bg="#1d2237", fg="#c0ccde", font=("Segoe UI", 10))
        self.lbl_p_timbre.grid(row=1, column=0, sticky="w", padx=(0, 20), pady=2)

        self.lbl_p_centroid = tk.Label(p_grid, text="• Спектральная яркость: -- Гц", bg="#1d2237", fg="#c0ccde", font=("Segoe UI", 10))
        self.lbl_p_centroid.grid(row=1, column=1, sticky="w", padx=(0, 20), pady=2)

        self.lbl_p_singing = tk.Label(p_grid, text="• Профиль пения: НЕ ЗАДАН", bg="#1d2237", fg="#8a99ad", font=("Segoe UI", 10))
        self.lbl_p_singing.grid(row=2, column=0, sticky="w", padx=(0, 20), pady=2)

        self.lbl_p_cough_filter = tk.Label(p_grid, text="• Фильтр кашля: НЕ ЗАДАН", bg="#1d2237", fg="#8a99ad", font=("Segoe UI", 10))
        self.lbl_p_cough_filter.grid(row=2, column=1, sticky="w", padx=(0, 20), pady=2)

        self.lbl_p_noise_floor = tk.Label(p_grid, text="• Шум комнаты: --", bg="#1d2237", fg="#c0ccde", font=("Segoe UI", 10))
        self.lbl_p_noise_floor.grid(row=3, column=0, sticky="w", padx=(0, 20), pady=2)

        self.lbl_p_thresh = tk.Label(p_grid, text=f"• Порог допуска: {DEFAULT_OWNER_THRESHOLD*100:.1f}% (Пение: {SINGING_OWNER_THRESHOLD*100:.1f}%)", bg="#1d2237", fg="#00ff88", font=("Segoe UI", 10, "bold"))
        self.lbl_p_thresh.grid(row=3, column=1, sticky="w", padx=(0, 20), pady=2)

        btn_row = tk.Frame(c_scroll, bg="#151928")
        btn_row.pack(fill="x")

        self.btn_go_to_guard = ttk.Button(btn_row, text="▶ ПЕРЕЙТИ К РАСПОЗНАВАНИЮ В ЭФИРЕ", style="Green.TButton", command=self._switch_to_guard_and_start)
        self.btn_go_to_guard.pack(side="left", padx=(0, 10))

        ttk.Button(btn_row, text="Задать эталон: 'Алиса' (из прошивки)", style="Dark.TButton", command=self.set_alice_as_owner).pack(side="left", padx=(0, 8))
        ttk.Button(btn_row, text="Сбросить профиль", style="Dark.TButton", command=self.reset_owner_profile).pack(side="left")

    # --- TAB 2: РАСПОЗНАВАНИЕ ЭФИРА ---
    def _build_tab_guard(self):
        g_box = tk.Frame(self.tab_guard, bg="#151928", padx=20, pady=16)
        g_box.pack(fill="both", expand=True)

        ctrl_row = tk.Frame(g_box, bg="#151928")
        ctrl_row.pack(fill="x", pady=(0, 12))

        self.btn_monitor = ttk.Button(ctrl_row, text="▶ ЗАПУСТИТЬ МОНИТОРИНГ ЭФИРА", style="Green.TButton", command=self.toggle_monitoring)
        self.btn_monitor.pack(side="left", padx=(0, 15))

        self.lbl_monitor_hint = tk.Label(ctrl_row, text="Говорите команды или пойте в микрофон", bg="#151928", fg="#7a889b", font=("Segoe UI", 10))
        self.lbl_monitor_hint.pack(side="left")

        # ГЛАВНЫЙ ЭКРАН ВЕРДИКТА
        self.card_verdict_banner = tk.Label(
            g_box, text="МОНИТОРИНГ ОСТАНОВЛЕН", bg="#202538", fg="#8a99ad",
            font=("Segoe UI", 21, "bold"), pady=18, relief="flat", highlightthickness=2, highlightbackground="#2d3554"
        )
        self.card_verdict_banner.pack(fill="x", pady=(0, 12))

        stat_bar = tk.Frame(g_box, bg="#1d2237", padx=14, pady=8)
        stat_bar.pack(fill="x", pady=(0, 8))

        self.lbl_sim_percent = tk.Label(stat_bar, text="Сходство с владельцем: 0.0%", bg="#1d2237", fg="#ffffff", font=("Segoe UI", 12, "bold"))
        self.lbl_sim_percent.pack(side="left")

        self.lbl_live_pitch = tk.Label(stat_bar, text="Тон голоса (F0): -- Гц", bg="#1d2237", fg="#00e5ff", font=("Segoe UI", 11))
        self.lbl_live_pitch.pack(side="right")

        self.gauge_canvas = tk.Canvas(g_box, height=36, bg="#0d0e17", highlightthickness=1, highlightbackground="#252a42")
        self.gauge_canvas.pack(fill="x", pady=(0, 10))

        tk.Label(g_box, text="Осциллограмма и активность микрофона:", bg="#151928", fg="#7a889b", font=("Segoe UI", 9)).pack(anchor="w")
        self.wave_canvas = tk.Canvas(g_box, height=80, bg="#0d0e17", highlightthickness=1, highlightbackground="#252a42")
        self.wave_canvas.pack(fill="x", pady=(4, 10))

        tk.Label(g_box, text="Журнал событий:", bg="#151928", fg="#7a889b", font=("Segoe UI", 9)).pack(anchor="w")
        self.log_box = tk.Text(g_box, height=5, bg="#0d0e17", fg="#c0ccde", font=("Consolas", 9), relief="flat")
        self.log_box.pack(fill="both", expand=True)

    # --- TAB 3: ПРОВЕРКА ФАЙЛА ---
    def _build_tab_files(self):
        f_box = tk.Frame(self.tab_files, bg="#151928", padx=24, pady=20)
        f_box.pack(fill="both", expand=True)

        tk.Label(f_box, text="ПРОВЕРКА АУДИОФАЙЛА WAV НА ПРИНАДЛЕЖНОСТЬ ВЛАДЕЛЬЦУ", bg="#151928", fg="#00e5ff", font=("Segoe UI", 12, "bold")).pack(anchor="w", pady=(0, 6))
        tk.Label(f_box, text="Выберите аудиозапись голоса с диска для извлечения 512-d вектора и сравнения с владельцем:", bg="#151928", fg="#7a889b", font=("Segoe UI", 10)).pack(anchor="w", pady=(0, 14))

        ttk.Button(f_box, text="📁 Выбрать WAV файл для анализа...", style="FullCalib.TButton", command=self.test_wav_file).pack(anchor="w", pady=(0, 16))

        self.file_result_box = tk.Text(f_box, height=12, bg="#0d0e17", fg="#00ff88", font=("Consolas", 10), padx=12, pady=10, relief="flat")
        self.file_result_box.pack(fill="both", expand=True)
        self.file_result_box.insert("end", "Результаты проверки аудиофайла появятся здесь после выбора.\n")

    # --- Управление микрофонами ---
    def _populate_audio_devices(self):
        devs = sd.query_devices()
        in_devs = []
        cur_def = sd.default.device[0]
        def_idx = 0
        for i, d in enumerate(devs):
            if d['max_input_channels'] > 0:
                name = f"[{i}] {d['name']}"
                in_devs.append(name)
                if i == cur_def:
                    def_idx = len(in_devs) - 1
        self.combo_devices['values'] = in_devs
        if in_devs:
            self.combo_devices.current(def_idx)

    def _get_selected_device_idx(self):
        val = self.combo_devices.get()
        if val and val.startswith("["):
            try:
                return int(val.split("]")[0][1:])
            except Exception:
                pass
        return None

    def _log(self, msg):
        t = time.strftime("%H:%M:%S")
        self.log_box.insert("end", f"[{t}] {msg}\n")
        self.log_box.see("end")

    def _update_owner_ui(self):
        p = self.engine.owner_profile
        if p:
            self.lbl_p_name.configure(text=f"• Владелец: {p['name']}", fg="#00ff88")
            self.lbl_p_pitch.configure(text=f"• Основной тон (F0): {p['passport']['pitch']} Гц")
            self.lbl_p_timbre.configure(text=f"• Тембр: {p['passport']['timbre']}")
            self.lbl_p_centroid.configure(text=f"• Спектральная яркость: {p['passport']['centroid']} Гц")
            
            s_txt = "• Профиль пения: АКТИВЕН ✓" if p.get("sing_emb") is not None else "• Профиль пения: НЕ ЗАДАН"
            s_col = "#00ff88" if p.get("sing_emb") is not None else "#8a99ad"
            self.lbl_p_singing.configure(text=s_txt, fg=s_col)

            c_txt = "• Фильтр кашля: АКТИВЕН ✓" if p.get("cough_emb") is not None else "• Фильтр кашля: НЕ ЗАДАН"
            c_col = "#00ff88" if p.get("cough_emb") is not None else "#8a99ad"
            self.lbl_p_cough_filter.configure(text=c_txt, fg=c_col)

            n_val = p.get("noise_floor", 0.003)
            self.lbl_p_noise_floor.configure(text=f"• Шум комнаты: RMS {n_val:.4f}")

            self.lbl_p_thresh.configure(text=f"• Порог допуска: {p['threshold']*100:.1f}% (Пение: {SINGING_OWNER_THRESHOLD*100:.1f}%)")
            self.lbl_global_status.configure(text=f" ВЛАДЕЛЕЦ: {p['name'].upper()} ", bg="#004d26", fg="#00ff88")
            if not self.is_monitoring:
                self.card_verdict_banner.configure(text="СИСТЕМА ГОТОВА: НАЖМИТЕ 'ЗАПУСТИТЬ МОНИТОРИНГ'", bg="#202538", fg="#8a99ad")
        else:
            self.lbl_p_name.configure(text="• Владелец: НЕ ЗАРЕГИСТРИРОВАН", fg="#ff5577")
            self.lbl_p_pitch.configure(text="• Основной тон: -- Гц")
            self.lbl_p_timbre.configure(text="• Тембр: --")
            self.lbl_p_centroid.configure(text="• Спектральная яркость: -- Гц")
            self.lbl_p_singing.configure(text="• Профиль пения: НЕ ЗАДАН", fg="#8a99ad")
            self.lbl_p_cough_filter.configure(text="• Фильтр кашля: НЕ ЗАДАН", fg="#8a99ad")
            self.lbl_p_noise_floor.configure(text="• Шум комнаты: --")
            self.lbl_p_thresh.configure(text=f"• Порог допуска: {DEFAULT_OWNER_THRESHOLD*100:.1f}% (Пение: {SINGING_OWNER_THRESHOLD*100:.1f}%)")
            self.lbl_global_status.configure(text=" ТРЕБУЕТСЯ КАЛИБРОВКА ", bg="#3d1f2b", fg="#ff7799")
            if not self.is_monitoring:
                self.card_verdict_banner.configure(text="ОТКАЛИБРУЙТЕ ГОЛОС ВЛАДЕЛЬЦА (ВКЛАДКА 1)", bg="#331824", fg="#ff7799")

    def _switch_to_guard_and_start(self):
        self.notebook.select(self.tab_guard)
        if not self.is_monitoring:
            self._start_stream()

    # --- МУЛЬТИКЛАССОВЫЙ МАСТЕР КАЛИБРОВКИ ---
    def start_guided_calibration(self, full_mode=True):
        name = self.ent_owner_name.get().strip()
        if not name:
            messagebox.showwarning("Имя", "Введите имя владельца")
            return

        dev_idx = self._get_selected_device_idx()
        if dev_idx is None:
            messagebox.showwarning("Микрофон", "Пожалуйста, выберите рабочий микрофон в верхнем списке!")
            return

        if self.is_monitoring:
            self._stop_stream()

        self.btn_full_calib.configure(state="disabled")
        self.btn_quick_calib.configure(state="disabled")
        self.btn_sing_calib.configure(state="disabled")
        self.btn_cough_calib.configure(state="disabled")
        threading.Thread(target=self._calibration_wizard_thread, args=(name, dev_idx, full_mode), daemon=True).start()

    def calibrate_singing_only(self):
        """Быстрая калибровка только певческого голоса / напева в 1 клик"""
        dev_idx = self._get_selected_device_idx()
        if dev_idx is None:
            messagebox.showwarning("Микрофон", "Пожалуйста, выберите рабочий микрофон!")
            return
        if self.is_monitoring:
            self._stop_stream()

        self.btn_full_calib.configure(state="disabled")
        self.btn_quick_calib.configure(state="disabled")
        self.btn_sing_calib.configure(state="disabled")
        self.btn_cough_calib.configure(state="disabled")
        threading.Thread(target=self._singing_only_thread, args=(dev_idx,), daemon=True).start()

    def _singing_only_thread(self, dev_idx):
        self._log(f"--- ЗАПИСЬ НАПЕВА / ПЕНИЯ (Микрофон #{dev_idx}) ---")
        stage_full = "ДОПОЛНИТЕЛЬНЫЙ ЭТАП: НАПЕВ / ПЕНИЕ"
        prompt_text = "Спойте любую строчку, мелодию или протяните ноту («Ла-ла-ла...»)"

        for c in (3, 2, 1):
            self.root.after(0, lambda count=c: self._set_wizard_view(
                stage_full, prompt_text, f"Старт через {count} сек...", "#ffdd66"
            ))
            time.sleep(0.7)

        self.root.after(0, lambda: self._set_wizard_view(
            stage_full, prompt_text, "🎙 ПОЙТЕ СЕЙЧАС!", "#00ff88"
        ))

        try:
            rec = sd.rec(int(3.5 * SAMPLE_RATE), samplerate=SAMPLE_RATE, channels=1, dtype='float32', device=dev_idx)
            sd.wait()
        except Exception as e:
            self.root.after(0, lambda err=e: messagebox.showerror("Ошибка", f"Ошибка записи:\n{err}"))
            self._enable_calib_buttons()
            return

        audio = rec.flatten()
        trimmed = trim_active_audio(audio)
        emb = self.engine.get_embedding(trimmed)
        is_voice, pitch, centroid, timbre, is_sng, _ = check_human_voice(trimmed)

        if emb is None or not is_voice:
            self.root.after(0, lambda: self._set_wizard_view(
                "ЗВУК НЕ РАСПОЗНАН", "Не удалось зафиксировать певческий голос. Попробуйте еще раз ближе к микрофону.", "Повторите", "#ffaa00"
            ))
            self._enable_calib_buttons()
            return

        if not self.engine.owner_profile:
            name = self.ent_owner_name.get().strip() or "Главный Пользователь"
            self.engine.owner_profile = {
                "name": name,
                "centroid": emb,
                "passport": {"pitch": pitch, "centroid": centroid, "timbre": timbre},
                "threshold": DEFAULT_OWNER_THRESHOLD,
                "sing_emb": emb,
                "cough_emb": None,
                "noise_floor": 0.003
            }
        else:
            self.engine.owner_profile["sing_emb"] = emb

        self.engine.save_profile()
        self.root.after(0, self._update_owner_ui)
        self.root.after(0, lambda: self._set_wizard_view(
            "✓ НАПЕВ УСПЕШНО ДОБАВЛЕН!",
            f"Певческий эталон сохранен (F0: {pitch} Гц). Теперь вы можете свободно петь в эфире!",
            "Успешно", "#00ff88"
        ))
        self._log(f"✓ Эталон пения успешно сохранен (F0: {pitch} Гц, центроид {centroid} Гц)")
        self._enable_calib_buttons()

    def calibrate_cough_only(self):
        """Быстрая калибровка фильтра покашливаний в 1 клик"""
        dev_idx = self._get_selected_device_idx()
        if dev_idx is None:
            messagebox.showwarning("Микрофон", "Пожалуйста, выберите рабочий микрофон!")
            return
        if self.is_monitoring:
            self._stop_stream()

        self.btn_full_calib.configure(state="disabled")
        self.btn_quick_calib.configure(state="disabled")
        self.btn_sing_calib.configure(state="disabled")
        self.btn_cough_calib.configure(state="disabled")
        threading.Thread(target=self._cough_only_thread, args=(dev_idx,), daemon=True).start()

    def _cough_only_thread(self, dev_idx):
        self._log(f"--- ЗАПИСЬ ОБРАЗЦА КАШЛЯ (Микрофон #{dev_idx}) ---")
        stage_full = "ФИЛЬТР КАШЛЯ И ПОКАШЛИВАНИЙ"
        prompt_text = "Покашляйте 1-2 раза в микрофон"

        for c in (3, 2, 1):
            self.root.after(0, lambda count=c: self._set_wizard_view(
                stage_full, prompt_text, f"Старт через {count} сек...", "#ffdd66"
            ))
            time.sleep(0.7)

        self.root.after(0, lambda: self._set_wizard_view(
            stage_full, prompt_text, "🎙 КАШЛЯЙТЕ СЕЙЧАС!", "#00ff88"
        ))

        try:
            rec = sd.rec(int(2.8 * SAMPLE_RATE), samplerate=SAMPLE_RATE, channels=1, dtype='float32', device=dev_idx)
            sd.wait()
        except Exception as e:
            self.root.after(0, lambda err=e: messagebox.showerror("Ошибка", f"Ошибка записи:\n{err}"))
            self._enable_calib_buttons()
            return

        audio = rec.flatten()
        trimmed = trim_active_audio(audio, threshold=0.006)
        emb = self.engine.get_embedding(trimmed)

        if emb is None:
            self.root.after(0, lambda: self._set_wizard_view(
                "СЛИШКОМ ТИХО", "Звук кашля не обнаружен. Попробуйте еще раз.", "Повторите", "#ffaa00"
            ))
            self._enable_calib_buttons()
            return

        if not self.engine.owner_profile:
            name = self.ent_owner_name.get().strip() or "Главный Пользователь"
            self.engine.owner_profile = {
                "name": name,
                "centroid": emb,
                "passport": {"pitch": 120.0, "centroid": 1500.0, "timbre": "Голос"},
                "threshold": DEFAULT_OWNER_THRESHOLD,
                "sing_emb": None,
                "cough_emb": emb,
                "noise_floor": 0.003
            }
        else:
            self.engine.owner_profile["cough_emb"] = emb

        self.engine.save_profile()
        self.root.after(0, self._update_owner_ui)
        self.root.after(0, lambda: self._set_wizard_view(
            "✓ ФИЛЬТР КАШЛЯ СОХРАНЕН!",
            "Образец покашливания сохранен. Теперь система не будет ложно срабатывать на кашель.",
            "Успешно", "#00ff88"
        ))
        self._log("✓ Фильтр кашля успешно обновлен!")
        self._enable_calib_buttons()

    def _calibration_wizard_thread(self, name, dev_idx, full_mode):
        mode_str = "ПОЛНАЯ (5 ЭТАПОВ)" if full_mode else "БЫСТРАЯ (3 ЭТАПА)"
        self._log(f"--- НАЧАЛО КАЛИБРОВКИ: {mode_str} ВЛАДЕЛЬЦА '{name}' (Микрофон #{dev_idx}) ---")
        
        speech_takes = []
        speech_audios = []
        speech_pitches = []
        speech_centroids = []
        
        sing_emb = None
        cough_emb = None
        noise_floor = 0.003

        # Этапы калибровки
        steps = [
            ("ЭТАП 1: ОБЫЧНАЯ РЕЧЬ", "«Привет, Алиса, это мой голос»", "speech", 3.0),
            ("ЭТАП 2: ИНТОНАЦИЯ / ВОПРОС", "«Какая сегодня погода в городе?»", "speech", 3.0),
            ("ЭТАП 3: ГРОМКАЯ КОМАНДА", "«Включи мою любимую музыку на станции!»", "speech", 3.0)
        ]

        if full_mode:
            steps = [
                ("ЭТАП 1: ОБЫЧНАЯ РЕЧЬ", "«Привет, Алиса, это мой голос»", "speech", 3.0),
                ("ЭТАП 2: ИНТОНАЦИЯ / ВОПРОС", "«Какая сегодня погода в городе?»", "speech", 3.0),
                ("ЭТАП 3: ПЕНИЕ / НАПЕВАНИЕ", "Спойте любую строчку или протяните ноту: «Ла-ла-ла-ла...»", "singing", 3.5),
                ("ЭТАП 4: ФОНОВЫЙ ШУМ КОМНАТЫ", "Помолчите 2.5 секунды (система замерит тишину комнаты и микрофона)", "silence", 2.5),
                ("ЭТАП 5: КАШЕЛЬ И ПОКАШЛИВАНИЕ", "Покашляйте 1-2 раза в микрофон (чтобы ИИ игнорировал покашливания)", "cough", 2.8)
            ]

        total_steps = len(steps)

        for i, (stage_title, prompt_text, step_type, duration) in enumerate(steps):
            cur_num = f"{i+1}/{total_steps}"
            stage_full = f"{stage_title} ({cur_num})"
            self.root.after(0, lambda s=stage_full, p=prompt_text: self._set_wizard_view(s, p, "Приготовьтесь...", "#ffea79"))

            for c in (3, 2, 1):
                self.root.after(0, lambda s=stage_full, p=prompt_text, count=c: self._set_wizard_view(
                    s, p, f"Старт через {count} сек...", "#ffdd66"
                ))
                time.sleep(0.7)

            action_hint = "🎙 ПОЙТЕ СЕЙЧАС!" if step_type == "singing" else (
                "🎙 ГОВОРИТЕ СЕЙЧАС!" if step_type == "speech" else (
                    "🤫 ТИШИНА: МОЛЧИТЕ..." if step_type == "silence" else "🎙 КАШЛЯЙТЕ СЕЙЧАС!"
                )
            )
            self.root.after(0, lambda s=stage_full, p=prompt_text, a=action_hint: self._set_wizard_view(
                s, p, a, "#00ff88"
            ))

            try:
                rec = sd.rec(int(duration * SAMPLE_RATE), samplerate=SAMPLE_RATE, channels=1, dtype='float32', device=dev_idx)
                sd.wait()
            except Exception as e:
                self.root.after(0, lambda err=e: messagebox.showerror("Ошибка записи", f"Не удалось записать аудио:\n{err}"))
                self._enable_calib_buttons()
                return

            audio = rec.flatten()
            rms = float(np.sqrt(np.mean(audio**2)))

            if step_type in ("speech", "singing"):
                if rms < 0.003:
                    self.root.after(0, lambda: self._set_wizard_view(
                        "СЛИШКОМ ТИХО",
                        "Микрофон не уловил звук. Пожалуйста, говорите ближе к микрофону и нажмите кнопку калибровки заново.",
                        "Повторите попытку", "#ffaa00"
                    ))
                    self._enable_calib_buttons()
                    return

                is_voice, pitch, centroid, timbre, is_sng, _ = check_human_voice(audio)
                if not is_voice or pitch == 0.0:
                    self.root.after(0, lambda t=timbre: self._set_wizard_view(
                        "РЕЧЬ НЕ РАСПОЗНАНА",
                        f"Микрофон зафиксировал: {t}. Пожалуйста, произнесите фразу голосом четче или выберите микрофон вверху.",
                        "Повторите попытку", "#ffaa00"
                    ))
                    self._log(f"Этап {i+1} не принят: {timbre} (RMS: {rms:.4f})")
                    self._enable_calib_buttons()
                    return

                trimmed_audio = trim_active_audio(audio)
                emb = self.engine.get_embedding(trimmed_audio)
                if emb is None:
                    self.root.after(0, lambda: self._set_wizard_view(
                        "ОШИБКА ОБРАБОТКИ",
                        "Не удалось сформировать нейросетевой вектор. Попробуйте еще раз четче произнести фразу.",
                        "Ошибка признаков", "#ff4466"
                    ))
                    self._enable_calib_buttons()
                    return

                if step_type == "singing":
                    sing_emb = emb
                    self._log(f"Этап {i+1}/{total_steps} (Пение) принят: тон {pitch} Гц, центроид {centroid} Гц")
                else:
                    speech_takes.append(emb)
                    speech_audios.append(trimmed_audio)
                    speech_pitches.append(pitch)
                    speech_centroids.append(centroid)
                    self._log(f"Этап {i+1}/{total_steps} (Речь) принят: тон {pitch} Гц ({timbre}), центроид {centroid} Гц")

            elif step_type == "silence":
                noise_floor = max(0.001, rms)
                self._log(f"Этап {i+1}/{total_steps} (Шум комнаты) замерен: RMS {noise_floor:.5f}")

            elif step_type == "cough":
                trimmed_cough = trim_active_audio(audio, threshold=0.006)
                if len(trimmed_cough) >= int(0.25 * SAMPLE_RATE):
                    c_emb = self.engine.get_embedding(trimmed_cough)
                    if c_emb is not None:
                        cough_emb = c_emb
                        self._log(f"Этап {i+1}/{total_steps} (Кашель) успешно зафиксирован в фильтр кашля")
                else:
                    self._log(f"Этап {i+1}/{total_steps} (Кашель): звук был тихий, фильтр пропущен")

            time.sleep(0.5)

        # Вычисление главного эталонного центроида
        self.root.after(0, lambda: self._set_wizard_view(
            "НЕЙРОСЕТЕВОЙ СИНТЕЗ ПАСПОРТА",
            "Вычисление 512-d вектора ECAPA-TDNN, певческого эталона и фильтра кашля...",
            "Обработка...", "#00e5ff"
        ))

        centroid = np.mean(speech_takes, axis=0)
        centroid = centroid / np.linalg.norm(centroid)

        avg_pitch = float(np.mean(speech_pitches))
        avg_cent = float(np.mean(speech_centroids))
        _, _, _, timbre, _, _ = check_human_voice(speech_audios[0])

        similarities = [float(np.dot(t, centroid)) for t in speech_takes]
        avg_sim = float(np.mean(similarities))
        stability = avg_sim * 100

        passport = {
            "pitch": round(avg_pitch, 1),
            "centroid": round(avg_cent, 0),
            "timbre": timbre
        }

        self.engine.owner_profile = {
            "name": name,
            "centroid": centroid,
            "passport": passport,
            "threshold": DEFAULT_OWNER_THRESHOLD,
            "sing_emb": sing_emb,
            "cough_emb": cough_emb,
            "noise_floor": noise_floor
        }
        self.engine.save_profile()

        self._log("✓ ВСЕ ЭТАПЫ КАЛИБРОВКИ УСПЕШНО ЗАВЕРШЕНЫ!")
        self._log(f"• Тон F0: {passport['pitch']} Гц ({passport['timbre']})")
        self._log(f"• Стабильность дублей: {stability:.1f}%")
        self._log(f"• Профиль пения: {'АКТИВЕН' if sing_emb is not None else 'НЕ ЗАДАН'}")
        self._log(f"• Фильтр кашля: {'АКТИВЕН' if cough_emb is not None else 'НЕ ЗАДАН'}")

        self.root.after(0, self._update_owner_ui)
        self.root.after(0, lambda n=name: self._set_wizard_view(
            "✓ КАЛИБРОВКА УСПЕШНО ЗАВЕРШЕНА!",
            f"Профиль владельца '{n}' верифицирован для речи и пения. Перейдите во вкладку эфира!",
            "Профиль сохранен", "#00ff88"
        ))
        self._enable_calib_buttons()

    def _enable_calib_buttons(self):
        self.root.after(0, lambda: self.btn_full_calib.configure(state="normal"))
        self.root.after(0, lambda: self.btn_quick_calib.configure(state="normal"))
        self.root.after(0, lambda: self.btn_sing_calib.configure(state="normal"))
        self.root.after(0, lambda: self.btn_cough_calib.configure(state="normal"))

    def _set_wizard_view(self, stage, phrase, countdown, color):
        self.lbl_wizard_stage.configure(text=stage, fg=color)
        self.lbl_wizard_phrase.configure(text=phrase)
        self.lbl_wizard_countdown.configure(text=countdown, fg=color)

    # --- МОНИТОРИНГ ЭФИРА (СВОЙ / ЧУЖОЙ) ---
    def toggle_monitoring(self):
        if not self.is_monitoring:
            self._start_stream()
        else:
            self._stop_stream()

    def _start_stream(self):
        dev_idx = self._get_selected_device_idx()
        step = int(0.35 * SAMPLE_RATE)
        try:
            self.stream = sd.InputStream(samplerate=SAMPLE_RATE, channels=1, dtype='float32',
                                         device=dev_idx, blocksize=step, callback=self._audio_cb)
            self.stream.start()
            self.is_monitoring = True
            self.btn_monitor.configure(text="■ ОСТАНОВИТЬ МОНИТОРИНГ", style="Red.TButton")
            self.lbl_monitor_hint.configure(text="Эфир активен: говорите команды или пойте", fg="#00ff88")
            self.card_verdict_banner.configure(text="СЛУШАЮ ЭФИР: ЖДУ ГОЛОС...", bg="#1a2538", fg="#00e5ff")
            self._log(f"Запущен мониторинг эфира (Микрофон: {dev_idx})")
        except Exception as e:
            messagebox.showerror("Аудио ошибка", f"Не удалось открыть микрофон {dev_idx}:\n{e}")

    def _stop_stream(self):
        if self.stream:
            self.stream.stop()
            self.stream.close()
            self.stream = None
        self.is_monitoring = False
        self.btn_monitor.configure(text="▶ ЗАПУСТИТЬ МОНИТОРИНГ ЭФИРА", style="Green.TButton")
        self.lbl_monitor_hint.configure(text="Говорите команды или пойте в микрофон", fg="#7a889b")
        self.card_verdict_banner.configure(text="МОНИТОРИНГ ОСТАНОВЛЕН", bg="#202538", fg="#8a99ad")
        self._log("Мониторинг эфира остановлен")

    def _audio_cb(self, indata, frames, time_info, status):
        if self.is_monitoring:
            self.audio_q.put(indata.copy())

    def _audio_loop(self):
        win_size = int(1.8 * SAMPLE_RATE)

        while not self.audio_q.empty():
            chunk = self.audio_q.get().flatten()
            self.audio_buf = np.append(self.audio_buf, chunk)
            if len(self.audio_buf) > win_size:
                self.audio_buf = self.audio_buf[-win_size:]

            rms = float(np.sqrt(np.mean(chunk**2)))
            self._draw_vu(rms)
            self._draw_waveform(chunk)

            # Проверяем накопленный буфер (минимум 0.75 сек аудио)
            if rms > 0.005 and len(self.audio_buf) >= int(0.75 * SAMPLE_RATE):
                self.last_speech_time = time.time()
                is_voice, pitch, centroid, timbre, is_singing, is_cough_acoustic = check_human_voice(self.audio_buf)

                # Дополнительная проверка на кашель по последнему сегменту 0.45с (если кашель только что прозвучал)
                if not is_cough_acoustic and len(self.audio_buf) > int(0.45 * SAMPLE_RATE):
                    is_cough_acoustic, _, _ = check_cough_acoustic(self.audio_buf[-int(0.45 * SAMPLE_RATE):])

                owner = self.engine.owner_profile

                if owner:
                    live_emb = self.engine.get_embedding(self.audio_buf)
                    if live_emb is not None:
                        sim_speech = float(np.dot(live_emb, owner["centroid"]))
                        sim_sing = float(np.dot(live_emb, owner["sing_emb"])) if owner.get("sing_emb") is not None else -1.0
                        sim_cough = float(np.dot(live_emb, owner["cough_emb"])) if owner.get("cough_emb") is not None else -1.0

                        # Наилучшее сходство с владельцем (речь или пение)
                        sim_owner = max(sim_speech, sim_sing)
                        pct = max(0.0, min(100.0, sim_owner * 100))

                        # 1. ДЕТЕКТОР КАШЛЯ:
                        # Срабатывает либо по акустическому профилю трения горла, либо по вектору кашля
                        if is_cough_acoustic or (sim_cough > 0.32 and not is_voice):
                            self.lbl_live_pitch.configure(text="Звук: Кашель / Покашливание")
                            self.lbl_sim_percent.configure(text="Кашель обнаружен (Игнорируется)")
                            self._draw_gauge(0.0, DEFAULT_OWNER_THRESHOLD * 100)
                            self.card_verdict_banner.configure(
                                text="🟠 ✖ КАШЕЛЬ / ПОКАШЛИВАНИЕ ВЛАДЕЛЬЦА (ИГНОРИРУЕТСЯ)",
                                bg="#3b2310", fg="#ffaa44"
                            )
                            self._log("Зафиксирован кашель / покашливание владельца (команда проигнорирована)")

                        # 2. НЕ ГОЛОС (ПОСТОРОННИЙ ШУМ):
                        elif not is_voice:
                            self.lbl_live_pitch.configure(text=f"Звук: {timbre}")
                            self.lbl_sim_percent.configure(text="Сходство: 0.0% (Не речь)")
                            self._draw_gauge(0.0, DEFAULT_OWNER_THRESHOLD * 100)
                            self.card_verdict_banner.configure(
                                text="✖ НЕ ГОЛОС (ПОСТОРОННИЙ ШУМ / ПОМЕХА)",
                                bg="#301824", fg="#ff5577"
                            )

                        # 3. ЧЕЛОВЕЧЕСКИЙ ГОЛОС / ПЕНИЕ:
                        else:
                            # Адаптивный порог: при пении (любая тональность) или совпадении с певческим вектором - порог 0.65
                            is_owner_singing = is_singing or (owner.get("sing_emb") is not None and sim_sing >= SINGING_OWNER_THRESHOLD)
                            effective_thresh = SINGING_OWNER_THRESHOLD if is_owner_singing else owner["threshold"]
                            
                            tone_hint = f"Тон (F0): {pitch} Гц ({timbre})" + (" [ПЕНИЕ]" if is_singing else "")
                            self.lbl_live_pitch.configure(text=tone_hint)
                            self.lbl_sim_percent.configure(text=f"Сходство с владельцем: {pct:.1f}%")
                            self._draw_gauge(pct, effective_thresh * 100)

                            if sim_owner >= effective_thresh:
                                if is_owner_singing:
                                    banner_txt = f"🟢 ✓ ВЛАДЕЛЕЦ ПОЁТ: {owner['name'].upper()} (СВОЙ) — ДОСТУП РАЗРЕШЕН"
                                    bg_col = "#004d33" # deep emerald
                                else:
                                    banner_txt = f"🟢 ✓ ВЛАДЕЛЕЦ: {owner['name'].upper()} (СВОЙ) — ДОСТУП РАЗРЕШЕН"
                                    bg_col = "#00552b"

                                self.card_verdict_banner.configure(text=banner_txt, bg=bg_col, fg="#00ff88")
                            else:
                                self.card_verdict_banner.configure(
                                    text=f"✖ ЧУЖОЙ / НЕ ВЛАДЕЛЕЦ ({pct:.1f}%) — ДОСТУП ЗАПРЕЩЕН",
                                    bg="#551122", fg="#ff4466"
                                )
                else:
                    self.card_verdict_banner.configure(text="РЕЧЬ ОБНАРУЖЕНА (Владелец не откалиброван)", bg="#332a14", fg="#ffdd66")
            else:
                if self.is_monitoring and self.last_speech_time > 0 and (time.time() - self.last_speech_time > 2.2):
                    self.lbl_live_pitch.configure(text="Тон (F0): -- Гц (Ожидание речи)")
                    if self.engine.owner_profile:
                        self.card_verdict_banner.configure(text="СЛУШАЮ ЭФИР: ЖДУ ГОЛОС...", bg="#1a2538", fg="#00e5ff")
                    self.last_speech_time = 0.0

        self.root.after(40, self._audio_loop)

    def _draw_vu(self, rms):
        self.vu_meter.delete("all")
        w = self.vu_meter.winfo_width()
        h = self.vu_meter.winfo_height()
        if w <= 1:
            return
        level = min(1.0, rms * 15.0)
        lw = int(level * w)
        color = "#00ff88" if level < 0.75 else "#ff3355"
        self.vu_meter.create_rectangle(0, 0, lw, h, fill=color, width=0)

    def _draw_waveform(self, chunk):
        self.wave_canvas.delete("all")
        w = self.wave_canvas.winfo_width()
        h = self.wave_canvas.winfo_height()
        if w <= 1:
            return
        mid = h / 2
        step = max(1, len(chunk) // w)
        pts = []
        for x, val in enumerate(chunk[::step][:w]):
            y = mid - (val * mid * 3.5)
            pts.extend([x, max(2, min(h - 2, y))])
        if len(pts) >= 4:
            self.wave_canvas.create_line(pts, fill="#00d4ff", width=1.5)

    def _draw_gauge(self, value_pct, thresh_pct):
        self.gauge_canvas.delete("all")
        w = self.gauge_canvas.winfo_width()
        h = self.gauge_canvas.winfo_height()
        if w <= 1:
            return

        self.gauge_canvas.create_rectangle(0, 4, w, h - 4, fill="#1c2033", width=0)

        tx = int((thresh_pct / 100.0) * w)
        self.gauge_canvas.create_rectangle(0, 4, tx, h - 4, fill="#361a24", width=0)
        self.gauge_canvas.create_rectangle(tx, 4, w, h - 4, fill="#143322", width=0)

        vx = int((value_pct / 100.0) * w)
        fill_color = "#00ff88" if value_pct >= thresh_pct else "#ff4466"
        self.gauge_canvas.create_rectangle(0, 4, vx, h - 4, fill=fill_color, width=0)

        self.gauge_canvas.create_line(tx, 0, tx, h, fill="#ffffff", width=2)
        self.gauge_canvas.create_text(tx + 6, h/2, text=f"Порог {thresh_pct:.0f}%", fill="#ffffff", font=("Segoe UI", 8, "bold"), anchor="w")

    # --- ЭТАЛОН АЛИСЫ И СБРОС ---
    def set_alice_as_owner(self):
        alice_wav = 'extracted_fs/vendor/quasar/sounds/content_not_available.wav'
        if not os.path.exists(alice_wav):
            messagebox.showerror("Ошибка", "Файл Алисы не найден.")
            return

        w = wave.open(alice_wav, 'rb')
        data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32)[::3] / 32768.0
        emb = self.engine.get_embedding(data)
        _, pitch, centroid, timbre, _, _ = check_human_voice(data)

        self.engine.owner_profile = {
            "name": "Алиса (Яндекс)",
            "centroid": emb,
            "passport": {
                "pitch": pitch,
                "centroid": centroid,
                "timbre": "Женский (Алиса)"
            },
            "threshold": DEFAULT_OWNER_THRESHOLD,
            "sing_emb": None,
            "cough_emb": None,
            "noise_floor": 0.002
        }
        self.engine.save_profile()
        self._update_owner_ui()
        self._log("Профиль 'Алиса (Яндекс)' установлен как эталонный владелец")
        messagebox.showinfo("Успех", "Эталонный голос 'Алиса (Яндекс)' успешно активирован как владелец!")

    def reset_owner_profile(self):
        self.engine.owner_profile = None
        if os.path.exists(self.engine.profile_file):
            try:
                os.remove(self.engine.profile_file)
            except Exception:
                pass
        self._update_owner_ui()
        self._log("Профиль владельца сброшен")
        messagebox.showinfo("Сброс", "Профиль владельца успешно сброшен")

    # --- ТЕСТИРОВАНИЕ WAV ФАЙЛА ---
    def test_wav_file(self):
        path = filedialog.askopenfilename(filetypes=[("WAV files", "*.wav")])
        if not path:
            return
        try:
            w = wave.open(path, 'rb')
            sr = w.getframerate()
            ch = w.getnchannels()
            raw = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
            if ch > 1:
                raw = raw[::ch]
            if sr == 48000:
                raw = raw[::3]

            is_voice, pitch, centroid, timbre, is_sng, is_cgh = check_human_voice(raw)
            emb = self.engine.get_embedding(raw)

            owner = self.engine.owner_profile
            res = [f"=== РЕЗУЛЬТАТЫ АНАЛИЗА ФАЙЛА: {os.path.basename(path)} ==="]
            res.append(f"• Человеческий голос обнаружен: {'ДА' if is_voice else 'НЕТ'}")
            if is_voice:
                res.append(f"• Основной тон (Pitch F0): {pitch} Гц ({timbre})")
                res.append(f"• Режим: {'ПЕНИЕ' if is_sng else 'РЕЧЬ'}")
            elif is_cgh:
                res.append("• Тип звука: КАШЕЛЬ / ПОКАШЛИВАНИЕ")
            res.append(f"• Спектральный центроид: {centroid} Гц")

            if not is_voice:
                if is_cgh:
                    res.append("\nИТОГОВЫЙ ВЕРДИКТ: ✖ КАШЕЛЬ / ПОКАШЛИВАНИЕ (ИГНОРИРУЕТСЯ)")
                else:
                    res.append("\nИТОГОВЫЙ ВЕРДИКТ: ✖ НЕ ГОЛОС (ПОСТОРОННИЙ ШУМ)")
            elif owner and emb is not None:
                sim_sp = float(np.dot(emb, owner["centroid"]))
                sim_sg = float(np.dot(emb, owner["sing_emb"])) if owner.get("sing_emb") is not None else -1.0
                best_sim = max(sim_sp, sim_sg) * 100
                eff_th = (SINGING_OWNER_THRESHOLD if is_sng else owner["threshold"]) * 100
                
                if best_sim >= eff_th:
                    verdict = f"✓ ВЛАДЕЛЕЦ ПОЁТ: {owner['name'].upper()} (СВОЙ)" if is_sng else f"✓ ВЛАДЕЛЕЦ: {owner['name'].upper()} (СВОЙ)"
                else:
                    verdict = f"✖ ЧУЖОЙ / НЕ ВЛАДЕЛЕЦ ({best_sim:.1f}%)"

                res.append(f"• Сходство с эталоном '{owner['name']}': {best_sim:.1f}% (Порог: {eff_th:.1f}%)")
                res.append(f"\nИТОГОВЫЙ ВЕРДИКТ: {verdict}")
            else:
                res.append("\n(Профиль владельца не откалиброван - откалибруйте во вкладке 1)")

            text_out = "\n".join(res) + "\n\n"
            self.file_result_box.insert("end", text_out)
            self.file_result_box.see("end")
        except Exception as e:
            messagebox.showerror("Ошибка", f"Не удалось проанализировать файл:\n{e}")

def main():
    root = tk.Tk()
    app = OwnerGuardApp(root)
    root.mainloop()

if __name__ == '__main__':
    main()
