#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLCHAIN="$HOME/x-tools/arm-kindlehf-linux-gnueabihf"
TOOLCHAIN_URL="https://github.com/koreader/koxtoolchain/releases/download/2025.05/kindlehf.tar.gz"

cd "$ROOT"

echo "Using prebuilt kindlehf toolchain only. This script intentionally does not run koxtoolchain/gen-tc.sh."

if [ ! -x "$TOOLCHAIN/bin/arm-kindlehf-linux-gnueabihf-g++" ]; then
  echo "Installing prebuilt KOReader kindlehf toolchain."
  mkdir -p "$HOME/x-tools"
  curl -L "$TOOLCHAIN_URL" -o /tmp/kindlehf.tar.gz
  tar -xzf /tmp/kindlehf.tar.gz -C "$HOME"
fi

export PATH="$TOOLCHAIN/bin:$PATH"
export PKG_CONFIG_PATH="$TOOLCHAIN/arm-kindlehf-linux-gnueabihf/sysroot/usr/lib/pkgconfig:$TOOLCHAIN/arm-kindlehf-linux-gnueabihf/sysroot/usr/share/pkgconfig:${PKG_CONFIG_PATH:-}"

arm-kindlehf-linux-gnueabihf-g++ --version | head -n 1

rm -rf builddir_kindlehf
meson setup --cross-file "$TOOLCHAIN/meson-crosscompile.txt" builddir_kindlehf
meson compile -C builddir_kindlehf
