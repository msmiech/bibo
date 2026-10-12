#!/usr/bin/env bash
# Records the gameplay GIF of the README: the game plays tools/readme_demo.txt (menus, graphics settings, gameplay)
# with a fixed time step, writes every frame to a file and FFmpeg converts the frames to a GIF.
#
#   tools/record_gameplay.sh <build folder> [<GIF file>]
#
# The GIF file defaults to Screenshots/bibo_gameplay.gif. Needs FFmpeg and a display with OpenGL 4.3; on a server without one:
#   xvfb-run -a -s "-screen 0 1280x720x24" tools/record_gameplay.sh build
# Environment variables: WIDTH and HEIGHT of the game window (default 960x540), GIF_WIDTH (default 640), FPS (default 12)
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
build=$(cd "${1:?usage: $0 <build folder> [<GIF file>]}" && pwd)
gif=${2:-$repo/Screenshots/bibo_gameplay.gif}
demo=$repo/tools/readme_demo.txt
width=${WIDTH:-960}
height=${HEIGHT:-540}
gif_width=${GIF_WIDTH:-640}
fps=${FPS:-12}

game=$build/BIBO
if [ ! -x "$game" ]; then
	game=$build/BIBO.exe
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The game loads its assets from the current folder. With --demo and --record it ignores the saved settings.
echo "Recording the demo with $game..."
status=0
(cd "$build" && "$game" --demo "$demo" --record "$work/frames.rgb" --fps "$fps" "$width" "$height" 0) < /dev/null > "$work/bibo.log" 2>&1 || status=$?
cat "$work/bibo.log"
if [ "$status" -ne 0 ]; then
	echo "ERROR: BIBO exited with code $status" >&2
	exit 1
fi
# Size of the frames, e.g. "Recording 960x540 pixels at 15 fps to ..."
size=$(sed -n 's/^Recording \([0-9]*x[0-9]*\) pixels.*/\1/p' "$work/bibo.log")
if [ -z "$size" ] || [ ! -s "$work/frames.rgb" ]; then
	echo "ERROR: BIBO did not record any frames" >&2
	exit 1
fi

# One palette for the whole GIF (from the changing parts of the frames), only the changed rectangle of a frame is stored.
# Without dithering the GIF is about half as large (the camera moves, so almost every pixel changes in every frame).
echo "Converting the frames to $gif..."
mkdir -p "$(dirname "$gif")"
ffmpeg -hide_banner -loglevel error -y \
	-f rawvideo -pixel_format rgb24 -video_size "$size" -framerate "$fps" -i "$work/frames.rgb" \
	-filter_complex "vflip,scale=$gif_width:-1:flags=lanczos,split[frames][palette_input];[palette_input]palettegen=stats_mode=diff[palette];[frames][palette]paletteuse=dither=none:diff_mode=rectangle" \
	-loop 0 "$gif"
echo "$gif: $(du -h "$gif" | cut -f1)"
# GitHub shows large GIFs in the README slowly or not at all
if [ "$(wc -c < "$gif")" -gt 10000000 ]; then
	echo "WARNING: the GIF is larger than 10 MB, shorten tools/readme_demo.txt or lower GIF_WIDTH or FPS" >&2
fi
