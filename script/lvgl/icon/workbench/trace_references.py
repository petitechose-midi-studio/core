#!/usr/bin/env python3
"""Trace every active monochrome master and render review proofs."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

from icon_manifest import FAMILY_LABELS, REGISTRY_PATH, IconSpec, grouped_specs, load_specs

try:
    import potrace
except ImportError:  # pragma: no cover - actionable local setup error
    potrace = None


HERE = Path(__file__).resolve().parent
REFERENCE_DIR = HERE / "reference"
TRACE_DIR = HERE / "traced" / "potrace"
PROOF_DIR = HERE / "generated" / "potrace"
CONTACT_SHEET = HERE / "traced" / "potrace-contact-sheet.png"
CONTACT_SHEETS_DIR = HERE / "traced" / "potrace-contact-sheets"
QUALITY_REPORT = HERE / "traced" / "potrace-quality.tsv"
INK = (255, 148, 24, 255)
MASTER_SIZE = 512
SVG_VIEWBOX = 100


def number(value: float) -> str:
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def point(value: object) -> tuple[float, float]:
    scale = SVG_VIEWBOX / MASTER_SIZE
    return float(value.x) * scale, float(value.y) * scale  # type: ignore[attr-defined]


def trace_path(master: Image.Image) -> str:
    if potrace is None:
        raise RuntimeError(
            "missing potracer; install with: uv pip install --python "
            "C:\\Users\\miu-lab\\ms-dev-env\\.venv\\Scripts\\python.exe potracer"
        )

    # Potrace traces dark shapes. The clean master stores the glyph in alpha,
    # so invert it into a white-background/black-glyph bitmap first.
    bitmap = potrace.Bitmap(ImageOps.invert(master.getchannel("A")))
    curves = bitmap.trace(turdsize=16, alphamax=1.0, opticurve=True, opttolerance=0.2)
    commands: list[str] = []
    for curve in curves:
        start_x, start_y = point(curve.start_point)
        commands.append(f"M{number(start_x)} {number(start_y)}")
        for segment in curve:
            end_x, end_y = point(segment.end_point)
            if segment.is_corner:
                corner_x, corner_y = point(segment.c)
                commands.append(
                    f"L{number(corner_x)} {number(corner_y)} "
                    f"L{number(end_x)} {number(end_y)}"
                )
            else:
                c1_x, c1_y = point(segment.c1)
                c2_x, c2_y = point(segment.c2)
                commands.append(
                    f"C{number(c1_x)} {number(c1_y)} "
                    f"{number(c2_x)} {number(c2_y)} "
                    f"{number(end_x)} {number(end_y)}"
                )
        commands.append("Z")
    if not commands:
        raise RuntimeError("Potrace returned no path")
    return " ".join(commands)


def svg_document(path_data: str) -> str:
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">\n'
        f'  <path d="{path_data}" fill="#000000" fill-rule="evenodd"/>\n'
        "</svg>\n"
    )


def trace_spec(spec: IconSpec) -> tuple[str, str]:
    with Image.open(REFERENCE_DIR / f"{spec.name}.png") as master:
        return spec.name, svg_document(trace_path(master.convert("RGBA")))


def find_inkscape() -> str:
    resolved = shutil.which("inkscape")
    if resolved:
        return resolved
    installed = Path("C:/Program Files/Inkscape/bin/inkscape.exe")
    if installed.exists():
        return str(installed)
    raise RuntimeError("Inkscape is required to render the trace proofs")


def render(inkscape: str, source: Path, destination: Path, size: int) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [
            inkscape,
            str(source),
            "--export-area-page",
            "--export-background-opacity=0",
            f"--export-width={size}",
            f"--export-height={size}",
            f"--export-filename={destination}",
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode or not destination.exists():
        details = (result.stderr or result.stdout).strip()
        raise RuntimeError(f"failed to render {source.name}: {details}")


def render_task(task: tuple[str, Path, Path, int]) -> None:
    render(*task)


def font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    path = Path("C:/Windows/Fonts/segoeui.ttf")
    return ImageFont.truetype(path, size) if path.exists() else ImageFont.load_default()


def colored_alpha(image: Image.Image) -> Image.Image:
    result = Image.new("RGBA", image.size, INK)
    result.putalpha(image.convert("RGBA").getchannel("A"))
    return result


def centered(sheet: Image.Image, image: Image.Image, box: tuple[int, int, int, int]) -> None:
    width, height = box[2] - box[0], box[3] - box[1]
    fitted = ImageOps.contain(image.convert("RGBA"), (width, height), Image.Resampling.LANCZOS)
    sheet.alpha_composite(
        fitted,
        (box[0] + (width - fitted.width) // 2, box[1] + (height - fitted.height) // 2),
    )


def contact_sheet(specs: tuple[IconSpec, ...], destination: Path, title: str) -> None:
    header, row_height, width = 72, 146, 900
    sheet = Image.new("RGBA", (width, header + row_height * len(specs) + 12), (7, 8, 10, 255))
    draw = ImageDraw.Draw(sheet)
    draw.text((18, 10), title, font=font(17), fill=(238, 239, 242, 255))
    for label, x in (("Clean master", 196), ("Potrace", 360), ("16 px", 540), ("14 px", 640), ("12 px", 740)):
        draw.text((x, 44), label, font=font(13), fill=(145, 150, 160, 255))

    for row, spec in enumerate(specs):
        top = header + row * row_height
        if row % 2 == 0:
            draw.rounded_rectangle((10, top + 4, width - 10, top + row_height - 4), 8, fill=(18, 20, 24, 255))
        draw.text((18, top + 52), spec.name, font=font(15), fill=(235, 237, 240, 255))
        draw.text((18, top + 76), spec.reference, font=font(12), fill=(145, 150, 160, 255))
        with Image.open(REFERENCE_DIR / f"{spec.name}.png") as image:
            centered(sheet, colored_alpha(image), (188, top + 10, 318, top + 136))
        with Image.open(PROOF_DIR / "large" / f"{spec.name}.png") as image:
            centered(sheet, colored_alpha(image), (342, top + 10, 472, top + 136))
        for size, x in ((16, 520), (14, 620), (12, 720)):
            with Image.open(PROOF_DIR / str(size) / f"{spec.name}.png") as image:
                pixel = colored_alpha(image).resize((80, 80), Image.Resampling.NEAREST)
                centered(sheet, pixel, (x, top + 32, x + 80, top + 112))

    destination.parent.mkdir(parents=True, exist_ok=True)
    sheet.convert("RGB").save(destination, quality=96)


def overview_sheet(specs: tuple[IconSpec, ...]) -> None:
    columns, tile_width, tile_height, header = 5, 200, 154, 52
    rows = (len(specs) + columns - 1) // columns
    sheet = Image.new("RGBA", (columns * tile_width, header + rows * tile_height), (7, 8, 10, 255))
    draw = ImageDraw.Draw(sheet)
    draw.text((18, 15), f"{len(specs)} traced production candidates", font=font(18), fill=(238, 239, 242, 255))
    for index, spec in enumerate(specs):
        column, row = index % columns, index // columns
        left, top = column * tile_width, header + row * tile_height
        if (column + row) % 2 == 0:
            draw.rectangle((left, top, left + tile_width - 1, top + tile_height - 1), fill=(18, 20, 24, 255))
        draw.text((left + 9, top + 8), spec.name, font=font(12), fill=(235, 237, 240, 255))
        draw.text((left + 165, top + 8), spec.reference, font=font(11), fill=(145, 150, 160, 255))
        with Image.open(PROOF_DIR / "large" / f"{spec.name}.png") as image:
            centered(sheet, colored_alpha(image), (left + 17, top + 38, left + 111, top + 132))
        y = top + 42
        for size in (16, 14, 12):
            with Image.open(PROOF_DIR / str(size) / f"{spec.name}.png") as image:
                pixel = colored_alpha(image).resize((32, 32), Image.Resampling.NEAREST)
                sheet.alpha_composite(pixel, (left + 140, y))
                y += 34
    CONTACT_SHEET.parent.mkdir(parents=True, exist_ok=True)
    sheet.convert("RGB").save(CONTACT_SHEET, quality=96)


def remove_stale(directory: Path, suffix: str, expected_names: set[str]) -> None:
    if not directory.exists():
        return
    for path in directory.glob(f"*{suffix}"):
        if path.stem not in expected_names:
            path.unlink()


def binary_alpha(path: Path, size: int = 512) -> list[bool]:
    with Image.open(path) as image:
        alpha = image.convert("RGBA").getchannel("A")
        if alpha.size != (size, size):
            alpha = alpha.resize((size, size), Image.Resampling.LANCZOS)
        return [value >= 128 for value in alpha.tobytes()]


def fidelity(spec: IconSpec) -> float:
    reference = binary_alpha(REFERENCE_DIR / f"{spec.name}.png")
    traced = binary_alpha(PROOF_DIR / "large" / f"{spec.name}.png")
    intersection = sum(left and right for left, right in zip(reference, traced))
    union = sum(left or right for left, right in zip(reference, traced))
    return intersection / union if union else 0.0


def write(registry_path: Path = REGISTRY_PATH) -> None:
    specs = load_specs(registry_path)
    expected_names = {spec.name for spec in specs}
    inkscape = find_inkscape()
    TRACE_DIR.mkdir(parents=True, exist_ok=True)
    for directory in (TRACE_DIR, *(PROOF_DIR / name for name in ("large", "16", "14", "12"))):
        directory.mkdir(parents=True, exist_ok=True)
        remove_stale(directory, ".svg" if directory == TRACE_DIR else ".png", expected_names)

    workers = min(8, os.cpu_count() or 1)
    with ProcessPoolExecutor(max_workers=workers) as executor:
        traced = dict(executor.map(trace_spec, specs))
    for spec in specs:
        (TRACE_DIR / f"{spec.name}.svg").write_text(traced[spec.name], encoding="utf-8", newline="\n")

    render_tasks: list[tuple[str, Path, Path, int]] = []
    for spec in specs:
        source = TRACE_DIR / f"{spec.name}.svg"
        render_tasks.append((inkscape, source, PROOF_DIR / "large" / f"{spec.name}.png", 512))
        for size in (16, 14, 12):
            render_tasks.append((inkscape, source, PROOF_DIR / str(size) / f"{spec.name}.png", size))
    with ThreadPoolExecutor(max_workers=workers) as executor:
        list(executor.map(render_task, render_tasks))

    overview_sheet(specs)
    for family, family_specs in grouped_specs(specs):
        contact_sheet(
            family_specs,
            CONTACT_SHEETS_DIR / f"{family.lower()}-{FAMILY_LABELS[family].lower().replace(' ', '-')}.png",
            f"{FAMILY_LABELS[family]} — reference-to-vector quality gate",
        )
    scores = [(spec, fidelity(spec)) for spec in specs]
    QUALITY_REPORT.write_text(
        "name\treference\tfidelity\tsvg_bytes\n"
        + "".join(
            f"{spec.name}\t{spec.reference}\t{score:.4f}\t{(TRACE_DIR / f'{spec.name}.svg').stat().st_size}\n"
            for spec, score in scores
        ),
        encoding="utf-8",
        newline="\n",
    )
    check(registry_path)
    print(f"Traced {len(specs)} clean masters with Potrace using {workers} workers")
    print(f"Overview: {CONTACT_SHEET}")


def check(registry_path: Path = REGISTRY_PATH) -> None:
    specs = load_specs(registry_path)
    expected_names = {spec.name for spec in specs}
    actual_names = {path.stem for path in TRACE_DIR.glob("*.svg")}
    if actual_names != expected_names:
        raise RuntimeError(
            f"trace set mismatch; missing={sorted(expected_names - actual_names)}, "
            f"stale={sorted(actual_names - expected_names)}"
        )
    for size in ("large", "16", "14", "12"):
        proof_names = {path.stem for path in (PROOF_DIR / size).glob("*.png")}
        if proof_names != expected_names:
            raise RuntimeError(
                f"{size}px proof set mismatch; missing={sorted(expected_names - proof_names)}, "
                f"stale={sorted(proof_names - expected_names)}"
            )
    scores: list[float] = []
    for spec in specs:
        svg = TRACE_DIR / f"{spec.name}.svg"
        if svg.stat().st_size < 100:
            raise RuntimeError(f"empty trace: {svg}")
        root = ET.parse(svg).getroot()
        if root.attrib.get("viewBox") != "0 0 100 100":
            raise RuntimeError(f"{spec.name}: expected a 100 x 100 SVG viewBox")
        paths = list(root)
        if len(paths) != 1 or paths[0].attrib.get("fill") != "#000000":
            raise RuntimeError(f"{spec.name}: expected one neutral black path")
        with Image.open(PROOF_DIR / "large" / f"{spec.name}.png") as image:
            bounds = image.convert("RGBA").getchannel("A").point(lambda value: 255 if value >= 8 else 0).getbbox()
            if bounds is None or min(bounds[:2]) < 50 or max(bounds[2:]) > 462:
                raise RuntimeError(f"{spec.name}: traced glyph escapes the 10% safe area: {bounds}")
        for size in (16, 14, 12):
            proof = PROOF_DIR / str(size) / f"{spec.name}.png"
            if not proof.exists():
                raise RuntimeError(f"missing proof: {proof}")
            with Image.open(proof) as image:
                if image.convert("RGBA").getchannel("A").getbbox() is None:
                    raise RuntimeError(f"empty proof: {proof}")
        score = fidelity(spec)
        if score < 0.98:
            raise RuntimeError(f"{spec.name}: trace fidelity is only {score:.2%}")
        scores.append(score)
    print(
        f"Checked {len(specs)} traces and small-size proofs; "
        f"fidelity min={min(scores):.2%} avg={sum(scores) / len(scores):.2%}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("write", "check"))
    parser.add_argument("--registry", type=Path, default=REGISTRY_PATH)
    args = parser.parse_args()
    try:
        write(args.registry) if args.command == "write" else check(args.registry)
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
