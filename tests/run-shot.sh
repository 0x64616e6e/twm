#!/bin/sh
# Run one screenshot test: twm on the headless backend with the test program
# as its startup command. twm exits with the test's status.
#
#   run-shot.sh <twm> <test-program> <output.png>
set -eu
twm=$1
prog=$2
out=$3

rt=$(mktemp -d "${TMPDIR:-/tmp}/twm-test.XXXXXX")
trap 'rm -rf "$rt"' EXIT INT TERM
chmod 700 "$rt"
export XDG_RUNTIME_DIR="$rt"
# Never read the developer's own ~/.twmrc or geometry memory.
export HOME="$rt/home" XDG_STATE_HOME="$rt/state" TMPDIR="$rt"
mkdir -p "$HOME" "$XDG_STATE_HOME"
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY TWMRC TWM_SOCK
# Never the developer's session bus: a private one when dbus-daemon exists
# (portal tests), none otherwise (they skip).
unset DBUS_SESSION_BUS_ADDRESS
bus=""
if command -v dbus-run-session >/dev/null 2>&1; then
	bus="dbus-run-session --"
fi
export WLR_RENDERER=pixman
# The test program finds twm itself here (for `twm --reload`, configs).
export TWM_BIN="$twm" TWM_TEST_HOME="$HOME"
# GRT-9: --test stands this in for PAM, so the lock can be tested.
export TWM_TEST_LOCK_PASSWORD=hunter2
# GRT-11: and a stand-in fingerprint reader: a finger is this file existing.
export TWM_TEST_FINGER="$rt/finger"
# Stand-ins for system tools (a fake wpctl, LVL-*) go here, first on twm's PATH.
mkdir -p "$rt/bin"
export TWM_TEST_BIN="$rt/bin" PATH="$rt/bin:$PATH"

# GRT-1: a test may ask for more flags (--greeter); greetd's socket goes here.
export GREETD_SOCK="$rt/greetd.sock"
# shellcheck disable=SC2086
$bus "$twm" --headless 1920x1080 --no-xwayland --exit-with-startup --test ${TWM_EXTRA_ARGS:-} \
	-s "exec '$prog' '$out'"
