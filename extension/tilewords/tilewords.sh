#!/bin/sh
EXT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$EXT_DIR/bin/start.sh"
