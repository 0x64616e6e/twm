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
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY
export WLR_RENDERER=pixman

"$twm" --headless 1920x1080 --no-xwayland --exit-with-startup \
	-s "exec '$prog' '$out'"
