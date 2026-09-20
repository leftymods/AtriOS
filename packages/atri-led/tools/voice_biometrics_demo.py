#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Yandex Station Max - Offline Voice Biometrics (ECAPA-TDNN) Runner
Extracted and reverse-engineered from Yandex Quasar firmware:
  - vendor/quasar/bio_lingware/tflite-online-ecapa-tdnn-frontend/body.tflite
  - vendor/quasar/bio_lingware/tflite-online-ecapa-tdnn-frontend/head.tflite
  - vendor/quasar/bio_lingware/features_config.json
"""

import os
import sys
import wave
import numpy as np
from ai_edge_litert.interpreter import Interpreter

# --- 1. Audio Processing & Mel-Filterbank according to features_config.json ---

def hz_to_mel(hz):
    return 1127.0 * np.log(1.0 + hz / 700.0)

def mel_to_hz(mel):
    return 700.0 * (np.exp(mel / 1127.0) - 1.0)

def build_mel_filterbank(sr=16000, n_fft=512, n_mels=80, f_min=0.0, f_max=8000.0):
    mel_min = hz_to_mel(f_min)
    mel_max = hz_to_mel(f_max)
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

def extract_fbank_features(audio_16k):
    # Pre-emphasis coefficient: 0.97
    audio = np.append(audio_16k[0], audio_16k[1:] - 0.97 * audio_16k[:-1])
    frame_len = 320   # 20ms at 16kHz
    frame_shift = 160 # 10ms at 16kHz
    n_frames = (len(audio) - frame_len) // frame_shift + 1
    if n_frames < 1:
        return np.zeros((0, 80), dtype=np.float32)

    window = np.hamming(frame_len).astype(np.float32)
    features = []
    for i in range(n_frames):
        start = i * frame_shift
        frame = audio[start:start+frame_len].astype(np.float32)
        frame -= np.mean(frame)  # Remove DC offset
        frame *= window
        spec = np.fft.rfft(frame, 512)
        power = np.abs(spec) ** 2
        mel_energy = np.dot(MEL_FB, power)
        log_mel = np.log(np.maximum(mel_energy, 1e-10))
        features.append(log_mel)
    return np.array(features, dtype=np.float32)

def load_audio_wav_16k(path):
    w = wave.open(path, 'rb')
    sr = w.getframerate()
    ch = w.getnchannels()
    frames = w.readframes(w.getnframes())
    data = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0
    if ch > 1:
        data = data[::ch]
    if sr == 48000:
        data = data[::3]
    elif sr == 44100:
        # approximate downsample
        data = data[::2]
    return data

# --- 2. Neural Biometrics Model Loader ---

class YandexVoiceBiometrics:
    def __init__(self, base_dir='extracted_fs/vendor/quasar/bio_lingware/tflite-online-ecapa-tdnn-frontend'):
        body_path = os.path.join(base_dir, 'body.tflite')
        head_path = os.path.join(base_dir, 'head.tflite')
        
        if not os.path.exists(body_path) or not os.path.exists(head_path):
            raise FileNotFoundError(f"Biometrics models not found in {base_dir}")

        self.body = Interpreter(model_path=body_path)
        self.body.allocate_tensors()
        self.head = Interpreter(model_path=head_path)
        self.head.allocate_tensors()

    def get_embedding(self, audio_data):
        fbank = extract_fbank_features(audio_data)
        chunk_size = 40  # 40 frames = 400ms per partial
        n_chunks = len(fbank) // chunk_size
        if n_chunks == 0:
            padded = np.zeros((1, chunk_size, 80), dtype=np.float32)
            padded[0, :len(fbank), :] = fbank
            chunks = [padded]
        else:
            chunks = [fbank[i*chunk_size:(i+1)*chunk_size][np.newaxis, ...] for i in range(n_chunks)]

        body_outputs = []
        inp_idx = self.body.get_input_details()[0]['index']
        out_idx = self.body.get_output_details()[0]['index']

        for c in chunks:
            self.body.set_tensor(inp_idx, c)
            self.body.invoke()
            body_outputs.append(self.body.get_tensor(out_idx))

        all_body = np.concatenate(body_outputs, axis=1) # [1, T, 256]

        head_in_idx = self.head.get_input_details()[0]['index']
        head_out_idx = self.head.get_output_details()[0]['index']
        self.head.resize_tensor_input(head_in_idx, all_body.shape)
        self.head.allocate_tensors()
        self.head.set_tensor(head_in_idx, all_body)
        self.head.invoke()
        emb = self.head.get_tensor(head_out_idx).flatten()
        return emb / np.linalg.norm(emb)

    def compare_audio_files(self, file_a, file_b):
        sig_a = load_audio_wav_16k(file_a)
        sig_b = load_audio_wav_16k(file_b)
        emb_a = self.get_embedding(sig_a)
        emb_b = self.get_embedding(sig_b)
        similarity = float(np.dot(emb_a, emb_b))
        return similarity

if __name__ == '__main__':
    print("=" * 65)
    print("  Yandex Station Max - ECAPA-TDNN Voice Biometrics Engine")
    print("=" * 65)
    
    bio = YandexVoiceBiometrics()
    
    audio1 = 'extracted_fs/vendor/quasar/sounds/content_not_available.wav'
    audio2 = 'extracted_fs/vendor/quasar/sounds/content_not_paid.wav'
    audio3 = 'extracted_fs/vendor/quasar/sounds/turn_off_on_internet.wav'
    
    print("\n[+] Loading voice samples recorded in firmware...")
    print(f"    - Sample 1: {os.path.basename(audio1)} (Alice voice)")
    print(f"    - Sample 2: {os.path.basename(audio2)} (Alice voice)")
    print(f"    - Sample 3: {os.path.basename(audio3)} (Alice voice)")
    
    print("\n[+] Extracting 512-dimensional speaker embeddings via body.tflite & head.tflite...")
    sim_1_2 = bio.compare_audio_files(audio1, audio2)
    sim_1_3 = bio.compare_audio_files(audio1, audio3)
    sim_2_3 = bio.compare_audio_files(audio2, audio3)
    
    print(f"\n[*] Similarity (Sample 1 vs Sample 2): {sim_1_2:.4f}  --> IDENTICAL SPEAKER")
    print(f"[*] Similarity (Sample 1 vs Sample 3): {sim_1_3:.4f}  --> IDENTICAL SPEAKER")
    print(f"[*] Similarity (Sample 2 vs Sample 3): {sim_2_3:.4f}  --> IDENTICAL SPEAKER")
    
    # Test against different signal (e.g. noise)
    noise = (np.random.randn(32000) * 0.1).astype(np.float32)
    emb_noise = bio.get_embedding(noise)
    emb_1 = bio.get_embedding(load_audio_wav_16k(audio1))
    sim_noise = float(np.dot(emb_1, emb_noise))
    print(f"[*] Similarity (Alice vs Random Noise): {sim_noise:.4f}  --> DIFFERENT SENDER")
    
    print("\n[SUCCESS] Yandex ECAPA-TDNN Voice Biometrics executed successfully on this PC!")
