#!/usr/bin/env python3
"""Extract every active icon master from the approved proposal sheets."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageOps

from icon_manifest import (
    FAMILY_LABELS,
    REGISTRY_PATH,
    SHEETS_DIR,
    TEMPORARY_PNG_DIR,
    IconSpec,
    grouped_specs,
    load_specs,
)


HERE = Path(__file__).resolve().parent
SOURCE_CROPS_DIR = HERE / "source-crops"
REFERENCE_DIR = HERE / "reference"
CONTACT_SHEET = HERE / "reference-contact-sheet.png"
CONTACT_SHEETS_DIR = HERE / "reference-contact-sheets"
MASTER_SIZE = 512
SAFE_MARGIN = MASTER_SIZE // 10
ALPHA_FLOOR = 24


def cell_crop(sheet: Image.Image, index: int) -> Image.Image:
    column = (index - 1) % 4
    row = (index - 1) // 4
    x1, x2 = round(column * sheet.width / 4), round((column + 1) * sheet.width / 4)
    y1, y2 = round(row * sheet.height / 4), round((row + 1) * sheet.height / 4)
    cell = sheet.crop((x1, y1, x2, y2))

    # Keep only the large proposal glyph: this excludes the orange identifier,
    # size labels, and the two small raster previews.
    return cell.crop(
        (
            round(cell.width * 0.12),
            round(cell.height * 0.13),
            round(cell.width * 0.88),
            round(cell.height * 0.72),
        )
    )


def alpha_from_luminance(crop: Image.Image) -> Image.Image:
    if "A" in crop.getbands() and crop.getchannel("A").getextrema() != (255, 255):
        return crop.getchannel("A").point(lambda value: 0 if value < ALPHA_FLOOR else value)
    # Proposal glyphs are neutral white. Using the darkest RGB channel instead
    # of perceptual luminance rejects the orange cell identifier without a
    # shape-specific cleanup pass.
    red, green, blue = crop.convert("RGB").split()
    luminance = ImageChops.darker(red, ImageChops.darker(green, blue))
    black_point, white_point = 72, 220
    alpha = luminance.point(
        lambda value: 0
        if value <= black_point
        else 255
        if value >= white_point
        else round((value - black_point) * 255 / (white_point - black_point))
    )
    # Discard sub-10% opacity residue from proposal-cell dividers. This is
    # below visible glyph coverage and prevents the tracer from solidifying it.
    return alpha.point(lambda value: 0 if value < ALPHA_FLOOR else value)


def clean_master(crop: Image.Image) -> Image.Image:
    alpha = alpha_from_luminance(crop)
    bounds = alpha.getbbox()
    if bounds is None:
        raise RuntimeError("proposal cell contains no glyph")
    alpha = alpha.crop(bounds)
    available = MASTER_SIZE - 2 * SAFE_MARGIN
    alpha = ImageOps.contain(alpha, (available, available), Image.Resampling.LANCZOS)
    canvas = Image.new("L", (MASTER_SIZE, MASTER_SIZE), 0)
    canvas.paste(alpha, ((MASTER_SIZE - alpha.width) // 2, (MASTER_SIZE - alpha.height) // 2))
    master = Image.new("RGBA", canvas.size, "white")
    master.putalpha(canvas)
    return master


def label_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    path = Path("C:/Windows/Fonts/segoeui.ttf")
    return ImageFont.truetype(path, size) if path.exists() else ImageFont.load_default()


def checkerboard(size: tuple[int, int], tile: int = 12) -> Image.Image:
    image = Image.new("RGBA", size, (22, 24, 28, 255))
    draw = ImageDraw.Draw(image)
    for y in range(0, size[1], tile):
        for x in range(0, size[0], tile):
            if (x // tile + y // tile) % 2:
                draw.rectangle((x, y, x + tile - 1, y + tile - 1), fill=(31, 34, 39, 255))
    return image


def create_contact_sheet(specs: tuple[IconSpec, ...], destination: Path, title_text: str) -> None:
    width, row_height, header_height = 930, 150, 72
    sheet = Image.new("RGBA", (width, header_height + row_height * len(specs) + 12), (7, 8, 10, 255))
    draw = ImageDraw.Draw(sheet)
    title, label, muted = label_font(17), label_font(15), label_font(13)
    draw.text((18, 10), title_text, font=title, fill=(235, 237, 240, 255))
    for text, x in (("Native source crop", 220), ("Transparent 512 master", 500), ("16 / 14 / 12 px", 748)):
        draw.text((x, 44), text, font=muted, fill=(145, 150, 160, 255))

    for row, spec in enumerate(specs):
        name = spec.name
        top = header_height + row * row_height
        if row % 2 == 0:
            draw.rounded_rectangle((10, top + 4, width - 10, top + row_height - 4), radius=8, fill=(18, 20, 24, 255))
        draw.text((18, top + 60), name, font=label, fill=(235, 237, 240, 255))

        with Image.open(SOURCE_CROPS_DIR / f"{name}.png") as source:
            source = ImageOps.contain(source.convert("RGBA"), (210, 126), Image.Resampling.LANCZOS)
            sheet.alpha_composite(source, (220 + (210 - source.width) // 2, top + 12 + (126 - source.height) // 2))

        with Image.open(REFERENCE_DIR / f"{name}.png") as master:
            preview = ImageOps.contain(master.convert("RGBA"), (126, 126), Image.Resampling.LANCZOS)
            background = checkerboard((150, 134))
            background.alpha_composite(preview, ((150 - preview.width) // 2, (134 - preview.height) // 2))
            sheet.alpha_composite(background, (500, top + 8))

            x = 748
            for size in (16, 14, 12):
                tiny = master.resize((size, size), Image.Resampling.LANCZOS)
                proof = tiny.resize((48, 48), Image.Resampling.NEAREST)
                sheet.alpha_composite(proof, (x, top + 48))
                x += 58

    destination.parent.mkdir(parents=True, exist_ok=True)
    sheet.convert("RGB").save(destination, quality=96)


def create_overview(specs: tuple[IconSpec, ...]) -> None:
    columns, tile_width, tile_height, header = 5, 190, 142, 50
    rows = (len(specs) + columns - 1) // columns
    sheet = Image.new("RGBA", (columns * tile_width, header + rows * tile_height), (7, 8, 10, 255))
    draw = ImageDraw.Draw(sheet)
    draw.text((18, 14), f"{len(specs)} clean icon masters", font=label_font(18), fill=(238, 239, 242, 255))
    for index, spec in enumerate(specs):
        column, row = index % columns, index // columns
        left, top = column * tile_width, header + row * tile_height
        if (column + row) % 2 == 0:
            draw.rectangle((left, top, left + tile_width - 1, top + tile_height - 1), fill=(18, 20, 24, 255))
        draw.text((left + 10, top + 9), spec.name, font=label_font(12), fill=(235, 237, 240, 255))
        draw.text((left + 150, top + 9), spec.reference, font=label_font(11), fill=(145, 150, 160, 255))
        with Image.open(REFERENCE_DIR / f"{spec.name}.png") as master:
            preview = ImageOps.contain(master.convert("RGBA"), (82, 82), Image.Resampling.LANCZOS)
            sheet.alpha_composite(preview, (left + 24 + (82 - preview.width) // 2, top + 43 + (82 - preview.height) // 2))
            y = top + 47
            for size in (16, 14, 12):
                tiny = master.resize((size, size), Image.Resampling.LANCZOS)
                proof = tiny.resize((32, 32), Image.Resampling.NEAREST)
                sheet.alpha_composite(proof, (left + 128, y))
                y += 34
    sheet.convert("RGB").save(CONTACT_SHEET, quality=96)


def remove_stale_pngs(directory: Path, expected_names: set[str]) -> None:
    for path in directory.glob("*.png"):
        if path.stem not in expected_names:
            path.unlink()


def write(sheets_dir: Path, registry_path: Path) -> None:
    specs = load_specs(registry_path)
    expected_names = {spec.name for spec in specs}
    SOURCE_CROPS_DIR.mkdir(parents=True, exist_ok=True)
    REFERENCE_DIR.mkdir(parents=True, exist_ok=True)
    remove_stale_pngs(SOURCE_CROPS_DIR, expected_names)
    remove_stale_pngs(REFERENCE_DIR, expected_names)
    sheets: dict[str, Image.Image] = {}
    try:
        for spec in specs:
            if spec.sheet_name is None:
                canonical_path = TEMPORARY_PNG_DIR / f"{spec.name}.png"
                if not canonical_path.exists():
                    raise FileNotFoundError(f"canonical icon source not found: {canonical_path}")
                with Image.open(canonical_path) as image:
                    crop = image.convert("RGBA")
            else:
                sheet_path = sheets_dir / spec.sheet_name
                if not sheet_path.exists():
                    raise FileNotFoundError(f"approved proposal sheet not found: {sheet_path}")
                if spec.sheet_name not in sheets:
                    sheets[spec.sheet_name] = Image.open(sheet_path).convert("RGB")
                crop = cell_crop(sheets[spec.sheet_name], spec.cell)
            crop.save(SOURCE_CROPS_DIR / f"{spec.name}.png")
            clean_master(crop).save(REFERENCE_DIR / f"{spec.name}.png")
    finally:
        for sheet in sheets.values():
            sheet.close()
    create_overview(specs)
    for family, family_specs in grouped_specs(specs):
        create_contact_sheet(
            family_specs,
            CONTACT_SHEETS_DIR / f"{family.lower()}-{FAMILY_LABELS[family].lower().replace(' ', '-')}.png",
            f"{FAMILY_LABELS[family]} — clean reference masters",
        )
    check(registry_path)
    print(f"Prepared {len(specs)} clean reference masters")
    print(f"Overview: {CONTACT_SHEET}")


def check(registry_path: Path = REGISTRY_PATH) -> None:
    specs = load_specs(registry_path)
    expected_names = {spec.name for spec in specs}
    actual_names = {path.stem for path in REFERENCE_DIR.glob("*.png")}
    if actual_names != expected_names:
        missing = sorted(expected_names - actual_names)
        stale = sorted(actual_names - expected_names)
        raise RuntimeError(f"reference set mismatch; missing={missing}, stale={stale}")
    for spec in specs:
        path = REFERENCE_DIR / f"{spec.name}.png"
        if not path.exists():
            raise RuntimeError(f"missing clean master: {path}")
        with Image.open(path) as image:
            if image.mode != "RGBA" or image.size != (MASTER_SIZE, MASTER_SIZE):
                raise RuntimeError(f"{spec.name}: expected a {MASTER_SIZE}px RGBA master")
            bounds = image.getchannel("A").getbbox()
            if bounds is None:
                raise RuntimeError(f"{spec.name}: master is empty")
            if min(bounds[:2]) < SAFE_MARGIN - 1 or max(bounds[2:]) > MASTER_SIZE - SAFE_MARGIN + 1:
                raise RuntimeError(f"{spec.name}: master escapes the 10% safe area: {bounds}")
    print(f"Checked {len(specs)} clean reference masters")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("write", "check"))
    parser.add_argument("--sheets-dir", type=Path, default=SHEETS_DIR)
    parser.add_argument("--registry", type=Path, default=REGISTRY_PATH)
    args = parser.parse_args()
    try:
        write(args.sheets_dir, args.registry) if args.command == "write" else check(args.registry)
    except (FileNotFoundError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
