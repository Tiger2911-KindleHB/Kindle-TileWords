#!/bin/sh
EXT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$EXT_DIR" || exit 1

mkdir -p "$EXT_DIR/data" "$EXT_DIR/bin"
export TILEWORDS_HOME="$EXT_DIR"

# Keep the Kindle awake while the native framebuffer app owns the screen.
lipc-set-prop com.lab126.powerd preventScreenSaver 1 >/dev/null 2>&1

eips -c >/dev/null 2>&1
"$EXT_DIR/bin/tilewords"
RC=$?

eips -c >/dev/null 2>&1
lipc-set-prop com.lab126.powerd preventScreenSaver 0 >/dev/null 2>&1
exit "$RC"
