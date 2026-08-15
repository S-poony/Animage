#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rebuild every icon in this directory from animage.svg.

    python packaging/make-icons.py

The mark is drawn in animage.af, which is the Affinity Designer document it was
made in and the file to open to change it. Nothing automated reads that -- the
format is proprietary and closed -- so the chain is: export animage.svg from it
by hand, then run this, which derives everything else. That is the PNG ladder
the application's window icon is built out of, the Windows .ico the executable
carries, and the macOS .icns the bundle shows. All of it is committed rather
than generated during the build, because rasterising an SVG needs a renderer no
build machine is promised to have. Run this when the mark changes, and commit
what it writes.

Inkscape is the renderer, and is required to be on PATH or named with
--inkscape. Qt's own SVG support would have been the obvious alternative and
cannot be used: it implements SVG Tiny 1.2, and the mark uses a <mask>, which
Tiny has no notion of. Rendered by Qt the masked stroke simply disappears.

Nothing else is required. The .ico and .icns writers below are a few dozen
lines each and save the project a Pillow dependency it would otherwise carry
for one command a year.
"""

import argparse
import pathlib
import shutil
import struct
import subprocess
import sys
import zlib

HERE = pathlib.Path(__file__).resolve().parent
SVG = HERE / "animage.svg"
ICONS = HERE / "icons"

# What the window icon is offered. Qt picks the nearest at or above the size
# the platform asks for, so the ladder is the set of sizes worth having exact
# pixels for: the small ones a title bar and a task switcher ask for, and the
# large ones a file manager or a 200% display scales from.
PNG_SIZES = [16, 24, 32, 48, 64, 128, 256, 512]

# Windows reads these out of the executable. 256 is the one the large icon
# views use, and is stored as PNG; the rest are stored as bitmaps, which is
# what every version of Windows can read at those sizes.
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
ICO_AS_PNG = {128, 256}

# macOS names its icon sizes by four-character type rather than by number.
# These are the PNG-carrying ones, covering 32 through 512 at both plain and
# retina scale; Finder scales down for the two sizes below them.
ICNS_TYPES = [
    (b"ic11", 32),   # 16@2x
    (b"ic12", 64),   # 32@2x
    (b"ic07", 128),
    (b"ic13", 256),  # 128@2x
    (b"ic08", 256),
    (b"ic14", 512),  # 256@2x
    (b"ic09", 512),
]


def render(inkscape: str) -> None:
    """Rasterise the SVG at every size in the ladder."""
    ICONS.mkdir(exist_ok=True)
    for size in PNG_SIZES:
        out = ICONS / f"animage-{size}.png"
        subprocess.run(
            [inkscape, str(SVG), "--export-type=png", f"--export-filename={out}",
             # The artwork runs past the artboard on every side -- the mark is a
             # crop of a larger drawing -- so the area has to be named, or
             # Inkscape exports the drawing's own bounds instead of the square.
             "--export-area-page", f"--export-width={size}", f"--export-height={size}"],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        print(f"  {out.relative_to(HERE.parent)}")


def read_png(path: pathlib.Path) -> tuple[int, int, bytes]:
    """Return (width, height, RGBA rows) for one of our own PNGs.

    Narrow on purpose: it reads what Inkscape writes -- eight bits a channel,
    no interlacing -- and refuses anything else rather than quietly guessing.
    """
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{path} is not a PNG"
    pos, idat, header = 8, bytearray(), None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    assert header is not None, f"{path} has no IHDR"
    width, height, depth, colour, compression, filt, interlace = header
    assert (depth, compression, filt, interlace) == (8, 0, 0, 0), f"{path}: unsupported PNG"
    assert colour in (2, 6), f"{path}: unsupported colour type {colour}"
    channels = 3 if colour == 2 else 4

    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for _ in range(height):
        method = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = prev[i]
            upleft = prev[i - channels] if i >= channels else 0
            if method == 1:
                line[i] = (line[i] + left) & 0xFF
            elif method == 2:
                line[i] = (line[i] + up) & 0xFF
            elif method == 3:
                line[i] = (line[i] + (left + up) // 2) & 0xFF
            elif method == 4:
                p = left + up - upleft
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                best = left if (pa <= pb and pa <= pc) else (up if pb <= pc else upleft)
                line[i] = (line[i] + best) & 0xFF
            elif method != 0:
                raise AssertionError(f"{path}: unknown filter {method}")
        prev = line
        if channels == 3:
            for i in range(0, stride, 3):
                out += line[i:i + 3] + b"\xff"
        else:
            out += line
    return width, height, bytes(out)


def dib(path: pathlib.Path) -> bytes:
    """One icon as a Windows bitmap: header, bottom-up BGRA, and a mask."""
    width, height, rgba = read_png(path)
    head = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    rows = []
    for y in range(height - 1, -1, -1):
        row = rgba[y * width * 4:(y + 1) * width * 4]
        rows.append(b"".join(bytes((row[i + 2], row[i + 1], row[i], row[i + 3]))
                             for i in range(0, len(row), 4)))
    # The AND mask is obsolete for 32-bit icons -- the alpha channel above says
    # what is transparent -- but it is not optional, so it is written opaque.
    mask_stride = ((width + 31) // 32) * 4
    return head + b"".join(rows) + bytes(mask_stride * height)


def write_ico(out: pathlib.Path) -> None:
    images = []
    for size in ICO_SIZES:
        png = ICONS / f"animage-{size}.png"
        images.append((size, png.read_bytes() if size in ICO_AS_PNG else dib(png)))
    offset = 6 + 16 * len(images)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    for size, blob in images:
        # 256 is written as 0: the field is one byte, and 256 does not fit.
        directory += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0,
                                 1, 32, len(blob), offset)
        offset += len(blob)
    out.write_bytes(bytes(directory) + b"".join(blob for _, blob in images))
    print(f"  {out.relative_to(HERE.parent)}")


def write_icns(out: pathlib.Path) -> None:
    chunks = b""
    for kind, size in ICNS_TYPES:
        blob = (ICONS / f"animage-{size}.png").read_bytes()
        chunks += kind + struct.pack(">I", len(blob) + 8) + blob
    out.write_bytes(b"icns" + struct.pack(">I", len(chunks) + 8) + chunks)
    print(f"  {out.relative_to(HERE.parent)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inkscape", default="inkscape",
                        help="the Inkscape executable, if it is not on PATH")
    args = parser.parse_args()

    inkscape = shutil.which(args.inkscape)
    if inkscape is None:
        print(f"make-icons: cannot find {args.inkscape}; pass --inkscape PATH",
              file=sys.stderr)
        return 1

    print(f"Rendering {SVG.name} with {inkscape}")
    render(inkscape)
    write_ico(HERE / "animage.ico")
    write_icns(HERE / "animage.icns")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
