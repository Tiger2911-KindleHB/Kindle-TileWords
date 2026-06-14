#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TRIPLE="arm-kindlehf-linux-gnueabihf"
TOOLCHAIN="$HOME/x-tools/$TRIPLE"
TOOLCHAIN_URL="https://github.com/koreader/koxtoolchain/releases/download/2025.05/kindlehf.tar.gz"
CROSS_FILE="$ROOT/build-kindlehf.cross"

cd "$ROOT"

echo "Using prebuilt kindlehf toolchain only. This script intentionally does not run koxtoolchain/gen-tc.sh."

if [ ! -x "$TOOLCHAIN/bin/$TRIPLE-g++" ]; then
  echo "Installing prebuilt KOReader kindlehf toolchain."
  mkdir -p "$HOME/x-tools"
  curl -L "$TOOLCHAIN_URL" -o /tmp/kindlehf.tar.gz
  tar -xzf /tmp/kindlehf.tar.gz -C "$HOME"
fi

if [ ! -x "$TOOLCHAIN/bin/$TRIPLE-g++" ]; then
  echo "ERROR: kindlehf compiler was not found after toolchain install: $TOOLCHAIN/bin/$TRIPLE-g++" >&2
  find "$HOME" -maxdepth 4 -type f -name "$TRIPLE-g++" -print >&2 || true
  exit 1
fi

SYSROOT="$TOOLCHAIN/$TRIPLE/sysroot"
if [ ! -d "$SYSROOT" ]; then
  echo "ERROR: kindlehf sysroot was not found: $SYSROOT" >&2
  find "$TOOLCHAIN" -maxdepth 4 -type d -name sysroot -print >&2 || true
  exit 1
fi

export PATH="$TOOLCHAIN/bin:$PATH"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig:$SYSROOT/usr/lib/arm-linux-gnueabihf/pkgconfig"
export PKG_CONFIG_PATH=""

$TRIPLE-g++ --version | head -n 1

cat > "$CROSS_FILE" <<CROSS
[binaries]
c = '$TOOLCHAIN/bin/$TRIPLE-gcc'
cpp = '$TOOLCHAIN/bin/$TRIPLE-g++'
ar = '$TOOLCHAIN/bin/$TRIPLE-ar'
strip = '$TOOLCHAIN/bin/$TRIPLE-strip'
pkgconfig = 'pkg-config'

[properties]
sys_root = '$SYSROOT'
pkg_config_libdir = ['$SYSROOT/usr/lib/pkgconfig', '$SYSROOT/usr/share/pkgconfig', '$SYSROOT/usr/lib/arm-linux-gnueabihf/pkgconfig']
needs_exe_wrapper = true

[built-in options]
c_args = ['--sysroot=$SYSROOT']
cpp_args = ['--sysroot=$SYSROOT']
c_link_args = ['--sysroot=$SYSROOT']
cpp_link_args = ['--sysroot=$SYSROOT']

[host_machine]
system = 'linux'
cpu_family = 'arm'
cpu = 'armv7'
endian = 'little'
CROSS

echo "Generated Meson cross file: $CROSS_FILE"

rm -rf builddir_kindlehf
meson setup --cross-file "$CROSS_FILE" builddir_kindlehf
meson compile -C builddir_kindlehf
