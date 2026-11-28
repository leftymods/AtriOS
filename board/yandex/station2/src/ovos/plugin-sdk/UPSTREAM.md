# Upstream OVOS Protocol & Component Provenance

This crate (`ovos-plugin-sdk`) and the native Rust OVOS voice plugins in this
workspace implement the OpenVoiceOS (OVOS) MessageBus event protocol without a
Python runtime.

## Upstream References (Apache License 2.0)

- **OVOS Rust MessageBus** (`../messagebus`):
  - Upstream: `https://github.com/OscillateLabsLLC/ovos-rust-messagebus` (commit `eb99cda`)
  - License: Apache-2.0 (`../messagebus/LICENSE`)
- **OVOS MessageBus Protocol & Listener/Core/Audio Event Contracts**:
  - `OpenVoiceOS/ovos-dinkum-listener`: `recognizer_loop:wakeword`,
    `recognizer_loop:record_begin`, `recognizer_loop:record_end`,
    `recognizer_loop:utterance`, `recognizer_loop:audio_output_start`,
    `recognizer_loop:audio_output_end`
  - `OpenVoiceOS/ovos-core`: `speak`, `mycroft.stop`, intent routing envelope
    (`type`, `data`, `context`)
  - `OpenVoiceOS/ovos-audio`: TTS playback lifecycle (`speak` ->
    `recognizer_loop:audio_output_start` -> audio output ->
    `recognizer_loop:audio_output_end`, interrupted by `mycroft.stop`)
  - License: Apache-2.0

## Clean-Room Native Rust Implementations (AtriOS)

- `plugin-sdk`: async WebSocket client (`BusSender`, `OvosPlugin`,
  `run_plugin`, `run_service_plugin`) with loopback-only enforcement and
  1 MiB frame limits.
- `plugin-manager`: root-owned AArch64 manifest validator, process supervisor,
  runtime error telemetry, and mode-0600 settings store.
- `plugins/ovos-dinkum-listener`: native Rust audio listener connected to
  YNDX-0053 MAX `atri-aec --dual-vqe`, bounded ring-buffer VAD, Vosk wake-word
  + counter-activation + whisper recovery, and barge-in stop.
- `plugins/ovos-intent-router`: native Rust Russian intent router for local
  commands, volume, music control, timers, time, and stop/cancel.
- `plugins/ovos-tts-audio`: native Rust TTS/playback service using local voice
  pack + `espeak-ng` + `aplay` with whisper volume scaling and instant
  `mycroft.stop` cancellation.
