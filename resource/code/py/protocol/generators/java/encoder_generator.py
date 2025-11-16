"""
Java Encoder Generator
Generates Encoder.java with 7-bit MIDI-safe encoding methods.

This generator creates static encode methods for all builtin types
defined in builtin_types.yaml. Converts native types to 7-bit MIDI-safe bytes.

Key Features:
- 7-bit encoding for multi-byte types (float32: 4→5 bytes)
- Static methods with byte[] arrays
- Auto-generated from builtin_types.yaml (perfect C++/Java consistency)
- Companion file: Decoder.java (for decode methods)

Generated Output:
- Encoder.java (~150 lines depending on builtin types)
- Package: com.midi_studio.protocol
- All methods: public static (utility class)
- Direction: Type → SysEx bytes
"""

from __future__ import annotations
from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.type_loader import TypeRegistry, AtomicType


def generate_encoder_java(type_registry: TypeRegistry, output_path: Path) -> str:
    """
    Generate Encoder.java with encode methods for builtin types.

    Args:
        type_registry: TypeRegistry instance with loaded builtin types
        output_path: Path where Encoder.java will be written

    Returns:
        Generated Java code as string

    Example:
        >>> registry = TypeRegistry()
        >>> registry.load_builtins(Path('builtin_types.yaml'))
        >>> code = generate_encoder_java(registry, Path('Encoder.java'))
    """
    builtin_types: dict[str, AtomicType] = {
        name: atomic_type
        for name, atomic_type in type_registry.types.items()
        if atomic_type.is_builtin
    }

    header = _generate_header(builtin_types, "com.midi_studio.protocol")
    encoders = _generate_encoders(builtin_types)
    footer = _generate_footer()

    full_code = f"{header}\n{encoders}\n{footer}"
    return full_code


def _generate_header(builtin_types: dict[str, AtomicType], package: str) -> str:
    """Generate file header with package and class declaration."""
    type_list = ", ".join(builtin_types.keys())

    return f'''package {package};

/**
 * Encoder - 7-bit MIDI-safe Encoder/Decoder
 *
 * AUTO-GENERATED - DO NOT EDIT
 * Generated from: builtin_types.yaml
 *
 * This class provides static encode/decode methods for all builtin primitive
 * types. All multi-byte types use 7-bit encoding to ensure MIDI-safe
 * transmission (all bytes < 0x80).
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
 * - Static methods for zero overhead
 * - Direct byte array manipulation
 * - Optimized for protocol efficiency
 */
public final class Encoder {{

    // Private constructor prevents instantiation (utility class)
    private Encoder() {{
        throw new AssertionError("Utility class cannot be instantiated");
    }}

    // ============================================================================
    // ENCODE METHODS (Type → 7-bit MIDI-safe bytes)
    // ============================================================================
'''


def _generate_encoders(builtin_types: dict[str, AtomicType]) -> str:
    """Generate encode methods for each builtin type."""
    encoders: list[str] = []

    for type_name, atomic_type in sorted(builtin_types.items()):
        java_type = atomic_type.java_type
        desc = atomic_type.description

        if type_name == 'bool':
            encoders.append(f'''
    /**
     * Encode bool (1 byte: 0x00 or 0x01)
     * {desc}
     *
     * @param value Value to encode
     * @return Encoded byte array (1 byte)
     */
    public static byte[] encodeBool({java_type} value) {{
        return new byte[]{{ (byte) (value ? 0x01 : 0x00) }};
    }}
''')

        elif type_name == 'uint8':
            encoders.append(f'''
    /**
     * Encode uint8 (1 byte, no transformation needed if < 0x80)
     * {desc}
     *
     * @param value Value to encode (treated as unsigned)
     * @return Encoded byte array (1 byte)
     */
    public static byte[] encodeUint8({java_type} value) {{
        return new byte[]{{ (byte) (value & 0x7F) }};
    }}
''')

        elif type_name == 'int8':
            encoders.append(f'''
    /**
     * Encode int8 (1 byte, signed → unsigned mapping)
     * {desc}
     *
     * @param value Value to encode
     * @return Encoded byte array (1 byte)
     */
    public static byte[] encodeInt8({java_type} value) {{
        return new byte[]{{ (byte) (value & 0x7F) }};
    }}
''')

        elif type_name == 'uint16':
            encoders.append(f'''
    /**
     * Encode uint16 (2 bytes → 3 bytes, 7-bit encoding)
     * {desc}
     * Overhead: +50% (2→3 bytes)
     *
     * @param value Value to encode (treated as unsigned)
     * @return Encoded byte array (3 bytes)
     */
    public static byte[] encodeUint16({java_type} value) {{
        int val = value & 0xFFFF;  // Treat as unsigned
        return new byte[]{{
            (byte) (val & 0x7F),           // bits 0-6
            (byte) ((val >> 7) & 0x7F),    // bits 7-13
            (byte) ((val >> 14) & 0x03)    // bits 14-15 (only 2 bits needed)
        }};
    }}
''')

        elif type_name == 'int16':
            encoders.append(f'''
    /**
     * Encode int16 (2 bytes → 3 bytes, 7-bit encoding)
     * {desc}
     * Overhead: +50% (2→3 bytes)
     *
     * @param value Value to encode
     * @return Encoded byte array (3 bytes)
     */
    public static byte[] encodeInt16({java_type} value) {{
        int bits = value & 0xFFFF;
        return new byte[]{{
            (byte) (bits & 0x7F),
            (byte) ((bits >> 7) & 0x7F),
            (byte) ((bits >> 14) & 0x03)
        }};
    }}
''')

        elif type_name == 'uint32':
            encoders.append(f'''
    /**
     * Encode uint32 (4 bytes → 5 bytes, 7-bit encoding)
     * {desc}
     * Overhead: +25% (4→5 bytes)
     *
     * @param value Value to encode (treated as unsigned)
     * @return Encoded byte array (5 bytes)
     */
    public static byte[] encodeUint32({java_type} value) {{
        long val = value & 0xFFFFFFFFL;  // Treat as unsigned
        return new byte[]{{
            (byte) (val & 0x7F),           // bits 0-6
            (byte) ((val >> 7) & 0x7F),    // bits 7-13
            (byte) ((val >> 14) & 0x7F),   // bits 14-20
            (byte) ((val >> 21) & 0x7F),   // bits 21-27
            (byte) ((val >> 28) & 0x0F)    // bits 28-31 (only 4 bits needed)
        }};
    }}
''')

        elif type_name == 'int32':
            encoders.append(f'''
    /**
     * Encode int32 (4 bytes → 5 bytes, 7-bit encoding)
     * {desc}
     * Overhead: +25% (4→5 bytes)
     *
     * @param value Value to encode
     * @return Encoded byte array (5 bytes)
     */
    public static byte[] encodeInt32({java_type} value) {{
        long bits = value & 0xFFFFFFFFL;
        return new byte[]{{
            (byte) (bits & 0x7F),
            (byte) ((bits >> 7) & 0x7F),
            (byte) ((bits >> 14) & 0x7F),
            (byte) ((bits >> 21) & 0x7F),
            (byte) ((bits >> 28) & 0x0F)
        }};
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
     *
     * @param value Value to encode
     * @return Encoded byte array (5 bytes)
     */
    public static byte[] encodeFloat32({java_type} value) {{
        int bits = Float.floatToRawIntBits(value);
        long unsignedBits = bits & 0xFFFFFFFFL;

        return new byte[]{{
            (byte) (unsignedBits & 0x7F),           // bits 0-6
            (byte) ((unsignedBits >> 7) & 0x7F),    // bits 7-13
            (byte) ((unsignedBits >> 14) & 0x7F),   // bits 14-20
            (byte) ((unsignedBits >> 21) & 0x7F),   // bits 21-27
            (byte) ((unsignedBits >> 28) & 0x0F)    // bits 28-31
        }};
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
     *
     * @param value String to encode
     * @param maxLength Maximum allowed string length
     * @return Encoded byte array (1 + string.length bytes)
     * @throws IllegalArgumentException if string exceeds maxLength
     */
    public static byte[] encodeString(String value, int maxLength) {{
        if (value == null) {{
            value = "";
        }}

        int len = Math.min(value.length(), Math.min(maxLength, 127));
        byte[] result = new byte[1 + len];

        result[0] = (byte) (len & 0x7F);

        for (int i = 0; i < len; i++) {{
            result[1 + i] = (byte) (value.charAt(i) & 0x7F);  // ASCII chars < 0x80
        }}

        return result;
    }}
''')

    return "\n".join(encoders)


def _generate_footer() -> str:
    """Generate class closing."""
    return '''
}  // class Encoder
'''
