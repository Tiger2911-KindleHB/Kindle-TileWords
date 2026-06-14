#!/bin/sh
EXT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP="$EXT_DIR/bin/tilewords"
LOG="$EXT_DIR/data/launch.log"
PIDFILE="$EXT_DIR/data/tilewords.pid"

mkdir -p "$EXT_DIR/data"
cd "$EXT_DIR" || exit 1
export TILEWORDS_HOME="$EXT_DIR"

cleanup() {
    lipc-set-prop com.lab126.powerd preventScreenSaver 0 >/dev/null 2>&1
    if [ -n "$CHILD_PID" ]; then
        kill "$CHILD_PID" >/dev/null 2>&1
        wait "$CHILD_PID" >/dev/null 2>&1
    fi
    rm -f "$PIDFILE"
}
trap cleanup EXIT INT TERM HUP

show_error() {
    MSG="$1"
    echo "$MSG" >> "$LOG"
    eips -c >/dev/null 2>&1
    eips 2 3 "TileWords launch failed" >/dev/null 2>&1
    eips 2 5 "$MSG" >/dev/null 2>&1
    eips 2 7 "See data/launch.log" >/dev/null 2>&1
    sleep 4
}

{
    echo "--- TileWords launch $(date) ---"
    echo "EXT_DIR=$EXT_DIR"
    echo "APP=$APP"
} >> "$LOG"

# Prevent stale copies from the previous bad launch from staying alive.
if [ -f "$PIDFILE" ]; then
    OLD_PID="$(cat "$PIDFILE" 2>/dev/null)"
    if [ -n "$OLD_PID" ]; then
        kill "$OLD_PID" >/dev/null 2>&1
        sleep 1
        kill -9 "$OLD_PID" >/dev/null 2>&1
    fi
    rm -f "$PIDFILE"
fi
killall tilewords >/dev/null 2>&1

if [ ! -f "$APP" ]; then
    show_error "Missing bin/tilewords. Run the GitHub build artifact."
    exit 1
fi

chmod 755 "$APP" >/dev/null 2>&1

lipc-set-prop com.lab126.powerd preventScreenSaver 1 >/dev/null 2>&1

eips -c >/dev/null 2>&1
"$APP" >> "$LOG" 2>&1 &
CHILD_PID=$!
echo "$CHILD_PID" > "$PIDFILE"
wait "$CHILD_PID"
RC=$?
CHILD_PID=""

eips -c >/dev/null 2>&1
lipc-set-prop com.lab126.powerd preventScreenSaver 0 >/dev/null 2>&1
rm -f "$PIDFILE"

echo "TileWords exited with code $RC" >> "$LOG"
exit "$RC"
