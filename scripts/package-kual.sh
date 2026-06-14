#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN_SRC="${1:-$ROOT/builddir_kindlehf/tilewords}"
EXT_SRC="$ROOT/extension/tilewords"
PKG_ROOT="$ROOT/dist/pkg"
PKG_EXT="$PKG_ROOT/tilewords"
OUT="$ROOT/dist/tilewords-kual.zip"

if [ ! -f "$BIN_SRC" ]; then
  echo "Missing TileWords binary: $BIN_SRC" >&2
  exit 1
fi

rm -rf "$ROOT/dist"
mkdir -p "$PKG_EXT/bin" "$PKG_EXT/data"
cp -a "$EXT_SRC/." "$PKG_EXT/"
cp "$BIN_SRC" "$PKG_EXT/bin/tilewords"
chmod 755 "$PKG_EXT/bin/tilewords" "$PKG_EXT/bin/start.sh" "$PKG_EXT/tilewords.sh"

# Keep the dictionary in the artifact, but do not ship a stale save over an existing user save.
if [ ! -f "$PKG_EXT/data/dictionary.txt" ]; then
  cat > "$PKG_EXT/data/dictionary.txt" <<'DICT'
A
AN
AT
TO
IN
IT
IS
AS
ON
NO
OF
OR
DOG
CAT
WORD
WORDS
TILE
TILES
GAME
GAMES
PLAY
PLAYER
KINDLE
RACK
BOARD
SCORE
DICT
fi
: > "$PKG_EXT/data/save.json"

cd "$PKG_ROOT"
zip -r "$OUT" tilewords -x '*/.DS_Store'

echo "Wrote $OUT"
