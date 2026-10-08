#!/usr/bin/env bash
# Runs the GUI tests on Qt's Wayland platform inside a private, headless
# compositor, so nothing appears on (or interferes with) the desktop session.
#
#   tests/run-on-wayland.sh <build-dir> [ctest arguments...]
#
# Needs dbus-run-session and one of: kwin_wayland (--virtual) or weston
# (--backend=headless). The compositor gets its own D-Bus session and its own
# XDG_RUNTIME_DIR; both are gone when the script ends.
set -euo pipefail

build_dir=${1:?usage: run-on-wayland.sh <build-dir> [ctest arguments...]}
shift || true

# Unix socket paths are limited to ~108 bytes, so keep this short.
runtime_dir=$(mktemp -d /tmp/qfd-wl.XXXXXX)
chmod 700 "$runtime_dir"
cleanup() {
    fusermount3 -u "$runtime_dir/doc" 2>/dev/null || true
    rm -rf "$runtime_dir" 2>/dev/null || true
}
trap cleanup EXIT

if command -v kwin_wayland >/dev/null; then
    compositor="kwin_wayland --virtual --no-lockscreen --width 2000 --height 1200 --socket qfd-wl"
elif command -v weston >/dev/null; then
    compositor="weston --backend=headless --width=2000 --height=1200 --socket=qfd-wl"
else
    echo "No headless-capable Wayland compositor found (kwin_wayland or weston)." >&2
    exit 77
fi

export QFD_BUILD_DIR=$build_dir QFD_COMPOSITOR=$compositor
env -u DISPLAY -u WAYLAND_DISPLAY XDG_RUNTIME_DIR="$runtime_dir" dbus-run-session -- bash -c '
    $QFD_COMPOSITOR >"$XDG_RUNTIME_DIR/compositor.log" 2>&1 &
    compositor_pid=$!
    for _ in $(seq 1 80); do
        [ -S "$XDG_RUNTIME_DIR/qfd-wl" ] && break
        sleep 0.25
    done
    if [ ! -S "$XDG_RUNTIME_DIR/qfd-wl" ]; then
        echo "The compositor did not come up:" >&2
        tail -n 20 "$XDG_RUNTIME_DIR/compositor.log" >&2
        kill "$compositor_pid" 2>/dev/null
        exit 1
    fi
    sleep 1
    status=0
    WAYLAND_DISPLAY=qfd-wl QT_QPA_PLATFORM=wayland \
        ctest --test-dir "$QFD_BUILD_DIR" --output-on-failure "$@" || status=$?
    kill "$compositor_pid" 2>/dev/null
    wait "$compositor_pid" 2>/dev/null
    exit $status
' bash "$@" 2> >(grep -v -E "dbus-daemon\[|xdg-desktop-portal|Gtk-WARNING" >&2)
