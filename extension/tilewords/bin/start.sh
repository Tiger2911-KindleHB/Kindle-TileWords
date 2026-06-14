#!/bin/sh
APP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$APP_DIR/bin/tilewords"
LOG="$APP_DIR/data/launch.log"
APPLOG="$APP_DIR/data/app.log"

mkdir -p "$APP_DIR/data"
cd "$APP_DIR" || exit 1

# Do not leave a bad previous TileWords instance running.
if [ -f "$APP_DIR/data/tilewords.pid" ]; then
  OLD_PID="$(cat "$APP_DIR/data/tilewords.pid" 2>/dev/null || true)"
  if [ -n "$OLD_PID" ]; then
    kill "$OLD_PID" >/dev/null 2>&1 || true
    sleep 1
    kill -9 "$OLD_PID" >/dev/null 2>&1 || true
  fi
fi

{
  echo "============================================================"
  echo "TileWords launch: $(date)"
  echo "APP_DIR=$APP_DIR"
  echo "BIN=$BIN"
  echo "PWD=$(pwd)"
  echo "USER=$(id)"
  ls -l "$BIN" 2>&1 || true
  echo "DISPLAY=${DISPLAY:-:0.0}"
  echo "Starting binary..."
} >> "$LOG" 2>&1

: > "$APPLOG"

if [ ! -x "$BIN" ]; then
  echo "ERROR: missing executable binary: $BIN" >> "$LOG"
  eips -c >/dev/null 2>&1
  eips 2 4 "TileWords launch failed" >/dev/null 2>&1
  eips 2 6 "Missing executable binary" >/dev/null 2>&1
  sleep 4
  exit 1
fi

export TILEWORDS_HOME="$APP_DIR"
export DISPLAY="${DISPLAY:-:0.0}"
export GDK_USE_XFT=0
export GTK_IM_MODULE=gtk-im-context-simple
export XDG_CACHE_HOME="$APP_DIR/data/.cache"
export HOME="/mnt/us"

lipc-set-prop com.lab126.powerd preventScreenSaver 1 >/dev/null 2>&1

"$BIN" >> "$LOG" 2>&1 &
PID=$!
echo "$PID" > "$APP_DIR/data/tilewords.pid"
wait "$PID"
RC=$?

{
  echo "TileWords exited with code $RC"
  echo "--- app.log tail ---"
  tail -n 40 "$APPLOG" 2>/dev/null || true
} >> "$LOG" 2>&1

rm -f "$APP_DIR/data/tilewords.pid"
lipc-set-prop com.lab126.powerd preventScreenSaver 0 >/dev/null 2>&1

if [ "$RC" -ne 0 ]; then
  eips -c >/dev/null 2>&1
  eips 2 4 "TileWords launch failed" >/dev/null 2>&1
  eips 2 6 "See:" >/dev/null 2>&1
  eips 2 7 "$APP_DIR/data/launch.log" >/dev/null 2>&1
  eips 2 8 "$APP_DIR/data/app.log" >/dev/null 2>&1
  sleep 6
fi

exit "$RC"
