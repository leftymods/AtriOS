# OVOS Rust runtime for AtriOS beta

This workspace contains the OVOS MessageBus implementation and a Rust-only service
plugin foundation for the AArch64 station image.

The MessageBus source is vendored from
OscillateLabsLLC/ovos-rust-messagebus, a Rust implementation linked by the official
OpenVoiceOS ovos-messagebus project. Its upstream Apache-2.0 license and project
documentation are kept in messagebus/.

This is the transport and plugin-supervision foundation, not a complete voice
assistant. No wake-word, VAD, STT, TTS, or intent plugin has been ported yet, and
the new bus is not connected to the station microphone pipeline.

The Rust plugin manager follows the service boundary used by OVOS: plugins are
separate bus-connected services. It does not load Python entry points, install
code from the network, or execute files from writable directories. Each plugin
must be shipped inside a signed OTA as a root-owned AArch64 ELF binary with a
root-owned JSON manifest. The runtime state file lets the local app or an
administrator enable and disable already installed plugins without downloading
or executing new code.

A plugin process runs with the firmware's service privileges. The manifest
capability list describes which OVOS role it provides; it is not a Linux
sandbox. Only plugins shipped by a verified firmware package should be enabled.
