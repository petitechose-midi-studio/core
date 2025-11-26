#!/usr/bin/env python3
"""LVGL Image Converter - PNG to C array. Config via env: IMG_OUTPUT_DIR, COLOR_FORMAT, COMPRESS."""
from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from types import ModuleType

def find_project_root() -> Path:
    """Find project root by looking for platformio.ini."""
    current = Path(__file__).resolve().parent
    while current != current.parent:
        if (current / "platformio.ini").exists():
            return current
        current = current.parent
    raise FileNotFoundError("Project root not found")

def load_lvgl_module() -> ModuleType:
    """Dynamically load LVGLImage from LVGL library."""
    root = find_project_root()
    sys.path.insert(0, str(root / ".pio/libdeps/debug/lvgl/scripts"))
    import LVGLImage as module  # type: ignore[import-not-found]
    return module

# Load LVGL module
lvgl = load_lvgl_module()
LVGLImage: Any = lvgl.LVGLImage
ColorFormat: Any = lvgl.ColorFormat
CompressMethod: Any = lvgl.CompressMethod

# Config from environment
IMG_OUTPUT_DIR: Path = Path(os.environ.get("IMG_OUTPUT_DIR", ""))
COLOR_FORMAT: str = os.environ.get("COLOR_FORMAT", "ARGB8565")
COMPRESS: str = os.environ.get("COMPRESS", "LZ4")

CF_MAP: dict[str, Any] = {
    "RGB565": ColorFormat.RGB565,
    "ARGB8888": ColorFormat.ARGB8888,
    "ARGB8565": ColorFormat.ARGB8565,
    "RGB565A8": ColorFormat.RGB565A8,
    "L8": ColorFormat.L8,
    "A8": ColorFormat.A8,
}

COMPRESS_MAP: dict[str, Any] = {
    "NONE": CompressMethod.NONE,
    "LZ4": CompressMethod.LZ4,
    "RLE": CompressMethod.RLE,
}


def convert(input_path: str) -> bool:
    """Convert PNG to LVGL C array with PROGMEM attribute."""
    if not os.path.exists(input_path):
        print(f"  ✗ Not found: {input_path}", file=sys.stderr)
        return False

    input_file = Path(input_path)
    var_name = input_file.stem.replace("-", "_").replace(".", "_")
    output_path = IMG_OUTPUT_DIR / f"{var_name}.c"

    cf = CF_MAP.get(COLOR_FORMAT.upper())
    compress = COMPRESS_MAP.get(COMPRESS.upper())

    if not cf or not compress:
        print("  ✗ Invalid config", file=sys.stderr)
        return False

    try:
        img: Any = LVGLImage().from_png(input_path, cf=cf, background=0x000000)
        img.adjust_stride(align=1)
        img.to_c_array(str(output_path), compress=compress, outputname=var_name)

        # Add PROGMEM for Arduino
        content = output_path.read_text()
        pos = content.find("#endif") + len("#endif")
        if pos > len("#endif"):
            content = f"{content[:pos]}\n\n#ifdef ARDUINO\n#include <avr/pgmspace.h>\n#endif{content[pos:]}"

        macro = f"LV_ATTRIBUTE_{var_name.upper()}"
        content = content.replace(
            f"#ifndef {macro}\n#define {macro}\n#endif",
            f"#ifndef {macro}\n#define {macro} PROGMEM\n#endif"
        )
        output_path.write_text(content)

        size = output_path.stat().st_size
        print(f"  ✓ {input_file.name} → {var_name}.c ({img.w}x{img.h}, {size:,}B)")
        return True

    except Exception as e:
        print(f"  ✗ {input_file.name}: {e}", file=sys.stderr)
        return False


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: img_converter.py <image.png>", file=sys.stderr)
        sys.exit(1)
    sys.exit(0 if convert(sys.argv[1]) else 1)
