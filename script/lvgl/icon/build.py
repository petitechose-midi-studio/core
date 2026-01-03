#!/usr/bin/env python3
"""
Icon Font Builder - SVG → TTF + C++ header with incremental builds
"""

import sys
import os

# Force UTF-8 output on Windows
if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')  # type: ignore[union-attr]
    sys.stderr.reconfigure(encoding='utf-8')  # type: ignore[union-attr]
    os.environ['PYTHONIOENCODING'] = 'utf-8'

import subprocess
import hashlib
import json
import re
from pathlib import Path
from datetime import datetime
from typing import cast
from xml.etree import ElementTree as ET

# Find project root by looking for platformio.ini
def find_project_root() -> Path:
    """Find project root by looking for platformio.ini in parent directories."""
    current = Path(__file__).resolve().parent
    while current != current.parent:
        if (current / "platformio.ini").exists():
            return current
        current = current.parent
    raise FileNotFoundError("Project root not found (no platformio.ini)")

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = find_project_root()

sys.path.insert(0, str(SCRIPT_DIR))
from config import *

# === Colors & Logging ===

class C:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    BOLD = '\033[1m'
    DIM = '\033[2m'
    NC = '\033[0m'

def log(msg: str) -> None:      print(f"{C.BLUE}●{C.NC} {msg}")
def success(msg: str) -> None:  print(f"  {C.GREEN}✓{C.NC} {msg}")
def warn(msg: str) -> None:     print(f"  {C.YELLOW}⚠{C.NC} {msg}")
def error(msg: str) -> None:    print(f"{C.RED}✗{C.NC} {msg}", file=sys.stderr); sys.exit(1)
def added(msg: str) -> None:    print(f"  {C.GREEN}+{C.NC} {C.DIM}{msg}{C.NC}")
def modified(msg: str) -> None: print(f"  {C.YELLOW}~{C.NC} {C.DIM}{msg}{C.NC}")
def removed(msg: str) -> None:  print(f"  {C.RED}-{C.NC} {C.DIM}{msg}{C.NC}")
def skip(msg: str) -> None:     print(f"  {C.DIM}  {msg}{C.NC}")


# === Hash & Cache ===

def file_hash(path: Path) -> str:
    return hashlib.md5(path.read_bytes()).hexdigest()[:12]

def load_hashes(cache_dir: Path) -> dict[str, str]:
    hash_file = cache_dir / "hashes.json"
    if hash_file.exists():
        return json.loads(hash_file.read_text())
    return {}

def save_hashes(cache_dir: Path, hashes: dict[str, str]) -> None:
    (cache_dir / "hashes.json").write_text(json.dumps(hashes, indent=2))


# === SVG Cleaning ===

def inkscape_run(src: Path, dst: Path, actions: list[str] | None = None, fit: bool = False) -> bool:
    cmd = [INKSCAPE, str(src), "--export-type=svg", "--export-plain-svg", f"--export-filename={dst}"]
    if fit:
        cmd.append("--export-area-drawing")
    if actions:
        cmd.append(f"--actions={';'.join(actions)}")
    subprocess.run(cmd, capture_output=True)
    return dst.exists()

def xml_cleanup(src: Path, dst: Path):
    ET.register_namespace('', 'http://www.w3.org/2000/svg')
    tree = ET.parse(src)
    root = tree.getroot()

    def tag_name(e: ET.Element) -> str: return e.tag.split('}')[-1] if '}' in e.tag else e.tag
    def remove(e: ET.Element) -> None:
        for p in root.iter():
            if e in list(p): p.remove(e); return

    for elem in list(root.iter()):
        tag = tag_name(elem)
        style, fill = elem.get('style', ''), elem.get('fill', '')
        if ('fill:none' in style or fill == 'none') and 'stroke:' not in style:
            remove(elem)
        elif tag in ('metadata',) or any(x in elem.tag for x in ['sodipodi', 'inkscape', 'rdf']):
            remove(elem)
        elif tag in ('defs', 'g') and not list(elem):
            remove(elem)

    for elem in root.iter():
        if tag_name(elem) == 'path':
            for attr in ['style', 'fill']:
                if attr in elem.attrib: del elem.attrib[attr]
            elem.set('fill-rule', 'nonzero')

    for attr in list(root.attrib.keys()):
        if any(x in attr for x in ['sodipodi', 'inkscape', 'rdf', 'dc', 'cc', 'style']):
            del root.attrib[attr]

    tree.write(dst, encoding='unicode', xml_declaration=True)

def get_original_viewbox(svg_path: Path) -> tuple[float, float, float, float] | None:
    """Get original viewBox if present."""
    try:
        tree = ET.parse(svg_path)
        root = tree.getroot()
        vb = root.get('viewBox', '').split()
        if len(vb) == 4:
            return cast(tuple[float, float, float, float], tuple(map(float, vb)))
        return None
    except:
        return None

def square_and_center(src: Path, dst: Path, original_viewbox: tuple[float, float, float, float] | None):
    """
    If original viewBox is square: keep it (author did the centering).
    Otherwise: compute bbox, add 10% height padding, center in square.

    IMPORTANT: FontForge ignores viewBox, so we must physically transform the paths
    to center them, not just adjust viewBox.
    """
    tree = ET.parse(src)
    root = tree.getroot()

    # Check if original was already square
    if original_viewbox:
        _, _, ow, oh = original_viewbox
        if abs(ow - oh) < 0.01:  # Square (with tolerance)
            # Keep original viewBox
            root.set('viewBox', f'{original_viewbox[0]} {original_viewbox[1]} {ow} {oh}')
            tree.write(dst, encoding='unicode', xml_declaration=True)
            return

    # Get bbox from inkscape fit
    vb = root.get('viewBox', '').split()
    if len(vb) == 4:
        vx, vy, vw, vh = map(float, vb)
    else:
        vx, vy = 0, 0
        vw = float(root.get('width', '100').replace('px', ''))
        vh = float(root.get('height', '100').replace('px', ''))

    # 1. Padding = 10% of bbox height
    pad = vh * PADDING_PERCENT

    # 2. Square = max dimension + padding on both sides
    size = max(vw, vh) + 2 * pad

    # 3. Calculate translation to center content in square starting at (0,0)
    # Content currently at (vx, vy) with size (vw, vh)
    # We want it centered in a square of size `size` starting at (0, 0)
    # Center of square = (size/2, size/2)
    # Center of content should be at square center
    # So content top-left should be at: (size/2 - vw/2, size/2 - vh/2)
    # Translation needed: new_pos - current_pos
    tx = (size / 2 - vw / 2) - vx
    ty = (size / 2 - vh / 2) - vy

    # 4. Wrap all content in a group with transform
    ns = '{http://www.w3.org/2000/svg}'
    g = ET.Element(f'{ns}g')  # type: ignore[arg-type]
    g.set('transform', f'translate({tx:.4f}, {ty:.4f})')

    # Move all children into the group
    for child in list(root):
        root.remove(child)
        g.append(child)
    root.append(g)

    # 5. Set viewBox to square starting at origin
    root.set('viewBox', f'0 0 {size:.4f} {size:.4f}')
    root.set('width', f'{size:.4f}')
    root.set('height', f'{size:.4f}')

    tree.write(dst, encoding='unicode', xml_declaration=True)

def clean_svg(src: Path, dst: Path, temp_dir: Path) -> bool:
    t1, t2 = temp_dir / "1.svg", temp_dir / "2.svg"

    # Check if original has viewBox
    original_viewbox = get_original_viewbox(src)

    # Convert strokes to paths
    if not inkscape_run(src, t1, ["select-all", "object-to-path", "select-all", "object-stroke-to-path", "export-plain-svg", "export-do"]):
        return False

    # Clean up XML
    try:
        xml_cleanup(t1, t2)
    except:
        return False

    if original_viewbox:
        # Has viewBox → keep as-is (author defined proportions and centering)
        import shutil
        shutil.copy(t2, dst)
    else:
        # No viewBox → fit to content, then add padding and center
        t3 = temp_dir / "3.svg"
        if not inkscape_run(t2, t3, fit=True):
            return False

        tree = ET.parse(t3)
        root = tree.getroot()
        vb = root.get('viewBox', '').split()
        vx, vy, vw, vh = map(float, vb) if len(vb) == 4 else (0, 0, 100, 100)

        # Padding = 10% of height, square based on height, center
        pad = vh * PADDING_PERCENT
        size = vh + 2 * pad
        new_x = vx - (size - vw) / 2
        new_y = vy - pad
        root.set('viewBox', f'{new_x:.4f} {new_y:.4f} {size:.4f} {size:.4f}')
        root.set('width', f'{size:.4f}')
        root.set('height', f'{size:.4f}')
        tree.write(dst, encoding='unicode', xml_declaration=True)
        t3.unlink(missing_ok=True)

    for f in [t1, t2]:
        f.unlink(missing_ok=True)
    return True


# === Font Generation ===

FF_SCRIPT = '''
import fontforge, os, sys
svg_dir, output = sys.argv[1], sys.argv[2]
font = fontforge.font()
font.fontname, font.familyname = "{name}", "{family}"
font.fullname = "{family} Regular"
font.em, font.ascent, font.descent = {em}, {ascent}, {descent}
for i, f in enumerate(sorted(f for f in os.listdir(svg_dir) if f.endswith('.svg'))):
    cp = {start} + i
    g = font.createChar(cp)
    g.importOutlines(os.path.join(svg_dir, f))
    g.removeOverlap()
    g.correctDirection()
    bbox = g.boundingBox()
    if bbox:
        w = bbox[2] - bbox[0]
        g.transform([1, 0, 0, 1, {margin} - bbox[0], 0])
        g.width = int(w + 2 * {margin})
    print(f"GLYPH|{{cp}}|{{os.path.splitext(f)[0]}}")
font.generate(output)
'''

def generate_font(svg_dir: Path, ttf_path: Path) -> list[tuple[str, int]]:
    script = FF_SCRIPT.format(
        name=FONT_NAME, family=FONT_FAMILY, em=UNITS_PER_EM,
        ascent=ASCENT, descent=DESCENT, start=UNICODE_START, margin=GLYPH_MARGIN
    )
    script_path = svg_dir / "_gen.py"
    script_path.write_text(script)
    result = subprocess.run([FONTFORGE, "-script", str(script_path), str(svg_dir), str(ttf_path)],
                           capture_output=True, text=True)
    script_path.unlink()
    return [(name, int(cp)) for line in result.stdout.split('\n')
            if line.startswith('GLYPH|') for _, cp, name in [line.split('|')]]

def generate_header(glyphs: list[tuple[str, int]], path: Path, font_sizes: dict[str, int]):
    """Generate Icon.hpp with enum and set() function.

    Args:
        glyphs: List of (name, codepoint) tuples
        path: Output path for Icon.hpp
        font_sizes: Dict mapping size names to pixel values, e.g. {'S': 12, 'M': 14, 'L': 16}
    """
    def utf8(cp: int) -> str:
        return f'\\x{0xE0|(cp>>12):02X}\\x{0x80|((cp>>6)&0x3F):02X}\\x{0x80|(cp&0x3F):02X}'

    # Extract names from dict (preserves insertion order in Python 3.7+)
    names = list(font_sizes.keys())

    # Generate Size enum: S = 12, M = 14, L = 16
    size_enum = ', '.join(f'{name} = {size}' for name, size in font_sizes.items())
    default_name = names[len(names) // 2]  # Middle size as default

    # Generate font selection chain: (size == Size::S) ? ... : (size == Size::M) ? ... : fallback
    # Works for any number of sizes (2, 3, 4, etc.)
    font_cases = []
    fonts_struct = getattr(sys.modules['config'], 'FONTS_STRUCT', 'fonts')
    for i, (name, size) in enumerate(font_sizes.items()):
        if i < len(font_sizes) - 1:
            # All except last: add condition
            font_cases.append(f'(size == Size::{name}) ? {fonts_struct}.icons_{size}')
        else:
            # Last size: fallback (no condition)
            font_cases.append(f'{fonts_struct}.icons_{size}')

    lines: list[str] = [
        f'// Auto-generated | {len(glyphs)} icons | {datetime.now():%Y-%m-%d}',
        '#pragma once', f'#include "{HEADER_INCLUDE}"', '',
        '#include <lvgl.h>', '',
        'namespace Icon {', f'enum class Size : uint8_t {{ {size_enum} }};', ''
    ]
    for name, cp in glyphs:
        cname = re.sub(r'[^a-zA-Z0-9]+', '_', name).strip('_').upper()
        lines.append(f'    constexpr const char* {cname} = "{utf8(cp)}";')

    # Build font selection chain with proper indentation
    font_select = '\n                        : '.join(font_cases)

    lines += ['',
        f'inline void set(lv_obj_t* label, const char* icon, Size size = Size::{default_name}) {{',
        f'    lv_font_t* font = {font_select};',
        '    lv_obj_set_style_text_font(label, font, 0);',
        '    lv_label_set_text(label, icon);',
        '}',
        '}  // namespace Icon'
    ]
    path.write_text('\n'.join(lines), encoding='utf-8')


# === LVGL Font Generation ===

def generate_lvgl_fonts(ttf_path: Path, out_dir: Path, glyphs: list[tuple[str, int]], font_sizes: dict[str, int], bpp: int) -> bool:
    """Generate LVGL binary fonts using npx lv_font_conv.

    Args:
        font_sizes: Dict mapping size names to pixel values, e.g. {'S': 12, 'M': 14, 'L': 16}
    """
    import shutil
    import tempfile

    # Find npx (on Windows it's npx.cmd)
    npx = shutil.which('npx')
    if not npx:
        warn("npx not found, skipping LVGL generation")
        warn("Install Node.js to get npx")
        return False

    # Unicode range from glyphs
    if not glyphs:
        return False
    min_cp = min(cp for _, cp in glyphs)
    max_cp = max(cp for _, cp in glyphs)
    char_range = f"0x{min_cp:04X}-0x{max_cp:04X}"

    data_dir = out_dir / "data"
    data_dir.mkdir(exist_ok=True)

    for size in font_sizes.values():
        arr_name = f"{FONT_NAME}_{size}_bin"
        out_name = f"{FONT_NAME}_{size}"

        # Generate binary
        with tempfile.NamedTemporaryFile(delete=False, suffix='.bin') as tmp:
            bin_file = Path(tmp.name)

        cmd = [
            npx, 'lv_font_conv',
            '--font', str(ttf_path),
            '--size', str(size),
            '--format', 'bin',
            '--bpp', str(bpp),
            '--range', char_range,
            '--no-kerning',
            '-o', str(bin_file)
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)

        if result.returncode != 0 or not bin_file.exists():
            warn(f"lv_font_conv failed for size {size}")
            continue

        bin_size = bin_file.stat().st_size
        bin_data = bin_file.read_bytes()
        bin_file.unlink()

        # Generate C file
        cpp_file = data_dir / f"{out_name}.c.inc"
        hex_data = ', '.join(f'0x{b:02x}' for b in bin_data)
        # Format in rows of 16 bytes
        hex_lines = [hex_data[i:i+16*6-2] for i in range(0, len(hex_data), 16*6)]

        cpp_content = f'''// Auto-generated | {FONT_NAME} | {size}px | {bpp}bpp | {datetime.now():%Y-%m-%d %H:%M}
#include <Arduino.h>

const uint8_t {arr_name}[] PROGMEM = {{
    {(','+chr(10)+'    ').join(hex_lines)}
}};
const uint32_t {arr_name}_len = {bin_size};
'''
        cpp_file.write_text(cpp_content, encoding='utf-8')

        # Generate header
        hpp_file = data_dir / f"{out_name}.hpp"
        hpp_content = f'''// Auto-generated | {FONT_NAME} | {size}px | {bpp}bpp | {datetime.now():%Y-%m-%d %H:%M}
#pragma once
#include <Arduino.h>
extern const uint8_t {arr_name}[] PROGMEM;
extern const uint32_t {arr_name}_len;
'''
        hpp_file.write_text(hpp_content, encoding='utf-8')
        success(f"{out_name} ({bin_size} bytes)")

    return True


# === Main ===

def main():
    print(f"\n{C.BOLD}{C.BLUE}═══ Icon Font Builder ═══{C.NC}\n")

    # Paths
    src_dir = PROJECT_ROOT / SVG_SOURCE_DIR
    ttf_dir = PROJECT_ROOT / TTF_OUTPUT_DIR
    header_dir = PROJECT_ROOT / HEADER_OUTPUT_DIR
    cache_dir = PROJECT_ROOT / CACHE_DIR
    svg_cache = cache_dir / "svg"
    temp_dir = cache_dir / "temp"

    if not src_dir.exists():
        error(f"Source not found: {SVG_SOURCE_DIR}")

    print(f"{C.DIM}Source: {src_dir}")
    print(f"TTF:    {ttf_dir}")
    print(f"Header: {header_dir}")
    print(f"Cache:  {cache_dir}{C.NC}\n")

    # Setup dirs
    ttf_dir.mkdir(parents=True, exist_ok=True)
    header_dir.mkdir(parents=True, exist_ok=True)
    svg_cache.mkdir(parents=True, exist_ok=True)
    temp_dir.mkdir(parents=True, exist_ok=True)

    # Load hashes
    old_hashes = load_hashes(cache_dir)
    new_hashes = {}

    # Find source SVGs
    src_svgs = {f.name: f for f in sorted(src_dir.glob("*.svg"))}

    # === Step 1: Process SVGs ===
    log(f"Processing SVGs ({len(src_svgs)} files)")

    stats = {'added': 0, 'modified': 0, 'unchanged': 0, 'removed': 0}
    need_rebuild = False

    for name, src_path in src_svgs.items():
        h = file_hash(src_path)
        new_hashes[name] = h
        cached_svg = svg_cache / name

        if name not in old_hashes:
            # New file
            if clean_svg(src_path, cached_svg, temp_dir):
                added(name)
                stats['added'] += 1
                need_rebuild = True
            else:
                warn(f"Failed: {name}")
        elif old_hashes[name] != h:
            # Modified file
            if clean_svg(src_path, cached_svg, temp_dir):
                modified(name)
                stats['modified'] += 1
                need_rebuild = True
            else:
                warn(f"Failed: {name}")
        else:
            # Unchanged
            if not cached_svg.exists():
                # Cache missing, regenerate
                if clean_svg(src_path, cached_svg, temp_dir):
                    modified(f"{name} (cache rebuilt)")
                    stats['modified'] += 1
                    need_rebuild = True
            else:
                skip(name)
                stats['unchanged'] += 1

    # Remove deleted files
    for name in old_hashes:
        if name not in src_svgs:
            cached = svg_cache / name
            if cached.exists():
                cached.unlink()
            removed(name)
            stats['removed'] += 1
            need_rebuild = True

    # Save hashes
    save_hashes(cache_dir, new_hashes)

    # Summary
    parts = []
    if stats['added']: parts.append(f"{C.GREEN}+{stats['added']}{C.NC}")
    if stats['modified']: parts.append(f"{C.YELLOW}~{stats['modified']}{C.NC}")
    if stats['removed']: parts.append(f"{C.RED}-{stats['removed']}{C.NC}")
    if stats['unchanged']: parts.append(f"{C.DIM}={stats['unchanged']}{C.NC}")
    success(f"SVGs: {' '.join(parts)}")

    # === Step 2: Generate Font ===
    ttf_path = ttf_dir / f"{FONT_NAME}.ttf"
    header_path = header_dir / "Icon.hpp"

    # Check if LVGL fonts need rebuild (in header_dir/data/)
    lvgl_files_exist = all(
        (header_dir / "data" / f"{FONT_NAME}_{s}.c.inc").exists()
        for s in FONT_SIZES.values()
    )

    if not need_rebuild and ttf_path.exists() and header_path.exists() and lvgl_files_exist:
        print(f"\n{C.GREEN}✓{C.NC} {C.DIM}No changes, font up to date{C.NC}")
    else:
        # Generate TTF
        log("Generating TTF")
        glyphs = generate_font(svg_cache, ttf_path)
        if not glyphs:
            error("FontForge failed")
        success(f"{ttf_path.name} ({len(glyphs)} glyphs)")

        # Generate Icon.hpp
        generate_header(glyphs, header_path, FONT_SIZES)
        success(header_path.name)

        # Generate LVGL fonts (output to header_dir for C++ includes)
        log(f"Generating LVGL fonts ({', '.join(map(str, FONT_SIZES.values()))}px)")
        generate_lvgl_fonts(ttf_path, header_dir, glyphs, FONT_SIZES, LVGL_BPP)

    # Cleanup temp
    for f in temp_dir.iterdir():
        f.unlink()

    print(f"\n{C.GREEN}✓ Done!{C.NC}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
