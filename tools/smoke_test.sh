#!/usr/bin/env bash
# Smoke test: starts the game, plays tools/smoke_demo.txt (main menu, a few steps, the pause menu) and checks that it ends by
# itself without errors. Used by the CI for every build and for the release packages; run it after changes to the start up,
# the shaders, the assets or the clean up.
#
#   tools/smoke_test.sh <folder with BIBO and its assets>     (the build folder or an installed package)
#
# Needs a display with OpenGL 4.3; on a server without one: xvfb-run -a -s "-screen 0 1280x720x24" tools/smoke_test.sh build
# Fails if the game
# - crashes or exits with an error code (with the sanitizers also: a memory error, or a leak found when it exits),
# - does not end within SMOKE_TIMEOUT seconds (default 300): it hangs, e.g. while loading,
# - prints an error (shaders that do not compile, missing assets, incomplete framebuffers, ...).
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
folder=$(cd "${1:?usage: $0 <folder with BIBO and its assets>}" && pwd)
demo=$repo/tools/smoke_demo.txt
limit=${SMOKE_TIMEOUT:-300}

game=$folder/BIBO
if [ ! -x "$game" ]; then
	game=$folder/BIBO.exe
fi
log=$(mktemp)
trap 'rm -f "$log"' EXIT

fail() {
	# Shown as an annotation by GitHub Actions
	echo "::error::$1"
	echo "ERROR: $1" >&2
	exit 1
}

# The game loads its assets from the current folder. With --demo it ignores the saved settings.
echo "Smoke test of $game (at most $limit s)..."
status=0
(cd "$folder" && timeout "$limit" "$game" --demo "$demo" --fps 10 800 600 0) < /dev/null > "$log" 2>&1 || status=$?
cat "$log"
if [ "$status" -eq 124 ]; then
	fail "BIBO did not end within $limit s (it hangs, or SMOKE_TIMEOUT is too short for this machine)"
fi
if [ "$status" -ne 0 ]; then
	fail "BIBO exited with code $status"
fi
# Only print these words for real errors (see AGENTS.md)
if grep -E "Failed|Could not|ERROR|not complete|below 4.3" "$log"; then
	fail "BIBO reported errors"
fi
echo "Smoke test passed"
