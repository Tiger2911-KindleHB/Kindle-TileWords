#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TRIPLE="arm-kindlehf-linux-gnueabihf"
TOOLCHAIN="$HOME/x-tools/$TRIPLE"
TOOLCHAIN_URL="https://github.com/koreader/koxtoolchain/releases/download/2025.05/kindlehf.tar.gz"
SDK_DIR="$HOME/kindle-sdk"
CROSS_FILE="$TOOLCHAIN/meson-crosscompile.txt"
GTK_PC="$TOOLCHAIN/$TRIPLE/sysroot/usr/lib/pkgconfig/gtk+-2.0.pc"

cd "$ROOT"

echo "Using the same KindleHF + Kindle SDK GTK2 workflow as the working Kindle apps."
echo "This script does not run koxtoolchain/gen-tc.sh."

if [ ! -x "$TOOLCHAIN/bin/$TRIPLE-g++" ]; then
  echo "Installing prebuilt KOReader kindlehf toolchain."
  mkdir -p "$HOME/x-tools"
  curl -L "$TOOLCHAIN_URL" -o /tmp/kindlehf.tar.gz
  tar -xzf /tmp/kindlehf.tar.gz -C "$HOME"
fi

if [ ! -x "$TOOLCHAIN/bin/$TRIPLE-g++" ]; then
  echo "ERROR: kindlehf compiler was not found: $TOOLCHAIN/bin/$TRIPLE-g++" >&2
  find "$HOME" -maxdepth 5 -type f -name "$TRIPLE-g++" -print >&2 || true
  exit 1
fi

export PATH="$TOOLCHAIN/bin:$PATH"
$TRIPLE-g++ --version | head -n 1

need_sdk=0
if [ ! -f "$CROSS_FILE" ]; then
  need_sdk=1
fi
if [ ! -f "$GTK_PC" ]; then
  need_sdk=1
fi

if [ "$need_sdk" = "1" ]; then
  echo "Installing Kindle SDK sysroot so Meson can find gtk+-2.0."

  sudo apt-get update
  sudo apt-get install -y \
    build-essential autoconf automake bison flex gawk \
    libtool libtool-bin libncurses-dev curl file git gperf \
    help2man texinfo unzip wget sed libarchive-dev nettle-dev \
    meson ninja-build pkg-config zip python3 make binutils aria2 \
    gtk2.0 libgtk2.0-dev

  rm -rf "$SDK_DIR"
  git clone --recursive --depth=1 https://github.com/KindleModding/kindle-sdk.git "$SDK_DIR"
  chmod +x "$SDK_DIR/gen-sdk.sh"

  set +e
  (cd "$SDK_DIR" && ./gen-sdk.sh kindlehf)
  sdk_rc=$?
  set -e

  sudo chown -R "$(id -u):$(id -g)" "$TOOLCHAIN" "$SDK_DIR" 2>/dev/null || true

  if [ "$sdk_rc" -ne 0 ]; then
    if [ -f "$CROSS_FILE" ] && [ -f "$GTK_PC" ]; then
      echo "Kindle SDK returned $sdk_rc after creating the required sysroot files; continuing."
    else
      echo "ERROR: Kindle SDK install failed before creating required GTK2 cross-build files." >&2
      echo "Missing check:" >&2
      ls -l "$CROSS_FILE" "$GTK_PC" >&2 || true
      exit "$sdk_rc"
    fi
  fi
fi

if [ ! -f "$CROSS_FILE" ]; then
  echo "ERROR: missing Meson cross file: $CROSS_FILE" >&2
  exit 1
fi
if [ ! -f "$GTK_PC" ]; then
  echo "ERROR: missing GTK2 pkg-config file: $GTK_PC" >&2
  exit 1
fi

rm -rf builddir_kindlehf
meson setup --cross-file "$CROSS_FILE" builddir_kindlehf
meson compile -C builddir_kindlehf
