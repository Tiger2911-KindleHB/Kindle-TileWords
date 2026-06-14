# TileWords KUAL

TileWords KUAL is a native local word-tile board game for jailbroken Kindle devices. It is designed as a KUAL extension with a high-contrast black-and-white interface, touch-first controls, local dictionary validation, and automatic save/load behavior.

This project is an early alpha. The first version focuses on reliable human-vs-human pass-and-play, board/rack handling, scoring, validation, and persistence before adding an AI opponent.

## Features

- Native Kindle framebuffer application
- Launches through KUAL
- No WebView
- No browser dependency
- No network requirement
- No online account requirement
- Fully local pass-and-play for two human players
- Standard 15x15 word-tile board
- Premium squares:
  - 2L: double letter
  - 3L: triple letter
  - 2W: double word
  - 3W: triple word
  - Center star/start square
- Black-and-white premium-square patterns and labels
- Seven-tile rack
- Tile bag using a classic English letter distribution
- Local move validation
- Local dictionary lookup
- Accurate scoring for:
  - Letter values
  - Letter premiums on newly placed tiles only
  - Word premiums on newly placed tiles only
  - Center-star double-word behavior
  - Cross-words
  - 50-point seven-tile bonus
- Pass turn
- Exchange tiles
- Shuffle rack
- Blank tile letter selection popup
- Invalid move popup
- New game confirmation popup
- Game-over popup
- Automatic save/load
- Saves after successful actions and local tile placement changes

## Installation

Download the release artifact named:

```text
tilewords-kual.zip
```

Unzip it directly into the Kindle extensions directory:

```text
/extensions
```

The resulting folder should look like this:

```text
/extensions/tilewords/
  tilewords.sh
  menu.json
  bin/
    tilewords
  data/
    dictionary.txt
    save.json
  assets/
    icons/
    fonts/
  README.md
```

Launch it from KUAL as:

```text
TileWords -> Start TileWords
```

## Controls

### Board

- Tap a rack tile to select it.
- Tap an empty board square to place the selected tile.
- Tap an unsubmitted board tile to return it to the rack.
- Submitted tiles are locked and cannot be removed.

### Rack

- The current player has a seven-tile rack at the bottom of the screen.
- The selected tile is inverted.
- Blank tiles show as `?` and prompt for a chosen letter when placed.
- `SHUF` shuffles the current rack.

### Buttons

- `SUBMIT`: validate and score the current move.
- `PASS`: pass the turn and return any unsubmitted tiles to the rack.
- `EXCH`: enter exchange mode.
- `SHUF`: shuffle the current rack.
- `NEW`: confirm and start a new game.
- `EXIT`: save and close the app.

### Exchange mode

1. Tap `EXCH`.
2. Tap rack tiles to mark them for exchange.
3. Tap `DONE` to exchange the selected tiles.
4. Tap `CANCEL` to leave exchange mode without exchanging.

## Move validation

The app validates moves locally.

Rules enforced in v1:

- At least one tile must be placed.
- First move must cover the center star.
- Later moves must connect to locked board tiles.
- New tiles must be placed in one row or one column.
- The main word cannot contain gaps.
- All newly formed words must exist in the local dictionary.
- Premium squares apply only to newly placed tiles.

## Dictionary

The dictionary lives here:

```text
/extensions/tilewords/data/dictionary.txt
```

The bundled dictionary is intentionally small and suitable for alpha testing. Replace it with a larger legal word list for normal play.

Dictionary format:

```text
WORD
ANOTHER
EXAMPLE
```

Notes:

- One word per line.
- A-Z letters only are used.
- Lowercase words are accepted but normalized to uppercase at startup.
- Lines starting with `#` are ignored.
- Words longer than 15 letters are ignored.

## Save behavior

The save file lives here:

```text
/extensions/tilewords/data/save.json
```

The app saves:

- Board letters
- Locked board tiles
- Blank-tile flags
- Current racks
- Tile bag contents
- Scores
- Current player
- Pass count
- Game-over state

If `save.json` is missing or invalid, the app starts a new game and recreates the save file after the next save action.

## Build instructions

This repository is intended to build through GitHub Actions using the Kindle hard-float toolchain.

Manual local build, assuming the KindleHF compiler is already on your `PATH`:

```bash
make clean
make CC=arm-kindlehf-linux-gnueabihf-gcc
make package
```

The packaged KUAL extension will be written to:

```text
dist/tilewords-kual.zip
```

## GitHub Actions

The included workflow:

```text
.github/workflows/build.yml
```

builds the native binary, stages the KUAL extension folder, and uploads a single artifact:

```text
tilewords-kual.zip
```

The artifact is designed to unzip cleanly into `/extensions`.

## Known limitations

- AI opponent is not included in v1.
- The bundled dictionary is a small starter dictionary for alpha testing.
- Rack rearrangement is implemented as shuffle, not manual drag-and-drop ordering.
- The UI is intentionally static; there are no animations.
- Touchscreen coordinate orientation can vary across Kindle generations. If taps are rotated or mirrored on a specific model, the input mapping layer may need a small device-specific adjustment.
- Partial-refresh ioctl support is not model-specific in v1. The app uses conservative redraws and framebuffer pan requests instead of relying on device-specific refresh structs.

## Project status

Alpha/testing release. Core local play is implemented first. Report bugs with the Kindle model, firmware version, what action triggered the issue, and whether the dictionary/save file had been modified.


## Hot-seat privacy

After a player completes a turn with **Submit**, **Pass**, or a confirmed **Exchange**, the app immediately hides the board/rack view and shows a plain handoff screen. The next player must tap **Confirm** before their rack is displayed. This prevents the outgoing player from seeing the incoming player's rack during pass-and-play.
