"""
Renders the icon of BIBO (Icon/bibo.svg) into the files the game uses:
- Icon/bibo.png: the window icon on Linux (X11), loaded by the game at start up
- Icon/bibo.ico: the icon of the executable and of the window on Windows (BIBO/bibo.rc), one image per common size

Run in the folder of the repository (resvg renders the SVG, Pillow writes the ICO file):

    uv run --with resvg-py --with pillow python tools/build_icon.py
"""
import io
import os

import resvg_py
from PIL import Image

ICON_FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Icon")
SVG = os.path.join(ICON_FOLDER, "bibo.svg")
PNG_SIZE = 256
# Every size is rendered from the SVG, so that the outlines stay sharp (instead of scaling the largest image down)
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]


def render(size):
	png = resvg_py.svg_to_bytes(svg_path=SVG, width=size, height=size)
	return Image.open(io.BytesIO(png)).convert("RGBA")


def main():
	render(PNG_SIZE).save(os.path.join(ICON_FOLDER, "bibo.png"), optimize=True)

	images = [render(size) for size in ICO_SIZES]
	images[-1].save(os.path.join(ICON_FOLDER, "bibo.ico"), sizes=[(size, size) for size in ICO_SIZES], append_images=images[:-1])


if __name__ == "__main__":
	main()
