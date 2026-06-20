# TileWords

TileWords is a local word-tile board game with pass-and-play multiplayer, CPU opponents, scoring, dictionary validation, and customizable game setup.

## Current Features

### Word-Tile Gameplay

* 15x15 word-tile board
* 7-tile player rack
* Tile bag system
* Turn-based play
* Submit word
* Pass turn
* Exchange tiles
* Shuffle rack
* Blank tile support
* Local word validation
* Score tracking
* End-game scoring foundation

### Premium Board Squares

* Double Letter squares
* Triple Letter squares
* Double Word squares
* Triple Word squares
* Center start square
* Premium squares use clear board labels

### Local Dictionary Support

* Uses a local dictionary file
* No internet lookup required
* Supports large word lists
* Dictionary loading screen on startup
* Dictionary-based move validation
* Case-insensitive word handling

### Human Players

* Local pass-and-play support
* 2 to 4 player games
* Private handoff screen between human players
* Player racks are hidden until that player confirms their turn
* Scoreboard supports multiple players
* Active player is clearly highlighted

### CPU Opponents

* CPU players can be added from New Game setup
* CPU automatically takes its turn
* CPU uses the same board rules, dictionary, and scoring system as human players
* CPU can place words, score points, exchange tiles, or pass
* CPU move result is shown before handing control back to the next human player

### CPU Difficulty

* Easy CPU difficulty
* Normal CPU difficulty
* Hard CPU difficulty
* Difficulty can be changed from Settings
* CPU difficulty changes how aggressively the CPU chooses scoring opportunities

### New Game Setup

* Choose number of players
* Set each player as Human or CPU
* Choose tile limit
* Choose standard or procedural board
* Enable or disable starter letters
* Start Game and Cancel options

### Procedural Board Option

* Standard board option
* Procedural board option
* Randomized premium square layout
* Center start square remains fixed
* Optional starter letters
* Starter letters are spaced so they do not immediately form words

### Handoff Screen

* Clean “Pass the Kindle” screen
* Shows whose turn is next
* Shows current scoreboard
* Shows how many points the previous player scored
* Shows the word used to score
* Keeps the Exit button available
* Uses a clear Confirm button for human turns

### Settings

* Grid size adjustment
* CPU difficulty adjustment
* Saved settings

### Interface

* Large touch targets
* Large rack tiles
* Readable board labels
* Minimal visual clutter
* Loading screen during startup

### Save and Resume

* Automatic save behavior
* Game restores on launch
* Saves player scores
* Saves current player
* Saves racks
* Saves tile bag
* Saves board state
* Saves CPU settings
* Saves grid size setting
* Saves procedural board state
* Unsubmitted tiles are returned to the rack before saving

### Controls

* New button
* Settings button
* Exit button
* Submit button
* Pass button
* Exchange button
* Shuffle button
* Value button showing the estimated score for the current move

## Current Status

TileWords is currently in active testing. The core game is playable with human players and CPU opponents. Current focus areas include interface polish, CPU balancing, board readability, and general usability improvements.
