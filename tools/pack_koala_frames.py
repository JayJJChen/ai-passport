#!/usr/bin/env python3
"""Build and verify the koala's 20-frame native RGB565A8 animation atlas."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
IMAGE_DIR = ROOT / "assets" / "images"
OUTPUT = IMAGE_DIR / "koala_frames.bin"
MANIFEST = IMAGE_DIR / "koala_frames.bin.manifest.json"
FRAME_SIZE = (144, 144)
FRAME_BYTES = FRAME_SIZE[0] * FRAME_SIZE[1] * 3
SOURCES = (
    ("wave", IMAGE_DIR / "koala-wave-source.png"),
    ("nod", IMAGE_DIR / "koala-nod-source.png"),
    ("point-right", IMAGE_DIR / "koala-point-source.png"),
    ("walk", IMAGE_DIR / "koala-walk-source.png"),
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sheet_frames(path: Path) -> list["Image.Image"]:
    from PIL import Image
    image = Image.open(path).convert("RGBA")
    if image.width % 2 or image.height % 2:
        raise ValueError(f"{path} must divide evenly into a 2x2 sheet")
    if image.getchannel("A").getextrema() != (0, 255):
        raise ValueError(f"{path} must contain transparent and opaque pixels")
    width, height = image.width // 2, image.height // 2
    frames = []
    for row in range(2):
        for column in range(2):
            cell = image.crop((column * width, row * height,
                               (column + 1) * width, (row + 1) * height))
            frames.append(cell.resize(FRAME_SIZE, Image.Resampling.LANCZOS))
    return frames


def load_frames() -> list["Image.Image"]:
    from PIL import Image
    groups = {name: sheet_frames(path) for name, path in SOURCES}
    point_left = [frame.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
                  for frame in groups["point-right"]]
    frames = groups["wave"] + groups["nod"] + groups["point-right"] + point_left + groups["walk"]
    if len(frames) != 20:
        raise AssertionError("koala atlas must contain exactly 20 frames")
    return frames


def encode_frame(frame: "Image.Image") -> bytes:
    pixels = list(frame.getdata())
    rgb = b"".join(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                   for r, g, b, _ in pixels)
    return rgb + bytes(alpha for _, _, _, alpha in pixels)


def build_bytes() -> bytes:
    return b"".join(encode_frame(frame) for frame in load_frames())


def manifest_bytes(atlas: bytes) -> bytes:
    document = {
        "format": "RGB565A8",
        "frame_bytes": FRAME_BYTES,
        "frame_count": 20,
        "frame_order": ["wave:0-3", "nod:4-7", "point-right:8-11",
                        "point-left-mirrored:12-15", "walk:16-19"],
        "height": FRAME_SIZE[1],
        "output_sha256": sha256(atlas),
        "sources": {path.name: sha256(path.read_bytes()) for _, path in SOURCES},
        "width": FRAME_SIZE[0],
    }
    return (json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def build() -> None:
    atlas = build_bytes()
    OUTPUT.write_bytes(atlas)
    MANIFEST.write_bytes(manifest_bytes(atlas))
    print(f"Wrote {len(atlas)} bytes, 20 frames, sha256={sha256(atlas)}")


def verify() -> None:
    if not OUTPUT.exists() or not MANIFEST.exists():
        raise SystemExit("koala animation atlas or manifest is missing; run the build command")
    atlas = OUTPUT.read_bytes()
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    expected_order = ["wave:0-3", "nod:4-7", "point-right:8-11",
                      "point-left-mirrored:12-15", "walk:16-19"]
    if (manifest.get("format") != "RGB565A8" or
            manifest.get("width") != FRAME_SIZE[0] or
            manifest.get("height") != FRAME_SIZE[1] or
            manifest.get("frame_count") != 20 or
            manifest.get("frame_bytes") != FRAME_BYTES or
            manifest.get("frame_order") != expected_order or
            len(atlas) != 20 * FRAME_BYTES or
            manifest.get("output_sha256") != sha256(atlas)):
        raise SystemExit("koala animation atlas metadata or payload is invalid; run the build command")
    source_hashes = manifest.get("sources")
    expected_sources = {path.name: sha256(path.read_bytes()) for _, path in SOURCES}
    if source_hashes != expected_sources:
        raise SystemExit("koala animation sources changed; run the build command")
    print(f"Koala atlas: PASS, {len(atlas)} bytes, sha256={sha256(atlas)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("build", "verify"))
    args = parser.parse_args()
    build() if args.command == "build" else verify()


if __name__ == "__main__":
    main()
