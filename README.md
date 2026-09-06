# Omascribe

A pencil-first notes app for the Framework 12 (and any Linux tablet with a stylus).
One job: let the pen write, keep the ink as vectors, get out of the way.

Omawrite is for the keyboard. Omascribe is for the pen. Same stack (Qt Quick,
C++), same quiet chrome, same follow-the-desktop light and dark.

## What it does

- Infinite page on the vertical axis. The paper grows as you write down it.
- Fineliner with pressure. Finger pans. Wheel pans. Palm is ignored while the pen is down.
- Eraser removes whole strokes. Lasso selects and moves. Ruler snaps a stroke to 15 degree lines.
- Notes live as `.omascribe` JSON under `~/.local/share/omascribe/notes/`. Autosave.
- Inks are named (`ink`, `blue`, `red`, `gray`) and resolved against the paper, so a theme
  change never hides a stroke. `ink` is always the contrasting writing colour.
- Undo / redo. Title in the page. Note list on the left, like Apple Notes.

## Why C++ and Qt, not Rust

Omawrite is Qt Quick. The Framework 12 stylus already arrives as a Wayland tablet
device (`ILIT2901 Stylus`), and Qt's `QTabletEvent` carries pressure without extra
drivers. Rust is not on this machine, and a second GUI stack would fight the pen
for no gain. If this ever grows a crate, the file format stays JSON either way.

## Build

Needs `qt6-base`, `qt6-declarative`, `xdg-desktop-portal`. `qmake6` and `g++` do the rest.

```
./bin/build
./build/omascribe
./build/omascribe --self-test
```

## Use

- Pen draws. Finger or two-finger / wheel pans. Middle-mouse pans at a desk.
- `P` pen, `E` eraser, `V` select, `L` ruler
- `Ctrl+N` new note, `Ctrl+E` export PDF or SVG, `Ctrl+Z` undo, `Delete` deletes a selection, `F11` fullscreen

The working file stays `.omascribe` JSON on this machine. Export writes a shareable
vector PDF (A4 width, page as tall as the ink) or SVG: white paper, dark ink, never
the on-screen theme. The original note is not replaced.

Hardware eraser tip (if the pen has one) erases regardless of the current tool.

## Files

Vector strokes, not bitmaps:

```
{
  "format": "omascribe",
  "version": 1,
  "id": "...",
  "title": "...",
  "strokes": [{ "tool": "fineliner", "color": "#222324", "width": 2.4, "p": [x, y, pressure, ...] }]
}
```

## Gymbal

Screen rotation is Gymbal's job. Omascribe draws in the window's current
coordinates; Hyprland already remaps the tablet to the transformed output.
No extra handling here.

## Name

`omascribe`, not `omanote`. Scribe is the pen counterpart to write.
