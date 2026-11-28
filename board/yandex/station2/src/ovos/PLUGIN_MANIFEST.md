# Rust plugin manifest

Place one JSON file per plugin under /usr/share/atrios/ovos/plugins.d. Install its
AArch64 executable under /usr/lib/atrios/ovos/plugins. Both files must be shipped
inside a signed OTA and remain root-owned and non-writable by group or other.

Schema version 1:

    {
      "schema": 1,
      "id": "example-plugin",
      "name": "Example plugin",
      "version": "0.1.0",
      "api_version": 1,
      "binary": "example-plugin",
      "capabilities": ["intent"],
      "default_enabled": false
    }

Binary is a filename, not a path. Supported capabilities describe the role in
the OVOS pipeline: audio.capture, audio.playback, wake_word, vad, stt, tts,
intent, transformer, or word_correction. The current manager checks this
vocabulary but does not claim OS-level permission isolation.

State is kept in /var/lib/atrios/ovos/plugins-enabled.json, mode 0600. Use
atrios-ovos-plugin-manager --list, --validate, --enable ID, and --disable ID.
Changes are applied by the running manager within two seconds. The Rust SDK
in plugin-sdk connects only to the loopback OVOS MessageBus and uses the
standard OVOS message envelope.

The repository currently contains no wake-word, VAD, STT, or TTS plugin port.
Those services must be migrated from their corresponding OpenVoiceOS sources
and validated against the actual station microphone and audio hardware before
they can replace the removed assistant.
