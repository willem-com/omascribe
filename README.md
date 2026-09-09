# Omascribe

A pencil-first notes app for the Framework 12 (and any Linux tablet with a stylus).
One job: let the pen write, keep the ink as vectors, get out of the way.

Omawrite is for the keyboard. Omascribe is for the pen. Same stack (Qt Quick,
C++), same quiet chrome, same follow-the-desktop light and dark.

## What it does

- Infinite page on the vertical axis. The paper grows as you write down it, but never scrolls on its own.
- Plain paper. The dot grid appears only while you scroll, then fades.
- Fineliner with pressure. Two fingers scroll. Wheel scrolls. One finger does nothing, so a palm cannot move the page. Touch is ignored while the pen is down or hovering.
- Eraser removes whole strokes. Lasso selects and moves. Ruler snaps a stroke to 15 degree lines.
- Notes live as `.omascribe` JSON under `~/.local/share/omascribe/notes/`. Autosave.
- Inks are named (`ink`, `blue`, `red`, `gray`) and resolved against the paper, so a theme
  change never hides a stroke. `ink` is always the contrasting writing colour.
- Typed text too: click the page with the mouse or touchpad, tap it with one finger, or click with the pen in the Text tool (`T`), then type. The block wraps at the width of your handwriting when the ink spans the page, otherwise at a normal reading width from where you tapped. Tap a block to edit it, tap empty paper to leave it. Pen draws over text.
- Undo / redo. Title in the page. Note list on the left, like Apple Notes.
- Ink is drawn as scene-graph geometry with 4x MSAA, so scrolling and hovering cost the CPU nothing.
- Deleting a note moves it to `~/.local/share/omascribe/trash/`.

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

- Pen draws. Two fingers or the wheel scroll. Nothing else moves the page, ever.
- `P` pen, `E` eraser, `V` select, `L` ruler, `T` text
- `Ctrl+N` new note, `Ctrl+E` export PDF or SVG, `Ctrl+Z` undo, `Ctrl+Shift+Z` redo
- Two-finger tap undoes, three-finger tap redoes
- `Delete` deletes a selection, `F11` fullscreen

The working file stays `.omascribe` JSON on this machine. Export writes a shareable
vector PDF (A4 width, page as tall as the ink) or SVG: white paper, dark ink, never
the on-screen theme. Each stroke is one filled outline path, so a full page is a few
hundred KB. The original note is not replaced.

`OMASCRIBE_MSAA=0` (or 2, 8) changes the multisampling for a latency comparison.

The Framework stylus has two barrel buttons. Firmware defaults, which Omascribe follows:

- **Lower button** (nearest the tip): eraser. Hold it and write to erase. Tap it in hover to toggle the eraser tool so you can keep erasing without holding.
- **Upper button**: reserved. While it is held the tip does nothing. It used to pan; scrolling is two fingers only now.

## Live agent readout

While Omascribe is open, the current note is mirrored for agents (no screenshot of
the window required):

```
~/.local/share/omascribe/current.json   metadata: title, strokes, colors, bounds, paths, typedText
~/.local/share/omascribe/current.png    ink on white paper (for vision)
~/.local/share/omascribe/current.svg    same ink as vectors
~/.local/share/omascribe/current.omascribe  symlink to the live JSON note
```

```
omascribe --readout
```

prints `current.json`. The files refresh 5 s after the pen rests (and on note open and quit).
`current.png` is the thing to open to *see* the drawing; the `.omascribe` file is the geometry.

## Files

Vector strokes, not bitmaps:

```
{
  "format": "omascribe",
  "version": 1,
  "id": "...",
  "title": "...",
  "strokes": [{ "tool": "fineliner", "color": "ink", "width": 2.4, "p": [x, y, pressure, ...] }],
  "texts": [{ "id": "...", "x": 24, "y": 600, "width": 620, "size": 17, "text": "typed" }]
}
```

## Gymbal

Screen rotation is Gymbal's job. Omascribe draws in the window's current
coordinates; Hyprland already remaps the tablet to the transformed output.
No extra handling here.

## Name

`omascribe`, not `omanote`. Scribe is the pen counterpart to write.
