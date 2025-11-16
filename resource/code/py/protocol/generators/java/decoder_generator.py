"""
Java Decoder Generator
Generates Decoder.java with 7-bit MIDI-safe decoding methods.

This generator creates static decode methods for all builtin types
defined in builtin_types.yaml. Converts 7-bit MIDI-safe bytes back to native types.

Key Features:
- 7-bit decoding for multi-byte types (5 bytes → float32)
- Static methods with byte[] arrays
- Auto-generated from builtin_types.yaml (perfect C++/Java consistency)
- Java exceptions for error handling (IllegalArgumentException)
- Companion file: Encoder.java (for encode methods)

Generated Output:
- Decoder.java (~150 lines depending on builtin types)
- Package: com.midi_studio.protocol
- All methods: public static (utility class)
- Direction: SysEx bytes → Type
"""

from __future__ import annotations
from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.type_loader import TypeRegistry, AtomicType


def generate_decoder_java(type_registry: TypeRegistry, output_path: Path) -> str:
    """
    Generate Decoder.java with decode methods for builtin types.

    Args:
        type_registry: TypeRegistry instance with loaded builtin types
        output_path: Path where Decoder.java will be written

    Returns:
        Generated Java code as string

    Example:
        >>> registry = TypeRegistry()
        >>> registry.load_builtins(Path('builtin_types.yaml'))
        >>> code = generate_decoder_java(registry, Path('Decoder.java'))
    """
    builtin_types: dict[str, AtomicType] = {
        name: atomic_type
        for name, atomic_type in type_registry.types.items()
        if atomic_type.is_builtin
    }

    header = _generate_header(builtin_types, "com.midi_studio.protocol")
    decoders = _generate_decoders(builtin_types)
    footer = _generate_footer()

    full_code = f"{header}\n{decoders}\n{footer}"
    return full_code


def _generate_header(builtin_types: dict[str, AtomicType], package: str) -> str:
    """Generate file header with package and class declaration."""
    type_list = ", ".join(builtin_types.keys())

    return f'''package {package};

/**
 * Decoder - 7-bit MIDI-safe Decoder
 *
 * AUTO-GENERATED - DO NOT EDIT
 * Generated from: builtin_types.yaml
 *
 * This class provides static decode methods for all builtin primitive
 * types. Converts 7-bit MIDI-safe bytes back to native types.
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
 * - Static methods for zero overhead
 * - Direct byte array manipulation
 * - Optimized for protocol efficiency
 *
 * Companion file: Encoder.java (Type → SysEx direction)
 */
public final class Decoder {{

    // Private constructor prevents instantiation (utility class)
    private Decoder() {{
        throw new AssertionError("Utility class cannot be instantiated");
    }}

    // ============================================================================
    // DECODE METHODS (7-bit MIDI-safe bytes → Type)
    // ============================================================================
    // Returns decoded value or throws IllegalArgumentException on error
'''


def _generate_decoders(builtin_types: dict[str, AtomicType]) -> str:
    """Generate decode methods for each builtin type."""
    decoders: list[str] = []

    for type_name, atomic_type in sorted(builtin_types.items()):
        java_type = atomic_type.java_type
        desc = atomic_type.description

        if type_name == 'bool':
            decoders.append(f'''
    /**
     * Decode bool (1 byte)
     * {desc}
     *
     * @param data Byte array containing encoded data
     * @param offset Start offset in array
     * @return Decoded value
     * @throws IllegalArgumentException if insufficient data
     */
    public static {java_type} decodeBool(byte[] data, int offset) {{
        if (data.length - offset < 1) {{
            throw new IllegalArgumentException("Insufficient data for bool decode");
        }}
        return data[offset] != 0x00;
    }}
''')

        elif type_name == 'uint8':
            decoders.append(f'''
    /**
     * Decode uint8 (1 byte)
     * {desc}
     *
     * @param data Byte array containing encoded data
     * @param offset Start offset in array
     * @return Decoded value (unsigned, returned as {java_type})
     * @throws IllegalArgumentException if insufficient data
     */
    public static {java_type} decodeUint8(byte[] data, int offset) {{
        if (data.length - offset < 1) {{
            throw new IllegalArgumentException("Insufficient data for uint8 decode");
        }}
        return (data[offset] & 0x7F);
    }}
''')

        elif type_name == 'int8':
            decoders.append(f'''
    /**
     * Decode int8 (1 byte)
     * {desc}
     *
     * @param data Byte array containing encoded data
     * @param offset Start offset in array
     * @return Decoded value
     * @throws IllegalArgumentException if insufficient data
     */
    public static {java_type} decodeInt8(byte[] data, int offset) {{
        if (data.length - offset < 1) {{
            throw new IllegalArgumentException("Insufficient data for int8 decode");
        }}
        return (byte) (data[offset] & 0x7F);
    }}
''')

        elif type_name == 'uint16':
            decoders.append(f'''
    /**
     * Decode uint16 (3 bytes → 2 bytes)
     * {desc}
     *
     * @param data Byte array containing encoded data
     * @param offset Start offset in array
     * @return Decoded value (unsigned, returned as {java_type})
     * @throws IllegalArgumentException if insufficient data
     */
    public static {java_type} decodeUint16(byte[] data, int offset) {{
        if (data.length - offset < 3) {{
            throw new IllegalArgumentException("Insufficient data for uint16 decode");
        }}

        return (data[offset] & 0x7F)
                | ((data[offset + 1] & 0x7F) << 7)
                | ((data[offset + 2] & 0x03) << 14);
    }}
''')

        elif type_name == 'int16':
            decoders.append(f'''
    /**
     * Decode int16 (3 bytes → 2 bytes)
     * {desc}
     *
     * @param data Byte array containing encoded data
     * @param offset Start offset in array
     * @return Decoded value
     * @throws IllegalArgumentException if insufficient data
     */
    public static {java_type} decodeInt16(byte[] data, int offset) {{
        if (data.length - offset < 3) {{
            throw new IllegalArgumentException("Insufficient data for int16 decode");
        }}

        int bits = (data[offset] & 0x7F)
                 | ((data[offset + 1] & 0x7F) << 7)
                 | ((data[offset + 2] & 0x03) << 14);

        return (short) bits;
    }}
''')

        elif type_name == 'uint32':
            decoders.append(f'''
    /**
     * Decode uint32 (5 bytes → 4 bytes)
     * {desc}
     *
     * @param data Byte array containing encoded data
     * @param offset Start offset in array
     * @return Decoded value (unsigned, returned as {java_type})
     * @throws IllegalArgumentException if insufficient data
     */
    public static {java_type} decodeUint32(byte[] data, int offset) {{
        if (data.length - offset < 5) {{
            throw new IllegalArgumentException("Insufficient data for uint32 decode");
        }}

        long val = (data[offset] & 0x7FL)
                 | ((data[offset + 1] & 0x7FL) << 7)
                 | ((data[offset + 2] & 0x7FL) << 14)
                 | ((data[offset + 3] & 0x7FL) << 21)
                 | ((data[offset + 4] & 0x0FL) << 28);

        return (int) val;
    }}
''')

        elif type_name == 'int32':
            decoders.append(f'''
    /**
     * Decode int32 (5 bytes → 4 bytes)
     * {desc}
     *
     * @param data Byte array containing encoded data
     * @param offset Start offset in array
     * @return Decoded value
     * @throws IllegalArgumentException if insufficient data
     */
    public static {java_type} decodeInt32(byte[] data, int offset) {{
        if (data.length - offset < 5) {{
            throw new IllegalArgumentException("Insufficient data for int32 decode");
        }}

        long bits = (data[offset] & 0x7FL)
                  | ((data[offset + 1] & 0x7FL) << 7)
                  | ((data[offset + 2] & 0x7FL) << 14)
                  | ((data[offset + 3] & 0x7FL) << 21)
                  | ((data[offset + 4] & 0x0FL) << 28);

        return (int) bits;
    }}
''')

        elif type_name == 'float32':
            decoders.append(f'''
    /**
     * Decode float32 (5 bytes → 4 bytes)
     * {desc}
     *
     * @param data Byte array containing encoded data
     * @param offset Start offset in array
     * @return Decoded value
     * @throws IllegalArgumentException if insufficient data
     */
    public static {java_type} decodeFloat32(byte[] data, int offset) {{
        if (data.length - offset < 5) {{
            throw new IllegalArgumentException("Insufficient data for float32 decode");
        }}

        long bits = (data[offset] & 0x7FL)
                  | ((data[offset + 1] & 0x7FL) << 7)
                  | ((data[offset + 2] & 0x7FL) << 14)
                  | ((data[offset + 3] & 0x7FL) << 21)
                  | ((data[offset + 4] & 0x0FL) << 28);

        return Float.intBitsToFloat((int) bits);
    }}
''')

        elif type_name == 'string':
            decoders.append(f'''
    /**
     * Decode string (variable length)
     * {desc}
     *
     * @param data Byte array containing encoded data
     * @param offset Start offset in array
     * @param maxLength Maximum allowed string length
     * @return Decoded string
     * @throws IllegalArgumentException if insufficient data or string too long
     */
    public static String decodeString(byte[] data, int offset, int maxLength) {{
        if (data.length - offset < 1) {{
            throw new IllegalArgumentException("Insufficient data for string decode");
        }}

        int len = data[offset] & 0x7F;
        offset++;

        if (data.length - offset < len) {{
            throw new IllegalArgumentException("Insufficient data for string content");
        }}

        if (len > maxLength) {{
            throw new IllegalArgumentException(
                "String length " + len + " exceeds maximum " + maxLength);
        }}

        StringBuilder sb = new StringBuilder(len);
        for (int i = 0; i < len; i++) {{
            sb.append((char) (data[offset + i] & 0x7F));
        }}

        return sb.toString();
    }}
''')

    return "\n".join(decoders)


def _generate_footer() -> str:
    """Generate class closing."""
    return '''
}  // class Decoder
'''
