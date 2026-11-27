#!/usr/bin/env bash
# Headless regression test for Prism Island interactions.
# Run via: make test-ui   (uses xvfb-run)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP="$ROOT/bin/prism-monitor"
TMP=$(mktemp -d)
export XDG_CONFIG_HOME="$TMP/config"
export XDG_DATA_HOME="$TMP/data"
mkdir -p "$XDG_CONFIG_HOME/prism-monitor" "$XDG_DATA_HOME"
printf '[island]\nenabled=true\nmetrics=79\ncycle_s=30\n' \
    > "$XDG_CONFIG_HOME/prism-monitor/config.ini"

PID=""
WID=""
cleanup() {
    [ -n "$PID" ] && kill -9 "$PID" 2>/dev/null || true
    rm -rf "$TMP"
}
trap cleanup EXIT

xsetroot -solid black 2>/dev/null || true
# Use a unique GtkApplication ID so a running desktop instance cannot turn
# this test process into a remote proxy that exits immediately.
export PRISM_APP_ID_OVERRIDE="io.prism.Monitor.Test.test$$"
"$APP" > "$TMP/app.log" 2>&1 &
PID=$!

wait_win() {
    for _ in $(seq 1 60); do
        local id
        id=$(timeout 5 xdotool search --name '^Prism Island$' 2>/dev/null \
             | head -1 || true)
        if [ -n "$id" ]; then
            WID=$id
            return 0
        fi
        sleep 0.2
    done
    echo "FAIL: island window never appeared" >&2
    cat "$TMP/app.log" >&2
    exit 1
}
geom() {
    timeout 5 xdotool getwindowgeometry --shell "$WID" 2>/dev/null \
        | grep -E '^(X|Y|WIDTH|HEIGHT)='
}
read_geom() { eval "$(geom)"; }
assert_eq() {
    if [ "$2" != "$3" ]; then
        echo "FAIL $1: expected '$2', got '$3'" >&2
        exit 1
    fi
    echo "ok: $1 ($3)"
}
assert_gt() {
    if [ "$2" -le "$3" ]; then
        echo "FAIL $1: expected > $3, got $2" >&2
        exit 1
    fi
    echo "ok: $1 ($2)"
}
# No window manager under Xvfb: raise the island before synthetic clicks so
# stacking matches a real desktop (_NET_WM_STATE_ABOVE does this for us).
click_island() {
    xdotool windowraise "$WID"
    xdotool mousemove "$1" "$2"
    sleep 0.25
    xdotool click 1
    sleep 0.8
}
# Xvfb without a WM occasionally drops a synthetic click; retry a few times.
click_until_height() {
    local cx=$1 cy=$2 want=$3 attempt
    for attempt in 1 2 3; do
        click_island "$cx" "$cy"
        read_geom
        if [ "$HEIGHT" = "$want" ]; then
            return 0
        fi
    done
    return 1
}

wait_win
sleep 3   # let app_create finish mapping and raising the main window
read_geom
assert_eq "initial pill width" 340 "$WIDTH"
assert_eq "initial pill height" 56 "$HEIGHT"

# chevron expands to the adaptive panel
click_island $((X + WIDTH - 20)) $((Y + HEIGHT / 2))
read_geom
assert_eq "expanded width" 340 "$WIDTH"
assert_gt "expanded height" "$HEIGHT" 100

# collapse chevron returns to the pill (retry in case a click is dropped)
if ! click_until_height $((X + WIDTH - 22)) $((Y + 25)) 56; then
    echo "FAIL: collapse chevron did not restore pill" >&2
    exit 1
fi
assert_eq "collapsed width" 340 "$WIDTH"
assert_eq "collapsed height" 56 "$HEIGHT"

# body click cycles without resizing the window
click_island $((X + 120)) $((Y + HEIGHT / 2))
read_geom
assert_eq "cycle keeps pill height" 56 "$HEIGHT"

# drag to the right edge snaps to a sliver
SX=$X; SY=$Y
xdotool windowraise "$WID"
xdotool mousemove $((SX + 100)) $((SY + HEIGHT / 2))
xdotool mousedown 1
for i in $(seq 1 15); do
    xdotool mousemove --sync $((SX + 100 + i * 40)) $((SY + HEIGHT / 2)) >/dev/null
    sleep 0.02
done
xdotool mouseup 1
sleep 1
read_geom
assert_eq "docked sliver width" 14 "$WIDTH"
assert_eq "docked sliver height" 56 "$HEIGHT"

# hovering the sliver reveals the pill
xdotool windowraise "$WID"
xdotool mousemove $((X + WIDTH / 2)) $((Y + HEIGHT / 2))
sleep 0.9
read_geom
assert_eq "hover reveal width" 340 "$WIDTH"

# leaving the edge retracts again
xdotool mousemove 30 30
sleep 1.6
read_geom
assert_eq "retracted sliver width" 14 "$WIDTH"

echo "all island UI checks passed"
