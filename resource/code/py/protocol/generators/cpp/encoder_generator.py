"""
C++ Encoder Generator
Generates Encoder.hpp with 7-bit MIDI-safe encoding functions.

This generator creates static inline encode functions for all builtin
types defined in builtin_types.yaml. The encoding ensures all bytes are
MIDI-safe (<0x80) to prevent conflicts with SysEx control bytes.

Key Features:
- 7-bit encoding for multi-byte types (float32: 4→5 bytes)
- Static inline functions (zero runtime overhead)
- Auto-generated from builtin_types.yaml (perfect consistency)
- Companion file: Decoder.hpp (for decode functions)

Generated Output:
- Encoder.hpp (~150 lines depending on builtin types)
- Namespace: Protocol
- All functions: static inline (header-only)
- Direction: Type → SysEx bytes
"""

from __future__ import annotations

from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.type_loader import TypeRegistry, AtomicType


def generate_encoder_hpp(type_registry: TypeRegistry, output_path: Path) -> str:
    """
    Generate Encoder.hpp with encode functions for builtin types.

    Args:
        type_registry: TypeRegistry instance with loaded builtin types
        output_path: Path where Encoder.hpp will be written

    Returns:
        Generated C++ code as string

    Example:
        >>> registry = TypeRegistry()
        >>> registry.load_builtins(Path('builtin_types.yaml'))
        >>> code = generate_encoder_hpp(registry, Path('Encoder.hpp'))
    """
    builtin_types: dict[str, AtomicType] = {
        name: atomic_type
        for name, atomic_type in type_registry.types.items()
        if atomic_type.is_builtin
    }

    header = _generate_header(builtin_types)
    encoders = _generate_encoders(builtin_types)
    footer = _generate_footer()

    full_code = f"{header}\n{encoders}\n{footer}"
    return full_code


def _generate_header(builtin_types: dict[str, AtomicType]) -> str:
    """Generate file header with includes and namespace."""
    type_list = ", ".join(builtin_types.keys())

    return f'''/**
 * Encoder.hpp - 7-bit MIDI-safe Encoder
 *
 * AUTO-GENERATED - DO NOT EDIT
 * Generated from: builtin_types.yaml
 *
 * This file provides static inline encode functions for all builtin
 * primitive types. Converts native types to 7-bit MIDI-safe bytes.
 *
 * Supported types: {type_list}
 *
 * Encoding Strategy:
 * - Single-byte types (uint8, int8): No encoding (already < 0x80 when valid)
 * - Multi-byte integers: 7-bit per byte (e.g., uint16: 2→3 bytes)
 * - Float32: 4→5 bytes (7-bit chunks)
 * - String: length prefix (7-bit) + ASCII data
 *
 * Performance:
 * - Static inline = zero runtime overhead
 * - Compiler optimizes away function calls
 * - Identical to manual inline code
 *
 * Companion file: Decoder.hpp (SysEx → Type direction)
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <etl/string.h>

namespace Protocol {{

// ============================================================================
// ENCODE FUNCTIONS (Type → 7-bit MIDI-safe bytes)
// ============================================================================
'''


def _generate_encoders(builtin_types: dict[str, AtomicType]) -> str:
    """Generate encode functions for each builtin type."""
    encoders: list[str] = []

    for type_name, atomic_type in sorted(builtin_types.items()):
        cpp_type: str | None = atomic_type.cpp_type
        desc: str = atomic_type.description

        if cpp_type is None:
            continue

        if type_name == 'bool':
            encoders.append(f'''
/**
 * Encode bool (1 byte: 0x00 or 0x01)
 * {desc}
 */
static inline void encodeBool(uint8_t*& buf, bool val) {{
    *buf++ = val ? 0x01 : 0x00;
}}
''')

        elif type_name == 'uint8':
            encoders.append(f'''
/**
 * Encode uint8 (1 byte, no transformation needed if < 0x80)
 * {desc}
 */
static inline void encodeUint8(uint8_t*& buf, uint8_t val) {{
    *buf++ = val & 0x7F;  // Ensure MIDI-safe
}}
''')

        elif type_name == 'int8':
            encoders.append(f'''
/**
 * Encode int8 (1 byte, signed → unsigned mapping)
 * {desc}
 */
static inline void encodeInt8(uint8_t*& buf, int8_t val) {{
    *buf++ = static_cast<uint8_t>(val) & 0x7F;
}}
''')

        elif type_name == 'uint16':
            encoders.append(f'''
/**
 * Encode uint16 (2 bytes → 3 bytes, 7-bit encoding)
 * {desc}
 * Overhead: +50% (2→3 bytes)
 */
static inline void encodeUint16(uint8_t*& buf, uint16_t val) {{
    *buf++ = val & 0x7F;           // bits 0-6
    *buf++ = (val >> 7) & 0x7F;    // bits 7-13
    *buf++ = (val >> 14) & 0x03;   // bits 14-15 (only 2 bits needed)
}}
''')

        elif type_name == 'int16':
            encoders.append(f'''
/**
 * Encode int16 (2 bytes → 3 bytes, 7-bit encoding)
 * {desc}
 * Overhead: +50% (2→3 bytes)
 */
static inline void encodeInt16(uint8_t*& buf, int16_t val) {{
    uint16_t bits = static_cast<uint16_t>(val);
    *buf++ = bits & 0x7F;
    *buf++ = (bits >> 7) & 0x7F;
    *buf++ = (bits >> 14) & 0x03;
}}
''')

        elif type_name == 'uint32':
            encoders.append(f'''
/**
 * Encode uint32 (4 bytes → 5 bytes, 7-bit encoding)
 * {desc}
 * Overhead: +25% (4→5 bytes)
 */
static inline void encodeUint32(uint8_t*& buf, uint32_t val) {{
    *buf++ = val & 0x7F;           // bits 0-6
    *buf++ = (val >> 7) & 0x7F;    // bits 7-13
    *buf++ = (val >> 14) & 0x7F;   // bits 14-20
    *buf++ = (val >> 21) & 0x7F;   // bits 21-27
    *buf++ = (val >> 28) & 0x0F;   // bits 28-31 (only 4 bits needed)
}}
''')

        elif type_name == 'int32':
            encoders.append(f'''
/**
 * Encode int32 (4 bytes → 5 bytes, 7-bit encoding)
 * {desc}
 * Overhead: +25% (4→5 bytes)
 */
static inline void encodeInt32(uint8_t*& buf, int32_t val) {{
    uint32_t bits = static_cast<uint32_t>(val);
    *buf++ = bits & 0x7F;
    *buf++ = (bits >> 7) & 0x7F;
    *buf++ = (bits >> 14) & 0x7F;
    *buf++ = (bits >> 21) & 0x7F;
    *buf++ = (bits >> 28) & 0x0F;
}}
''')

        elif type_name == 'float32':
            encoders.append(f'''
/**
 * Encode float32 (4 bytes → 5 bytes, 7-bit encoding)
 * {desc}
 * Overhead: +25% (4→5 bytes)
 *
 * Uses IEEE 754 bit representation, split into 7-bit chunks.
 */
static inline void encodeFloat32(uint8_t*& buf, float val) {{
    uint32_t bits;
    memcpy(&bits, &val, sizeof(float));  // Type-punning via memcpy (safe)

    *buf++ = bits & 0x7F;           // bits 0-6
    *buf++ = (bits >> 7) & 0x7F;    // bits 7-13
    *buf++ = (bits >> 14) & 0x7F;   // bits 14-20
    *buf++ = (bits >> 21) & 0x7F;   // bits 21-27
    *buf++ = (bits >> 28) & 0x0F;   // bits 28-31
}}
''')

        elif type_name == 'string':
            encoders.append(f'''
/**
 * Encode string (variable length: 1 byte length + data)
 * {desc}
 *
 * Format: [length (7-bit)] [char0] [char1] ... [charN-1]
 * Max length: 127 chars (7-bit length encoding)
 */
template<size_t MAX_SIZE>
static inline void encodeString(uint8_t*& buf, const etl::string<MAX_SIZE>& str) {{
    uint8_t len = static_cast<uint8_t>(str.length()) & 0x7F;  // Max 127
    *buf++ = len;

    for (size_t i = 0; i < len; ++i) {{
        *buf++ = static_cast<uint8_t>(str[i]) & 0x7F;  // ASCII chars are already < 0x80
    }}
}}
''')

    return "\n".join(encoders)


def _generate_footer() -> str:
    """Generate namespace closing and file footer."""
    return '''
}  // namespace Protocol
'''
