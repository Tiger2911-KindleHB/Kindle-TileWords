#!/bin/sh
APP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$APP_DIR/bin/tilewords"
LOG="$APP_DIR/data/launch.log"

mkdir -p "$APP_DIR/data"
cd "$APP_DIR" || exit 1

{
  echo "============================================================"
  echo "TileWords launch: $(date)"
  echo "APP_DIR=$APP_DIR"
  echo "BIN=$BIN"
  echo "PWD=$(pwd)"
  echo "USER=$(id)"
  ls -l "$BIN" 2>&1 || true
  echo "DISPLAY=${DISPLAY:-:0.0}"
} >> "$LOG" 2>&1

if [ ! -x "$BIN" ]; then
  echo "ERROR: missing executable binary: $BIN" >> "$LOG"
  eips -c >/dev/null 2>&1
  eips 2 4 "TileWords launch failed" >/dev/null 2>&1
  eips 2 6 "Missing executable:" >/dev/null 2>&1
  eips 2 7 "$BIN" >/dev/null 2>&1
  sleep 4
  exit 1
fi

export TILEWORDS_HOME="$APP_DIR"
export DISPLAY="${DISPLAY:-:0.0}"
export GDK_USE_XFT=0
export GTK_IM_MODULE=gtk-im-context-simple

lipc-set-prop com.lab126.powerd preventScreenSaver 1 >/dev/null 2>&1

"$BIN" >> "$LOG" 2>&1
RC=$?

echo "TileWords exited with code $RC" >> "$LOG"
lipc-set-prop com.lab126.powerd preventScreenSaver 0 >/dev/null 2>&1
eips -c >/dev/null 2>&1
exit "$RC"
