"""Upscale emulator screenshots for the README gallery.

Usage: python tools/upscale.py SRC.png DEST.png

Pebble screenshots are 144x168. Scaling them 3x with nearest-neighbour
sampling keeps the pixels sharp when GitHub shows them large, which plain
browser scaling does not.
"""
import sys

from PIL import Image

SCALE = 3

src, dest = sys.argv[1], sys.argv[2]
image = Image.open(src).convert("RGB")
image = image.resize((image.width * SCALE, image.height * SCALE), Image.NEAREST)
image.save(dest, optimize=True)
