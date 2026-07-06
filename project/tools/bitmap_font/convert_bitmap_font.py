#!/usr/bin/env python3
"""Build a variable-width bitmap font atlas and JSON metrics from a TTF/OTF."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".c", ".txt", ".csv"}


def collect_characters(charset_files: list[Path], scan_paths: list[Path]) -> list[str]:
    text = "".join(chr(codepoint) for codepoint in range(32, 127))
    for path in charset_files:
        text += path.read_text(encoding="utf-8-sig")
    for root in scan_paths:
        files = [root] if root.is_file() else (
            path for path in root.rglob("*") if path.suffix.lower() in SOURCE_SUFFIXES
        )
        for path in files:
            text += path.read_text(encoding="utf-8-sig", errors="ignore")

    # Preserve first appearance so atlas generation is deterministic.
    return list(dict.fromkeys(char for char in text if char not in "\r\n\t"))


def next_power_of_two(value: int) -> int:
    return 1 << max(0, value - 1).bit_length()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--output-png", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--size", type=int, default=72)
    parser.add_argument("--padding", type=int, default=4)
    parser.add_argument("--atlas-width", type=int, default=2048)
    parser.add_argument("--variation", default="")
    parser.add_argument("--charset", action="append", default=[], type=Path)
    parser.add_argument("--scan", action="append", default=[], type=Path)
    args = parser.parse_args()

    if not args.font.is_file():
        raise SystemExit(f"Font not found: {args.font}")
    characters = collect_characters(args.charset, args.scan)
    font = ImageFont.truetype(str(args.font), args.size)
    if args.variation:
        try:
            font.set_variation_by_name(args.variation)
        except (AttributeError, OSError) as error:
            raise SystemExit(
                f"Font variation '{args.variation}' is unavailable: {error}") from error
    ascent, descent = font.getmetrics()

    measured: list[dict[str, int | str]] = []
    for character in characters:
        left, top, right, bottom = font.getbbox(character, anchor="ls")
        width = max(1, right - left)
        height = max(1, bottom - top)
        measured.append({
            "char": character,
            "codepoint": ord(character),
            "width": width,
            "height": height,
            "bearingX": left,
            "bearingY": top,
            "advance": max(1, round(font.getlength(character))),
        })

    padding = max(0, args.padding)
    atlas_width = next_power_of_two(max(128, args.atlas_width))
    x = padding
    y = padding
    row_height = 0
    for glyph in measured:
        packed_width = int(glyph["width"]) + padding * 2
        packed_height = int(glyph["height"]) + padding * 2
        if x + packed_width > atlas_width:
            x = padding
            y += row_height
            row_height = 0
        glyph["x"] = x + padding
        glyph["y"] = y + padding
        x += packed_width
        row_height = max(row_height, packed_height)
    atlas_height = next_power_of_two(y + row_height + padding)

    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(atlas)
    for glyph in measured:
        draw.text(
            (int(glyph["x"]) - int(glyph["bearingX"]),
             int(glyph["y"]) - int(glyph["bearingY"])),
            str(glyph["char"]), font=font, fill=(255, 255, 255, 255), anchor="ls")

    args.output_png.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(args.output_png)
    metadata = {
        "version": 1,
        "texture": args.output_png.name,
        "fontSize": args.size,
        "lineHeight": ascent + descent,
        "ascent": ascent,
        "glyphs": measured,
    }
    args.output_json.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {args.output_png} ({atlas_width}x{atlas_height}, {len(measured)} glyphs)")
    print(f"Generated {args.output_json}")


if __name__ == "__main__":
    main()
