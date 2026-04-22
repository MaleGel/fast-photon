"""
Atlas baker for fast-photon (multi-page).

Scans a directory of source PNG files and packs them into one or more
fixed-size texture atlas pages. Emits one PNG per page plus a single JSON
manifest the engine runtime reads to look up each sprite's page + UV rect.

Usage:
    python bake.py <input_dir> <output_dir> [--page-size N] [--max-pages N]

Sprite IDs are derived from file paths relative to <input_dir>, without the
.png extension. Subdirectories appear as forward-slash separators.
Example: input_dir/units/warrior.png → sprite id "units/warrior".
"""

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List

from PIL import Image
from rectpack import newPacker


DEFAULT_PAGE_SIZE = 1024    # one page is DEFAULT_PAGE_SIZE x DEFAULT_PAGE_SIZE
DEFAULT_MAX_PAGES = 16      # safety ceiling — a bad input won't spawn hundreds


@dataclass
class Source:
    """One PNG discovered on disk, plus its intended sprite id."""
    sprite_id: str
    path: Path
    image: Image.Image

    @property
    def width(self) -> int:  return self.image.width

    @property
    def height(self) -> int: return self.image.height


def collect_sources(input_dir: Path) -> List[Source]:
    """Walk input_dir, return a Source per .png. Ids use forward slashes."""
    sources: List[Source] = []
    for png_path in sorted(input_dir.rglob("*.png")):
        rel       = png_path.relative_to(input_dir).with_suffix("")
        sprite_id = rel.as_posix()
        image     = Image.open(png_path).convert("RGBA")
        sources.append(Source(sprite_id, png_path, image))
        print(f"  found: {sprite_id}  ({image.width}x{image.height})")
    return sources


def validate_sizes(sources: List[Source], page_size: int) -> bool:
    """Every sprite must fit within a single page."""
    ok = True
    for s in sources:
        if s.width > page_size or s.height > page_size:
            print(f"[bake] ERROR: sprite '{s.sprite_id}' "
                  f"({s.width}x{s.height}) exceeds page size {page_size}")
            ok = False
    return ok


def pack_sprites(sources: List[Source], page_size: int, max_pages: int):
    """Run rectpack with multiple fixed-size bins. Returns the packer."""
    packer = newPacker(rotation=False)  # rotation=False keeps UVs intuitive
    for idx, src in enumerate(sources):
        packer.add_rect(src.width, src.height, rid=idx)
    for _ in range(max_pages):
        packer.add_bin(page_size, page_size)
    packer.pack()
    return packer


def bake(input_dir: Path, output_dir: Path, page_size: int, max_pages: int) -> int:
    print(f"[bake] scanning {input_dir}")
    sources = collect_sources(input_dir)
    if not sources:
        print("[bake] no .png files found — nothing to do")
        return 0

    if not validate_sizes(sources, page_size):
        return 1

    packer = pack_sprites(sources, page_size, max_pages)

    # ── Check that everything was packed ─────────────────────────────────────
    packed_count = sum(len(bin) for bin in packer)
    if packed_count != len(sources):
        print(f"[bake] ERROR: only {packed_count}/{len(sources)} sprites fit "
              f"within {max_pages} pages of {page_size}x{page_size}. "
              f"Increase --page-size or --max-pages.")
        return 1

    # ── Compose page images + entries ────────────────────────────────────────
    output_dir.mkdir(parents=True, exist_ok=True)

    pages_manifest = []
    sprite_entries = []

    for page_index, bin in enumerate(packer):
        if len(bin) == 0:
            continue  # rectpack may leave trailing empty bins — skip them

        page_image = Image.new("RGBA", (page_size, page_size), (0, 0, 0, 0))
        for rect in bin:
            src = sources[rect.rid]
            page_image.paste(src.image, (rect.x, rect.y))
            sprite_entries.append({
                "name": src.sprite_id,
                "page": page_index,
                "x":    rect.x,
                "y":    rect.y,
                "w":    rect.width,
                "h":    rect.height,
            })

        image_name = f"atlas_{page_index}.png"
        page_image.save(output_dir / image_name, "PNG")
        pages_manifest.append({ "index": page_index, "image": image_name })
        print(f"[bake] wrote {image_name}  ({len(bin)} sprites)")

    # Stable output order for clean diffs.
    sprite_entries.sort(key=lambda e: (e["page"], e["name"]))

    manifest = {
        "page_size": page_size,
        "pages":     pages_manifest,
        "sprites":   sprite_entries,
    }
    json_path = output_dir / "atlas.json"
    with json_path.open("w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    print(f"[bake] wrote {json_path}  "
          f"({len(pages_manifest)} pages, {len(sprite_entries)} sprites)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Pack PNG sprites into multi-page texture atlases.")
    ap.add_argument("input_dir",    type=Path,                         help="directory containing source PNGs")
    ap.add_argument("output_dir",   type=Path,                         help="directory to write atlas_*.png + atlas.json")
    ap.add_argument("--page-size",  type=int, default=DEFAULT_PAGE_SIZE, help=f"page side in pixels (default {DEFAULT_PAGE_SIZE})")
    ap.add_argument("--max-pages",  type=int, default=DEFAULT_MAX_PAGES, help=f"safety ceiling on page count (default {DEFAULT_MAX_PAGES})")
    args = ap.parse_args()

    if not args.input_dir.is_dir():
        print(f"[bake] ERROR: input dir does not exist: {args.input_dir}")
        return 1

    return bake(args.input_dir, args.output_dir, args.page_size, args.max_pages)


if __name__ == "__main__":
    sys.exit(main())
