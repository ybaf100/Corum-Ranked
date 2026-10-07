#!/usr/bin/env python3
"""Check the actual generated font bundles, including stale-cache and atlas limits."""
import argparse
import json
import re
import struct
import zipfile
from pathlib import Path


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def charset(spec):
    result = set()
    for item in spec.split(","):
        parts = list(map(int, item.split("-")))
        result.update(range(parts[0], parts[-1] + 1))
    return result


def verify(path):
    expected = {name: 128 for name in (
        "CorumSans", "CorumSansMedium", "CorumSansSemiBold", "CorumSansBold", "CorumSansBlack")}
    expected["CorumSansCountdown"] = 256
    pixels_by_tier = [0, 0, 0]
    with zipfile.ZipFile(path) as package:
        manifest = json.loads(package.read("mod.json"))
        fonts = manifest["resources"]["fonts"]
        files = {}
        for name in package.namelist():
            base = Path(name).name
            if base.startswith("CorumSans") and base.endswith((".fnt", ".png")):
                if base in files:
                    raise ValueError(f"Duplicate generated font: {base}")
                files[base] = name
        for name, size in expected.items():
            spec = fonts[name]
            if spec["size"] != size:
                raise ValueError(f"Unexpected source size for {name}: {spec['size']}")
            for index, (suffix, factor) in enumerate((("", 4), ("-hd", 2), ("-uhd", 1))):
                stem = name + suffix
                lines = package.read(files[stem + ".fnt"]).decode("utf-8").splitlines()
                info, common = fields(lines[0]), fields(lines[1])
                png = package.read(files[stem + ".png"])
                if png[:8] != b"\x89PNG\r\n\x1a\n":
                    raise ValueError(f"Not PNG: {stem}")
                width, height = struct.unpack_from(">II", png, 16)
                if int(info["size"]) != size // factor:
                    raise ValueError(f"Stale/wrong raster size for {stem}: {info['size']}")
                if int(common["pages"]) != 1 or (int(common["scaleW"]), int(common["scaleH"])) != (width, height):
                    raise ValueError(f"Font/atlas mismatch for {stem}")
                if not (0 < width <= 4096 and 0 < height <= 4096):
                    raise ValueError(f"Atlas exceeds supported budget for {stem}: {width}x{height}")
                glyphs = [fields(line) for line in lines if line.startswith("char ")]
                missing = charset(spec["charset"]) - {int(glyph["id"]) for glyph in glyphs}
                if missing:
                    raise ValueError(f"Missing glyphs in {stem}: {sorted(missing)}")
                for glyph in glyphs:
                    x, y, w, h = (int(glyph[key]) for key in ("x", "y", "width", "height"))
                    if min(x, y, w, h) < 0 or x + w > width or y + h > height:
                        raise ValueError(f"Invalid glyph rectangle in {stem}: {glyph['id']}")
                pixels_by_tier[index] += width * height
        # RGBA estimate when every UI weight is loaded at a single quality tier.
        if max(pixels_by_tier) * 4 > 96 * 1024 * 1024:
            raise ValueError("Combined font atlas pixels exceed the 96 MiB RGBA budget")
    print(f"Verified 18 SD/HD/UHD font bundles in {path.name}; RGBA MiB per tier: " +
          ", ".join(f"{pixels * 4 / 2**20:.2f}" for pixels in pixels_by_tier))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    args = parser.parse_args()
    packages = sorted(args.package.glob("*.geode")) if args.package.is_dir() else [args.package]
    if not packages:
        parser.error("No .geode package found")
    for package_path in packages:
        verify(package_path)
