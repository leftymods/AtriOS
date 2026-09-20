#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Real-time Voice Biometrics & Speaker Recognition Listener
Uses Yandex Station Max ECAPA-TDNN models (body.tflite + head.tflite)
Features:
  - Real-time microphone listening (16 kHz, 16-bit float)
  - Voice Activity Detection (VAD) using energy threshold
  - Interactive profile enrollment (Register user voice)
  - Real-time speaker recognition (calculates similarity with enrolled users)
  - Visual audio volume / meter display
"""

import os
import sys
import time
import queue
import numpy as np
import sounddevice as sd
from ai_edge_litert.interpreter import Interpreter

# --- Configuration matching Yandex Quasar features_config.json ---
SAMPLE_RATE = 16000
FRAME_LENGTH = 320   # 20 ms
FRAME_SHIFT = 160    # 10 ms
CHUNK_FRAMES = 40    # 40 frames = 400 ms per inference chunk
CHUNK_SAMPLES = CHUNK_FRAMES * FRAME_SHIFT + (FRAME_LENGTH - FRAME_SHIFT) # 6560 samples
VAD_ENERGY_THRESHOLD = 0.008  # Energy threshold to consider speech active
SIMILARITY_MATCH_THRESHOLD = 0.82  # Cosine similarity threshold for same speaker

# Build 80-bin Mel Filterbank
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
HAMMING_WIN = np.hamming(FRAME_LENGTH).astype(np.float32)

def extract_fbank_features(audio_16k):
    if len(audio_16k) < FRAME_LENGTH:
        return np.zeros((0, 80), dtype=np.float32)
    # Pre-emphasis (0.97)
    audio = np.append(audio_16k[0], audio_16k[1:] - 0.97 * audio_16k[:-1])
    n_frames = (len(audio) - FRAME_LENGTH) // FRAME_SHIFT + 1
    features = []
    for i in range(n_frames):
        start = i * FRAME_SHIFT
        frame = audio[start:start+FRAME_LENGTH].astype(np.float32)
        frame -= np.mean(frame)
        frame *= HAMMING_WIN
        spec = np.fft.rfft(frame, 512)
        power = np.abs(spec) ** 2
        mel_energy = np.dot(MEL_FB, power)
        log_mel = np.log(np.maximum(mel_energy, 1e-10))
        features.append(log_mel)
    return np.array(features, dtype=np.float32)

class RealtimeVoiceBiometrics:
    def __init__(self):
        base_dir = 'extracted_fs/vendor/quasar/bio_lingware/tflite-online-ecapa-tdnn-frontend'
        body_path = os.path.join(base_dir, 'body.tflite')
        head_path = os.path.join(base_dir, 'head.tflite')

        print("[+] Initializing Yandex ECAPA-TDNN neural networks...")
        self.body = Interpreter(model_path=body_path)
        self.body.allocate_tensors()
        self.head = Interpreter(model_path=head_path)
        self.head.allocate_tensors()

        self.enrolled_profiles = {}
        self.audio_queue = queue.Queue()

    def get_embedding(self, audio_data):
        fbank = extract_fbank_features(audio_data)
        if len(fbank) < 10:
            return None

        # Pad or slice into 40-frame chunks
        n_chunks = len(fbank) // CHUNK_FRAMES
        if n_chunks == 0:
            padded = np.zeros((1, CHUNK_FRAMES, 80), dtype=np.float32)
            padded[0, :len(fbank), :] = fbank
            chunks = [padded]
        else:
            chunks = [fbank[i*CHUNK_FRAMES:(i+1)*CHUNK_FRAMES][np.newaxis, ...] for i in range(n_chunks)]

        inp_idx = self.body.get_input_details()[0]['index']
        out_idx = self.body.get_output_details()[0]['index']
        body_outputs = []

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
        norm = np.linalg.norm(emb)
        return emb / norm if norm > 0 else emb

    def enroll_user(self, user_name, duration_sec=3.5):
        print(f"\n=======================================================")
        print(f" [РЕГИСТРАЦИЯ ГОЛОСА] Пользователь: {user_name}")
        print(f" Говорите в микрофон любую фразу в течение {duration_sec} сек...")
        print(f"=======================================================")
        for i in range(3, 0, -1):
            print(f" Старт через {i}...", end='\r')
            time.sleep(0.7)
        print(" ЗАПИСЬ... ГОВОРИТЕ СЕЙЧАС!                     ")

        recording = sd.rec(int(duration_sec * SAMPLE_RATE), samplerate=SAMPLE_RATE, channels=1, dtype='float32')
        sd.wait()
        audio = recording.flatten()
        rms = np.sqrt(np.mean(audio**2))
        if rms < 0.003:
            print(" [!] Слишком тихо! Убедитесь, что микрофон подключен и активен.")
            return False

        emb = self.get_embedding(audio)
        if emb is not None:
            self.enrolled_profiles[user_name] = emb
            print(f" [OK] Голосовой отпечаток пользователя '{user_name}' успешно сохранён!")
            print(f"      Размерность вектора: {len(emb)} чисел, громкость RMS: {rms:.4f}")
            return True
        return False

    def audio_callback(self, indata, frames, time_info, status):
        if status:
            pass
        self.audio_queue.put(indata.copy())

    def start_realtime_listener(self):
        print("\n" + "=" * 65)
        print("  РЕЖИМ РЕАЛЬНОГО ВРЕМЕНИ: ПРОСЛУШИВАНИЕ МИКРОФОНА")
        print("  Нейросеть ECAPA-TDNN слушает речь и распознает говорящего")
        print("  (Нажмите Ctrl+C для выхода)")
        print("=" * 65)

        buffer = np.zeros(0, dtype=np.float32)
        window_samples = int(1.8 * SAMPLE_RATE)  # 1.8-секундное скользящее окно речи
        step_samples = int(0.4 * SAMPLE_RATE)    # обновление каждые 400 мс

        stream = sd.InputStream(samplerate=SAMPLE_RATE, channels=1, dtype='float32',
                                blocksize=step_samples, callback=self.audio_callback)

        with stream:
            while True:
                try:
                    chunk = self.audio_queue.get().flatten()
                    buffer = np.append(buffer, chunk)
                    if len(buffer) > window_samples:
                        buffer = buffer[-window_samples:]

                    rms = np.sqrt(np.mean(chunk**2))
                    meter = '#' * int(min(rms * 150, 30))

                    if rms > VAD_ENERGY_THRESHOLD and len(buffer) >= int(0.8 * SAMPLE_RATE):
                        # Речь активна, прогоняем через ИИ
                        emb = self.get_embedding(buffer)
                        if emb is not None and len(self.enrolled_profiles) > 0:
                            best_user = None
                            best_sim = -1.0
                            for user, user_emb in self.enrolled_profiles.items():
                                sim = float(np.dot(emb, user_emb))
                                if sim > best_sim:
                                    best_sim = sim
                                    best_user = user

                            if best_sim >= SIMILARITY_MATCH_THRESHOLD:
                                status_msg = f"[ГОЛОС ОПОЗНАН: {best_user}] (сходство {best_sim*100:.1f}%)"
                            else:
                                status_msg = f"[НЕОПОЗНАННЫЙ ГОЛОС] (макс. {best_user}: {best_sim*100:.1f}%)"

                            sys.stdout.write(f"\r RMS: {rms:.3f} |{meter:<30}| {status_msg:<35}")
                            sys.stdout.flush()
                        else:
                            sys.stdout.write(f"\r RMS: {rms:.3f} |{meter:<30}| [РЕЧЬ ОБНАРУЖЕНА] (нет базы)     ")
                            sys.stdout.flush()
                    else:
                        sys.stdout.write(f"\r RMS: {rms:.3f} |{meter:<30}| [ТИШИНА / ФОН]                  ")
                        sys.stdout.flush()

                except KeyboardInterrupt:
                    print("\n\nПрослушивание остановлено.")
                    break

def main():
    print("=" * 65)
    print("  Яндекс Станция Макс - Голосовая биометрия в реальном времени")
    print("  (Offline ECAPA-TDNN Voice Biometrics & Speaker Recognition)")
    print("=" * 65)

    bio = RealtimeVoiceBiometrics()

    # Загружаем также голос Алисы из прошивки как предустановленный профиль
    alice_wav = 'extracted_fs/vendor/quasar/sounds/content_not_available.wav'
    if os.path.exists(alice_wav):
        w = wave.open(alice_wav, 'rb')
        frames = w.readframes(w.getnframes())
        data = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0
        if w.getnchannels() > 1:
            data = data[::w.getnchannels()]
        if w.getframerate() == 48000:
            data = data[::3]
        alice_emb = bio.get_embedding(data)
        if alice_emb is not None:
            bio.enrolled_profiles["Алиса (Яндекс)"] = alice_emb
            print("[+] Предустановлен эталонный профиль: 'Алиса (Яндекс)'")

    # Меню взаимодействия
    while True:
        print("\nМеню:")
        print(" 1. Запустить прослушивание микрофона в реальном времени")
        print(" 2. Записать (зарегистрировать) свой голос в базу")
        print(" 3. Показать список зарегистрированных пользователей")
        print(" 4. Выход")
        choice = input("\nВыберите действие (1-4) [по умолчанию 1]: ").strip()

        if choice == '' or choice == '1':
            bio.start_realtime_listener()
        elif choice == '2':
            name = input("Введите имя пользователя (например, Ваше имя): ").strip()
            if not name:
                name = "Пользователь 1"
            bio.enroll_user(name)
        elif choice == '3':
            print("\nЗарегистрированные пользователи:")
            for u in bio.enrolled_profiles:
                print(f"  - {u}")
        elif choice == '4':
            print("Выход.")
            break
        else:
            print("Неверный ввод.")

if __name__ == '__main__':
    main()
