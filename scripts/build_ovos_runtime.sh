#!/bin/bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE_DIR=""
NATIVE_BUILD=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --stage-dir) STAGE_DIR="$2"; shift 2 ;;
    --native) NATIVE_BUILD=1; shift ;;
    -h|--help)
      echo "Usage: $0 --stage-dir <dir> [--native]"
      exit 0
      ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

[ -n "$STAGE_DIR" ] || { echo "Error: --stage-dir <dir> is required" >&2; exit 2; }
export PATH="$HOME/.cargo/bin:$PATH"
command -v cargo >/dev/null 2>&1 || { echo "Error: Cargo is required for the OVOS Rust workspace" >&2; exit 1; }

MANIFEST="$PROJECT_ROOT/board/yandex/station2/src/ovos/Cargo.toml"
TARGET=aarch64-unknown-linux-gnu
TARGET_ARGS=()
if [ "$NATIVE_BUILD" -eq 0 ]; then
  LINKER="${CROSS_COMPILE:-aarch64-linux-gnu-}gcc"
  [ -x "$(command -v "$LINKER" 2>/dev/null || true)" ] || { echo "Error: cross linker not found: $LINKER" >&2; exit 1; }
  export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER="$LINKER"
  TARGET_ARGS+=(--target "$TARGET")
fi

cargo build --manifest-path "$MANIFEST" --workspace --release --locked "${TARGET_ARGS[@]}"

if [ "$NATIVE_BUILD" -eq 1 ]; then
  BIN_DIR="$PROJECT_ROOT/board/yandex/station2/src/ovos/target/release"
else
  BIN_DIR="$PROJECT_ROOT/board/yandex/station2/src/ovos/target/$TARGET/release"
fi

install -d "$STAGE_DIR/usr/lib/atrios/ovos/plugins" \
  "$STAGE_DIR/usr/share/atrios/ovos/plugins.d" \
  "$STAGE_DIR/usr/share/licenses/atrios/ovos-messagebus" \
  "$STAGE_DIR/usr/share/doc/atrios/ovos" "$STAGE_DIR/etc/init.d"
install -m 755 "$BIN_DIR/ovos_messagebus" "$STAGE_DIR/usr/lib/atrios/ovos-messagebus"
install -m 755 "$BIN_DIR/atrios-ovos-plugin-manager" "$STAGE_DIR/usr/lib/atrios/atrios-ovos-plugin-manager"
for plug in ovos-dinkum-listener ovos-intent-router ovos-tts-audio; do
  install -m 755 "$BIN_DIR/$plug" "$STAGE_DIR/usr/lib/atrios/ovos/plugins/$plug"
  install -m 644 "$PROJECT_ROOT/board/yandex/station2/src/ovos/plugins/$plug/manifest.json" \
    "$STAGE_DIR/usr/share/atrios/ovos/plugins.d/$plug.json"
done
install -m 755 "$PROJECT_ROOT/board/yandex/station2/rootfs_overlay/etc/init.d/S58ovos-messagebus" \
  "$STAGE_DIR/etc/init.d/S58ovos-messagebus"
install -m 755 "$PROJECT_ROOT/board/yandex/station2/rootfs_overlay/etc/init.d/S59ovos-plugin-manager" \
  "$STAGE_DIR/etc/init.d/S59ovos-plugin-manager"
install -m 644 "$PROJECT_ROOT/board/yandex/station2/src/ovos/plugins.d.README" \
  "$STAGE_DIR/usr/share/atrios/ovos/plugins.d/README"
install -m 644 "$PROJECT_ROOT/board/yandex/station2/src/ovos/messagebus/LICENSE" \
  "$STAGE_DIR/usr/share/licenses/atrios/ovos-messagebus/LICENSE"
install -m 644 "$PROJECT_ROOT/board/yandex/station2/src/ovos/README.md" \
  "$STAGE_DIR/usr/share/doc/atrios/ovos/README.md"
install -m 644 "$PROJECT_ROOT/board/yandex/station2/src/ovos/PLUGIN_MANIFEST.md" \
  "$STAGE_DIR/usr/share/doc/atrios/ovos/PLUGIN_MANIFEST.md"
install -m 644 "$PROJECT_ROOT/board/yandex/station2/src/ovos/messagebus/UPSTREAM.md" \
  "$STAGE_DIR/usr/share/doc/atrios/ovos/UPSTREAM.md"

chmod 755 "$STAGE_DIR/usr/lib/atrios/ovos-messagebus" "$STAGE_DIR/usr/lib/atrios/atrios-ovos-plugin-manager"
echo "[ovos] staged OVOS Rust MessageBus and plugin manager"
