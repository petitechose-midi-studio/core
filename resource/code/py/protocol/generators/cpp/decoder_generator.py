"""
C++ Decoder Generator
Generates Decoder.hpp with 7-bit MIDI-safe decoding functions.

This generator creates static inline decode functions for all builtin
types defined in builtin_types.yaml. The decoding converts 7-bit MIDI-safe
bytes back to native types.

Key Features:
- 7-bit decoding for multi-byte types (5 bytes → float32)
- Static inline functions (zero runtime overhead)
- Auto-generated from builtin_types.yaml (perfect consistency)
- ETL optional for safe error handling
- Separate from Encoder.hpp for clarity (SysEx → Type)

Generated Output:
- Decoder.hpp (~200-300 lines depending on builtin types)
- Namespace: Protocol
- All functions: static inline (header-only)
"""

from __future__ import annotations

from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.type_loader import TypeRegistry, AtomicType


def generate_decoder_hpp(type_registry: TypeRegistry, output_path: Path) -> str:
    """
    Generate Decoder.hpp with decode functions for builtin types.

    Args:
        type_registry: TypeRegistry instance with loaded builtin types
        output_path: Path where Decoder.hpp will be written

    Returns:
        Generated C++ code as string

    Example:
        >>> registry = TypeRegistry()
        >>> registry.load_builtins(Path('builtin_types.yaml'))
        >>> code = generate_decoder_hpp(registry, Path('Decoder.hpp'))
    """
    builtin_types: dict[str, AtomicType] = {
        name: atomic_type
        for name, atomic_type in type_registry.types.items()
        if atomic_type.is_builtin
    }

    header = _generate_header(builtin_types)
    decoders = _generate_decoders(builtin_types)
    footer = _generate_footer()

    full_code = f"{header}\n{decoders}\n{footer}"
    return full_code


def _generate_header(builtin_types: dict[str, AtomicType]) -> str:
    """Generate file header with includes and namespace."""
    type_list = ", ".join(builtin_types.keys())

    return f'''/**
 * Decoder.hpp - 7-bit MIDI-safe Decoder
 *
 * AUTO-GENERATED - DO NOT EDIT
 * Generated from: builtin_types.yaml
 *
 * This file provides static inline decode functions for all builtin
 * primitive types. Converts 7-bit MIDI-safe bytes back to native types.
 *
 * Supported types: {type_list}
 *
 * Decoding Strategy:
 * - SysEx bytes (7-bit) → Native types
 * - Multi-byte integers: 7-bit chunks → full integers
 * - Float32: 5 bytes → 4 bytes (7-bit chunks → IEEE 754)
 * - String: length prefix + ASCII data
 *
 * Performance:
 * - Static inline = zero runtime overhead
 * - Compiler optimizes away function calls
 * - Identical to manual inline code
 *
 * Companion file: Encoder.hpp (Type → SysEx direction)
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <etl/optional.h>
#include <etl/string.h>

namespace Protocol {{

// ============================================================================
// DECODE FUNCTIONS (7-bit MIDI-safe bytes → Type)
// ============================================================================
// Returns bool success, writes decoded value to output parameter
// Output parameter pattern minimizes memory footprint (no optional overhead)
// Returns false if insufficient data or invalid encoding
'''


def _generate_decoders(builtin_types: dict[str, AtomicType]) -> str:
    """Generate decode functions for each builtin type."""
    decoders: list[str] = []

    for type_name, atomic_type in sorted(builtin_types.items()):
        cpp_type: str | None = atomic_type.cpp_type
        desc: str = atomic_type.description

        if cpp_type is None:
            continue

        if type_name == 'bool':
            decoders.append(f'''
/**
 * Decode bool (1 byte)
 * {desc}
 */
static inline bool decodeBool(
    const uint8_t*& buf, size_t& remaining, bool& out) {{

    if (remaining < 1) return false;

    out = (*buf++) != 0x00;
    remaining -= 1;
    return true;
}}
''')

        elif type_name == 'uint8':
            decoders.append(f'''
/**
 * Decode uint8 (1 byte)
 * {desc}
 */
static inline bool decodeUint8(
    const uint8_t*& buf, size_t& remaining, uint8_t& out) {{

    if (remaining < 1) return false;

    out = (*buf++) & 0x7F;
    remaining -= 1;
    return true;
}}
''')

        elif type_name == 'int8':
            decoders.append(f'''
/**
 * Decode int8 (1 byte)
 * {desc}
 */
static inline bool decodeInt8(
    const uint8_t*& buf, size_t& remaining, int8_t& out) {{

    if (remaining < 1) return false;

    out = static_cast<int8_t>((*buf++) & 0x7F);
    remaining -= 1;
    return true;
}}
''')

        elif type_name == 'uint16':
            decoders.append(f'''
/**
 * Decode uint16 (3 bytes → 2 bytes)
 * {desc}
 */
static inline bool decodeUint16(
    const uint8_t*& buf, size_t& remaining, uint16_t& out) {{

    if (remaining < 3) return false;

    out = (buf[0] & 0x7F)
        | ((buf[1] & 0x7F) << 7)
        | ((buf[2] & 0x03) << 14);
    buf += 3;
    remaining -= 3;
    return true;
}}
''')

        elif type_name == 'int16':
            decoders.append(f'''
/**
 * Decode int16 (3 bytes → 2 bytes)
 * {desc}
 */
static inline bool decodeInt16(
    const uint8_t*& buf, size_t& remaining, int16_t& out) {{

    if (remaining < 3) return false;

    uint16_t bits = (buf[0] & 0x7F)
                  | ((buf[1] & 0x7F) << 7)
                  | ((buf[2] & 0x03) << 14);
    out = static_cast<int16_t>(bits);
    buf += 3;
    remaining -= 3;
    return true;
}}
''')

        elif type_name == 'uint32':
            decoders.append(f'''
/**
 * Decode uint32 (5 bytes → 4 bytes)
 * {desc}
 */
static inline bool decodeUint32(
    const uint8_t*& buf, size_t& remaining, uint32_t& out) {{

    if (remaining < 5) return false;

    out = (buf[0] & 0x7F)
        | ((buf[1] & 0x7F) << 7)
        | ((buf[2] & 0x7F) << 14)
        | ((buf[3] & 0x7F) << 21)
        | ((buf[4] & 0x0F) << 28);
    buf += 5;
    remaining -= 5;
    return true;
}}
''')

        elif type_name == 'int32':
            decoders.append(f'''
/**
 * Decode int32 (5 bytes → 4 bytes)
 * {desc}
 */
static inline bool decodeInt32(
    const uint8_t*& buf, size_t& remaining, int32_t& out) {{

    if (remaining < 5) return false;

    uint32_t bits = (buf[0] & 0x7F)
                  | ((buf[1] & 0x7F) << 7)
                  | ((buf[2] & 0x7F) << 14)
                  | ((buf[3] & 0x7F) << 21)
                  | ((buf[4] & 0x0F) << 28);
    out = static_cast<int32_t>(bits);
    buf += 5;
    remaining -= 5;
    return true;
}}
''')

        elif type_name == 'float32':
            decoders.append(f'''
/**
 * Decode float32 (5 bytes → 4 bytes)
 * {desc}
 */
static inline bool decodeFloat32(
    const uint8_t*& buf, size_t& remaining, float& out) {{

    if (remaining < 5) return false;

    uint32_t bits = (buf[0] & 0x7F)
                  | ((buf[1] & 0x7F) << 7)
                  | ((buf[2] & 0x7F) << 14)
                  | ((buf[3] & 0x7F) << 21)
                  | ((buf[4] & 0x0F) << 28);
    buf += 5;
    remaining -= 5;

    memcpy(&out, &bits, sizeof(float));  // Type-punning via memcpy (safe)
    return true;
}}
''')

        elif type_name == 'string':
            decoders.append(f'''
/**
 * Decode string (variable length)
 * {desc}
 */
template<size_t MAX_SIZE>
static inline bool decodeString(
    const uint8_t*& buf, size_t& remaining, etl::string<MAX_SIZE>& out) {{

    if (remaining < 1) return false;

    uint8_t len = (*buf++) & 0x7F;
    remaining -= 1;

    if (remaining < len) return false;
    if (len > MAX_SIZE) return false;  // String too long

    out.clear();
    for (uint8_t i = 0; i < len; ++i) {{
        out.push_back(static_cast<char>(buf[i] & 0x7F));
    }}

    buf += len;
    remaining -= len;
    return true;
}}
''')

    return "\n".join(decoders)


def _generate_footer() -> str:
    """Generate namespace closing and file footer."""
    return '''
}  // namespace Protocol
'''
