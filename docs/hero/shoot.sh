#!/usr/bin/env bash
# Hero screenshot on Hyprland 0.56 (Lua dispatchers). Scratch data dir with the
# hero note, window moved to workspace 2, single-window aspect ratio so the
# wallpaper shows, grim, then everything restored.
set -u
S="$(cd "$(dirname "$0")" && pwd)"
DATA="$S/xdg-data"
NOTE="${4:-$S/hello.omascribe}"
rm -rf "$DATA"; mkdir -p "$DATA/omascribe/notes"
cp "$NOTE" "$DATA/omascribe/notes/hero-hello.omascribe"
RATIO="${1:-1 1}"          # "1 1", "4 3", or "0 0" for none
MODE="${2:-window}"         # window | zen
OUT="${3:-$S/shot.png}"
WS=2

PREV_WS=$(hyprctl activeworkspace -j | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')
BEFORE=$(hyprctl clients -j | python3 -c 'import json,sys; print(" ".join(c["address"] for c in json.load(sys.stdin) if c["class"]=="omascribe"))')
RX=${RATIO% *}; RY=${RATIO#* }
hyprctl eval "hl.config({ layout = { single_window_aspect_ratio = { $RX, $RY }, single_window_aspect_ratio_tolerance = 0.02 } })" >/dev/null
env XDG_DATA_HOME="$DATA" setsid -f ~/.local/bin/omascribe >/dev/null 2>&1
ADDR=""
for i in $(seq 1 40); do
  ADDR=$(hyprctl clients -j | python3 -c "import json,sys; b='$BEFORE'.split(); print(next((c['address'] for c in json.load(sys.stdin) if c['class']=='omascribe' and c['address'] not in b), ''))")
  [ -n "$ADDR" ] && break; sleep 0.25
done
[ -z "$ADDR" ] && { echo "no new omascribe window"; exit 1; }
W="hl.get_window('address:$ADDR')"
hyprctl dispatch "hl.dsp.window.move({ workspace = $WS, window = $W })" >/dev/null
hyprctl dispatch "hl.dsp.focus({ workspace = $WS })" >/dev/null
hyprctl dispatch "hl.dsp.focus({ window = $W })" >/dev/null
sleep 2
if [ "$MODE" = zen ]; then
  hyprctl dispatch "hl.dsp.window.fullscreen({ window = $W })" 2>&1 | grep -v '^ok' | head -2
  sleep 1.5
fi
hyprctl clients -j | python3 -c "import json,sys; c=[c for c in json.load(sys.stdin) if c['address']=='$ADDR'][0]; print('window on ws', c['workspace']['id'], 'at', c['at'], 'size', c['size'], 'fullscreen', c['fullscreen'])"
# Park the pointer: on the tool pill in zen (the hover dot hides behind it), off-window otherwise.
if [ "$MODE" = zen ]; then hyprctl dispatch "hl.dsp.cursor.move({ x = 600, y = 716 })" >/dev/null; else hyprctl dispatch "hl.dsp.cursor.move({ x = 1190, y = 742 })" >/dev/null; fi
sleep 0.8
grim -o eDP-1 "$OUT" && echo "shot: $OUT"
hyprctl dispatch "hl.dsp.window.close({ window = $W })" >/dev/null
sleep 1
hyprctl eval "hl.config({ layout = { single_window_aspect_ratio = { 0, 0 }, single_window_aspect_ratio_tolerance = 0.1 } })" >/dev/null
hyprctl dispatch "hl.dsp.focus({ workspace = $PREV_WS })" >/dev/null
echo "restored ratio, back on workspace $PREV_WS"
