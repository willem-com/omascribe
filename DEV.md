# Omascribe working notes

Sessions: 6 Sep 2026 (Grok) and 7 Sep 2026 (Claude), WillemBG (Framework 12, Omarchy, Gymbal).
Git: `main` at `~/bench/2026-09-06-omascribe/` (local only, no remote).
This file is the pickup document. README is the user-facing summary.

Leave this bench. Do not teardown. The installed app and the notes stay.

## Pickup next session

```
cd ~/bench/2026-09-06-omascribe
git log --oneline
./bin/build && ./bin/install
omascribe --self-test
omascribe --readout
```

The running binary is `~/.local/bin/omascribe` (a copy, not a symlink). After a rebuild, run `./bin/install` or the old binary keeps running.

Desktop: Omascribe in the app list (`omascribe.desktop`, StartupWMClass=omascribe).

## What it is

Pencil notes. One job: jot with the Framework stylus, keep ink as vectors, get out of the way.

Omawrite is keyboard/markdown (Qt Quick, C++, Omarchy chrome). Omascribe is the pen counterpart on the same stack. Named omascribe, not omanote.

Not a hardware-bound fixture. Useful on any stylus Linux box. The install lives on WillemBG because this is the pen machine. Do not put it in [[willembg]]'s fixture list. Durable facts go in [[omascribe]] in the box.

## Why C++ / Qt Quick

- omawrite is Qt Quick. Matching chrome and theming was the point.
- Digitizer `ILIT2901:00 222A:5539 Stylus` (`/dev/input/event10`) is already a Wayland tablet. Hyprland name `ilit2901:00-222a:5539-stylus`.
- Qt `QTabletEvent` carries pressure, tilt, pointer type, barrel buttons.
- rustc was not on this machine. The `.omascribe` JSON does not care which language writes it.

## Layout on disk

```
~/bench/2026-09-06-omascribe/     source (keep)
  bin/build                      qmake6 + make
  bin/install                    copies binary + desktop + svg icon to ~/.local
  src/                           C++ and QML
  src/icons/                     stroke SVGs for the toolbar
  omawrite-ref/                  shallow clone of omacom-io/omawrite (theme/chrome reference)
  DEV.md                         this file
  README.md                      user-facing
  TEARDOWN.md                    lists extras; do not run it for a pause

~/.local/bin/omascribe
~/.local/share/applications/omascribe.desktop
~/.local/share/icons/hicolor/scalable/apps/omascribe.svg
~/.local/share/omascribe/notes/<uuid>.omascribe
~/.local/share/omascribe/trash/<uuid>.omascribe   deleted notes (since 7 Sep)
~/.local/share/omascribe/current.{json,png,svg,omascribe}
~/.config/willem.com/omascribe.conf     window geometry (QSettings)
```

First-run notes briefly lived under `~/.local/share/willem.com/omascribe/notes/` because organizationName was `willem.com`. Store now uses GenericDataLocation `omascribe/notes` and copies that legacy folder if the new one is empty.

## Build

Needs packages already on Omarchy 4: `qt6-base`, `qt6-declarative`, `qt6-svg`, `qt6-5compat` (QML `Qt5Compat.GraphicalEffects` for toolbar icon tint), `xdg-desktop-portal`. qmake6 and g++.

```
./bin/build          # -> build/omascribe
./bin/install        # -> ~/.local/bin/omascribe plus desktop/icon
./build/omascribe --self-test
./build/omascribe --readout
./build/omascribe --export-pdf in.omascribe out.pdf
./build/omascribe --export-svg in.omascribe out.svg
```

Self-test covers JSON roundtrip, named-colour canonicalization, undo/redo, PDF/SVG magic, agent readout files.

## Architecture

| File | Role |
|---|---|
| `src/main.cpp` | QGuiApplication, QML load, CLI flags |
| `src/systemtheme.*` | copied from omawrite: portal dark/light + text scale |
| `src/backend.*` | note list, autosave, theme colours, export, readout |
| `src/store.*` | `NoteStore` scans `notes/*.omascribe` |
| `src/document.*` | strokes, undo stack, JSON |
| `src/ink.*` | stroke geometry: ribbon (paired edge points), outline path for export, triangle list for the GPU |
| `src/inkcanvas.*` | tablet/touch/mouse, tools, pan, cursor; scene-graph nodes in `updatePaintNode` |
| `src/palette.h` | named inks for screen vs print |
| `src/exporter.*` | PDF, SVG, PNG, `writeAgentReadout` |
| `src/Main.qml` | Apple Notes-ish chrome, tool pill, Notes/Export chips |

InkCanvas is a plain `QQuickItem` since 7 Sep 2026. It builds scene-graph nodes in `updatePaintNode`: paper rect in item space, then one `QSGTransformNode` (translate by `-viewY`) holding the dot grid, one `QSGGeometryNode` per committed stroke, the live stroke, lasso, selection box and cursor. Scrolling is a matrix change. A committed stroke's triangles are built once and cached by stroke id (rebuilt when its point count, bounds, colour or the palette change); only the live stroke is rebuilt per frame. Edges are smoothed with 4x MSAA requested on the default `QSurfaceFormat` in `main.cpp` (`OMASCRIBE_MSAA=0|2|4|8` to compare). Hidden nodes sit under a `QSGOpacityNode` at opacity 0 and never carry zero vertices (a degenerate triangle instead).

History: the first version was a `QQuickPaintedItem` that repainted every stroke through QPainter on the CPU on every frame, hover event and scroll. That is why the pen felt better in the performance power profile (Willem's reflection, 6 Sep 23:05). A bitmap cache of committed ink was tried (commit `f449273`) and reverted (`47ef67f`): sagged look, no latency gain.

Stroke geometry (`ink.cpp`): `strokeRibbon` walks the polyline and emits paired left/right edge points at radius `strokeRadius` (width x pressure). Turns sharper than ~20 degrees collapse the inner side onto one point and walk an arc on the outer side, so joins stay round. `strokeOutline` turns that into one closed `QPainterPath` (winding fill, two round caps) for PDF/SVG/PNG; `appendStrokeTriangles` turns it into a triangle list for the scene graph. Same edge, both routes.

## File format

`.omascribe` is JSON:

```
{
  "format": "omascribe",
  "version": 1,
  "id": "<uuid>",
  "title": "...",
  "created": "ISO-8601",
  "modified": "ISO-8601",
  "height": 1400,
  "strokes": [
    {
      "id": "...",
      "tool": "fineliner",
      "color": "ink",
      "width": 2.4,
      "p": [x, y, pressure, x, y, pressure, ...]
    }
  ]
}
```

Autosave: 350 ms after last change, `QSaveFile` then rename. Also flush on note switch and quit. `Ctrl+S` is the same write. The "Saved" flash is that disk write, not a mode.

Delete (trash icon) moves the file to `~/.local/share/omascribe/trash/`; nothing is removed. No UI for the trash yet; restore by moving the file back into `notes/`.

Old files stored hex colours (`#222324`, `#eeeeee`). Loader maps near-black/near-white to `ink`, reds to `red`, blues to `blue`, greys to `gray`.

## Colours

Screen (`resolveColor(id, darkMode)`):

- `ink` : dark `#1a1a1a` / light `#f2f0ea` (always contrasts with the paper)
- `blue` / `red` / `gray` : chromatic, readable on both papers

Print/export/readout (`resolvePrintColor`): always dark marks on **white** paper. Never the on-screen theme. Cream paper in PDF was wrong (same class of bug as early iPad PNG export).

Paper in the app follows Omarchy `~/.local/state/omarchy/current/theme/colors.toml` plus a warm sheet in light mode (`#f7f4ec`).

## Input

Pen draws. Finger pans. Wheel pans. Middle-mouse pans. Palm is ignored while the pen is down or hovering (`m_penDown` / `penNear()` eat touch; `penNear` expires 1.5 s after the last tablet event in case a leave-proximity is missed). Any tablet event also cancels a finger pan in progress, so a palm that lands before the pen cannot turn the next stroke into a page drag.

### Framework stylus buttons

Official booklet: two programmable barrel buttons.

- **Lower** (nearest the tip): eraser. Linux: tool type flips to `BTN_TOOL_RUBBER`. Hold and write to erase. Hover-tap toggles the eraser tool so you can keep erasing without holding.
- **Upper**: right-click in firmware (`BTN_STYLUS`). In Omascribe that is pan (hold and move). Right-click is useless on a note.

Evdev node `ILIT2901:00 222A:5539 Stylus`: `BTN_TOOL_PEN`, `BTN_TOOL_RUBBER`, `BTN_TOUCH`, `BTN_STYLUS`, `BTN_STYLUS2`, pressure, tilt.

Qt maps `BTN_STYLUS` to `Qt::RightButton`, `BTN_STYLUS2` to `Qt::MiddleButton`, rubber to `PointerType::Eraser`.

The pen is not Bluetooth. It talks to the ILITEK digitizer. Flat battery was the first failure on this machine.

### Gestures

- Two-finger tap, little movement, under 500 ms: undo
- Two-finger drag: pan
- Three-finger tap: redo (Hyprland may steal three-finger for workspaces)
- `Ctrl+Z` / `Ctrl+Shift+Z`, toolbar arrows

### Tools

Pen, eraser (stroke eraser, not pixels), lasso select + move, ruler (15 degree snap). Hover cursor is a dot at the selected width, or an eraser ring.

Toolbar icons are stroke SVGs in `src/icons/`, tinted with `ColorOverlay`. Undo/redo are open C-curve arrows matching Willem's page sketch (not refresh circles, not Lucide hooked-chevrons).

## Export vs readout

Export (`Ctrl+E` or Export chip): human share file, PDF default, SVG in the picker. White paper, dark ink. Does not replace the working JSON.

Readout (for agents, refreshed 5 s after the pen rests, on note open, and on quit; before 7 Sep it ran 350 ms after every stroke on the GUI thread, which cost a hitch mid-sentence on a full page):

```
~/.local/share/omascribe/current.json
~/.local/share/omascribe/current.png     vision: the ink, no window chrome
~/.local/share/omascribe/current.svg
~/.local/share/omascribe/current.omascribe   symlink to the live note
```

When Willem says "look at my drawing", read `current.json` then `current.png`. Do not grim the window. Raw `p:[x,y,pressure,...]` will not let you read handwriting; the PNG will.

`omascribe --readout` prints `current.json`.

## Theming and chrome

- Dark/light from the desktop portal, same as omawrite
- Omarchy `colors.toml` for accent/background
- Compact window (`width < 880`): Notes chip opens an overlay list. Wide: sidebar.
- Title field blurs when the canvas is engaged (otherwise the caret stays after you start drawing)
- Gymbal owns rotation. No extra tablet mapping in the app. Hyprland remaps the stylus to the transformed output.

## Decisions that should not be silently reversed

1. Raster cache of committed ink: tried, looked sagged, reverted. Committed ink is scene-graph geometry now; do not go back to a painted item.
2. PDF/PNG-for-share paper is white, marks are dark, independent of UI theme.
3. Named inks in the file, resolved at draw time.
4. Working store is JSON vectors, not a PDF.
5. Two-finger tap is undo, not pan. Dragging two fingers still pans.
6. Lower stylus button is eraser (Framework default). Upper is pan, not a desktop right-click menu.
7. Delete goes to `trash/`, never straight to unlink.
8. Export is one filled outline path per stroke (the reflection page went from 2.4 MB to well under 300 KB); do not return to a polygon plus circle per segment.

## Open / next

- Hover-tap of the lower button to toggle eraser may be flaky: firmware often only sends eraser tool-type while the button is held. Hold-to-erase is the reliable gesture. Confirm on device.
- Three-finger redo vs Hyprland workspace gestures.
- No zoom, no typed text on the page, no layers, no cloud sync.
- Latency: committed ink no longer costs CPU per frame. What remains is Wayland + panel + the live stroke. If it still feels slow in power-saver, compare `OMASCRIBE_MSAA=0` and check the readout timer is not firing mid-write.
- Trash has no UI and no auto-purge. Add a "Trash" section in the sidebar if it ever fills up.
- Lasso selects by bounding-box overlap as a fallback; a stroke can be selected without being inside the lasso.
- No git remote. If this should live on WillemFW, add one and push. Until then the bench copy is the source.

## Trap list

- A palm that touches before the pen used to start a finger pan the pen never cancelled: the next stroke ended as a dot and dragged the page (fixed 9 Sep 2026, see Input).
- `TextInput` follows its caret even without focus, so a title wider than the field (compact window) showed only its tail. `autoScroll: activeFocus` now.

- `pkill -f omascribe` can kill the shell that contains that string. Kill by PID (`pgrep -x omascribe`).
- QML `tool: toolModel.get(index).value` fought C++ `setTool` from the stylus. Canvas tool is the source of truth; the pill syncs from `onToolChanged`.
- OrganizationName `willem.com` put QSettings and the first notes under `~/.local/share/willem.com/`. Notes moved; geometry may still be in `~/.config/willem.com/omascribe.conf`.
- Do not use em-dash in anything that faces Willem.

## Raw pen (8 Sep 2026)

Willem saw his jaggy handwriting come out "averaged". The app never smoothed:
libinput does, by default, for every tablet except Wacom AES. Fixed outside
the app with `/etc/libinput/local-overrides.quirks` (`MatchName=ILIT2901:00
222A:5539 Stylus`, `AttrTabletSmoothing=0`; takes effect when the device is
re-added, so relogin). App side, `kMinStep` in `inkcanvas.cpp` went from 0.7
to 0.05 logical px so no moving sample is dropped. Do not add smoothing,
Bezier fitting or point thinning to the ink path.

## Session 9 Sep 2026 (Claude)

Willem, in a half-width tiled window (600 x 750, compact mode): "scrolling is acting weird and I cannot see the document's title". Found: the live note's title carried 239 trailing spaces (source unknown, likely a held key while the field had focus), each a separate undo step, and the title field scrolled to the caret at the end so only blank showed. And the palm-first pan bug above. Changes: title field only auto-scrolls while editing and clips; consecutive title edits share one undo step; titles are trimmed on save, load, list and readout; touch is ignored while the pen hovers and any tablet event cancels a finger pan. Self-test covers the title behaviour. App restarted via `hyprctl dispatch closewindow` so it quit cleanly.

## Session 7 Sep 2026 (Claude)

Read the reflection PDF (`~/Documents/Reflectie-06-september-2026.pdf`, Willem's own hand): FW12 touch/digitizer not the best but works nicely; latency dropped a lot in performance mode; undo/redo icons sketched on the page and built by the agent ("fantastisch"); freedom to change everything vs the iPad notes app's blue lines. The vector original of that page was deleted after export (delete had no trash yet); the PDF is what survives.

Four changes, one commit each: scene-graph ink + MSAA, readout debounce 5 s, trash folder, single outline path per stroke. Verified: self-test (now also ribbon geometry and trash), test instance on a scratch `XDG_DATA_HOME`, Willem wrote "hello Claude, this is Willem" in it with the pen. That page was copied into the real notes folder.

## Exit this session

- Bench stays: `~/bench/2026-09-06-omascribe/`
- Install stays: `~/.local/bin/omascribe` and the desktop file
- Notes stay: `~/.local/share/omascribe/`
- App may be running. That is fine.
- Do not run TEARDOWN.md for a pause.
