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
export HOME="$rt/home" XDG_STATE_HOME="$rt/state"
mkdir -p "$HOME" "$XDG_STATE_HOME"
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY TWMRC TWM_SOCK
export WLR_RENDERER=pixman
# The test program finds twm itself here (for `twm --reload`, configs).
export TWM_BIN="$twm" TWM_TEST_HOME="$HOME"

"$twm" --headless 1920x1080 --no-xwayland --exit-with-startup --test \
	-s "exec '$prog' '$out'"
