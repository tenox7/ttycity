# ttycity — Micropolis (SimCity) for the terminal

A terminal port of [Micropolis Activity](https://github.com/SimHacker/micropolis/tree/master/micropolis-activity) using [ncurses](https://en.wikipedia.org/wiki/Ncurses). Original simulation engine as-is; only the X11/Tcl/Tk UI is replaced.

## Running

All assets (strings, scenarios, example cities) are baked into the binary — no external files needed.

```sh
./ttycity                       # new random city
./ttycity ../cities/bruce.cty   # load a city file
./ttycity -theme grass          # tan (default) | grass | dark
./ttycity -gfx standard         # classic curses look, 8 colors (default)
./ttycity -gfx unicode          # emoji / unicode graphics
./ttycity -gfx ascii            # 7-bit monochrome vt100
./ttycity -gfx aa               # aalib rendering (optional build)
```

## Graphics modes

Select with `-gfx <name>`, the `u` key, or *Options → Graphics*:

- **standard** (`color`) — one char per tile, ASCII + ACS + 8 colors; runs on any curses. The default.
- **unicode** — two columns per tile: emoji buildings/vehicles, box-drawing roads, braille terrain. Needs UTF-8 and wide-char curses (`make CURSES=-lncursesw` on Linux).
- **ascii** (`vt100`, `mono`) — strict 7-bit, no color, bold/reverse only.
- **aa** (`aalib`) — tiles rendered through [aalib](https://aa-project.sourceforge.net/aalib/), in color. Off by default: build with `make aalib` (add `AA_PREFIX=/opt/homebrew` if needed).

Without `-gfx` the mode is auto-detected from `TERM`/locale.

## Playing

| keys | action |
|------|--------|
| arrows / `hjkl` | move the cursor |
| Shift + letter  | pick a tool (see palette) |
| Enter           | build at the cursor |
| `m`             | cycle overview map / overlays |
| `u`             | cycle graphics mode |
| space           | pause · `0`–`3` set speed |
| Esc / F10       | menu bar |
| `g`             | new city · `q` quit |

Mouse supported: click to build, right-drag to pan, wheel scrolls; menus, palette and minimap are clickable. Resizable, works down to 80×24.

## Legal

Fork of Open Source Micropolis, based on SimCity Classic from Maxis, by Will Wright — https://github.com/SimHacker/micropolis/

- License: GPL (version 3 or later)
- Copyright (C) 1989 - 2007 Electronic Arts Inc.
