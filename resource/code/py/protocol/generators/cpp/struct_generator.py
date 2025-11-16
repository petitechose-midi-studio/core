"""
C++ Struct Generator
Generates struct/*.hpp files from messages with Encoder calls.

This generator creates lightweight C++ structs that call Encoder functions
instead of duplicating encoding logic. This achieves -73% code reduction
while maintaining identical performance (compiler inlines everything).

Key Features:
- Calls Encoder::encodeXXX() instead of inline logic (DRY)
- MAX_PAYLOAD_SIZE constexpr for validation
- ETL optional for safe decoding
- Zero runtime overhead (static inline + compiler optimization)

Generated Output:
- One .hpp file per message (e.g., TransportPlayMessage.hpp)
- ~60 lines per struct (vs ~350 with inline logic)
- Namespace: Protocol
"""

from __future__ import annotations

from typing import TYPE_CHECKING
from pathlib import Path

# Import field classes for runtime isinstance checks
from protocol.field import FieldBase, PrimitiveField, CompositeField
from protocol.generators.cpp.logger_generator import generate_log_method

if TYPE_CHECKING:
    from collections.abc import Sequence
    from protocol.message import Message
    from protocol.type_loader import TypeRegistry


def generate_struct_hpp(message: Message, message_id: int, type_registry: TypeRegistry, output_path: Path, string_max_length: int) -> str:
    """
    Generate C++ struct header for a message (supports composite fields).

    Args:
        message: Message instance to generate struct for
        message_id: Allocated MessageID for this message (e.g., 0x40)
        type_registry: TypeRegistry for resolving field types
        output_path: Path where struct .hpp will be written
        string_max_length: Maximum string length from config (e.g., 16)

    Returns:
        Generated C++ code as string

    Example:
        >>> transport_play = messages['TRANSPORT_PLAY']
        >>> code = generate_struct_hpp(transport_play, 0x40, registry, Path('TransportPlayMessage.hpp'), 16)
    """
    # Convert SCREAMING_SNAKE_CASE to PascalCase
    pascal_name = _to_pascal_case(message.name)
    struct_name = f"{pascal_name}Message"
    fields = message.fields
    description = f"{message.name} message"

    header = _generate_header(struct_name, description)

    # NEW: Generate composite structs FIRST (if any)
    composite_structs = _generate_composite_structs(fields, type_registry)

    struct_def = _generate_struct_definition(struct_name, message.name, message_id, fields, type_registry)
    encode_fn = _generate_encode_function(struct_name, fields, type_registry, string_max_length)
    decode_fn = _generate_decode_function(struct_name, fields, type_registry, string_max_length)

    # Generate toString() method for logging
    log_method = generate_log_method(struct_name, fields, type_registry)

    footer = _generate_footer()

    # Insert composite structs BEFORE main message struct
    full_code = f"{header}\n{composite_structs}\n{struct_def}\n{encode_fn}\n{decode_fn}\n\n{log_method}\n{footer}}};\n\n}}  // namespace Protocol\n"
    return full_code


def _generate_header(struct_name: str, description: str) -> str:
    """Generate file header with includes."""
    return f'''/**
 * {struct_name}.hpp - Auto-generated Protocol Struct
 *
 * AUTO-GENERATED - DO NOT EDIT
 * Generated from: types.yaml
 *
 * Description: {description}
 *
 * This struct uses encode/decode functions from Protocol namespace.
 * All encoding is 7-bit MIDI-safe. Performance is identical to inline
 * code due to static inline + compiler optimization.
 */

#pragma once

#include "../Encoder.hpp"
#include "../Decoder.hpp"
#include "../MessageID.hpp"
#include "../ProtocolConstants.hpp"
#include "../Logger.hpp"
#include <cstdint>
#include <etl/optional.h>
#include <etl/vector.h>

namespace Protocol {{

'''


def _generate_struct_definition(
    struct_name: str,
    message_name: str,  # SCREAMING_SNAKE_CASE name (e.g., "TRANSPORT_PLAY")
    message_id: int,    # Allocated ID (e.g., 0x40)
    fields: Sequence[FieldBase],  # Sequence of Field objects (primitive OR composite)
    type_registry: TypeRegistry
) -> str:
    """Generate struct definition with fields (supports composites)."""
    lines = [f"struct {struct_name} {{"]

    # Add static MESSAGE_ID constant
    lines.append(f"    // Auto-detected MessageID for protocol.send()")
    lines.append(f"    static constexpr MessageID MESSAGE_ID = MessageID::{message_name};")
    lines.append("")

    # Add fields FIRST (use new helper that handles both primitive and composite)
    for field in fields:
        cpp_type = _get_cpp_type_for_field(field, type_registry)
        lines.append(f"    {cpp_type} {field.name};")

    lines.append("")

    # Add fromHost field LAST (injected by DecoderRegistry after construction)
    lines.append(f"    // Origin tracking (set by DecoderRegistry during decode)")
    lines.append(f"    bool fromHost = false;")

    lines.append("")
    return "\n".join(lines)


def _generate_encode_function(
    struct_name: str,
    fields: Sequence[FieldBase],  # Sequence of Field objects
    type_registry: TypeRegistry,
    string_max_length: int
) -> str:
    """Generate encode() function calling Encoder."""
    # Calculate max and min payload sizes
    max_size = _calculate_max_payload_size(fields, type_registry, string_max_length)
    min_size = _calculate_min_payload_size(fields, type_registry, string_max_length)

    lines = [
        f"    /**",
        f"     * Maximum payload size in bytes (7-bit encoded)",
        f"     */",
        f"    static constexpr uint16_t MAX_PAYLOAD_SIZE = {max_size};",
        f"",
        f"    /**",
        f"     * Minimum payload size in bytes (with empty strings)",
        f"     */",
        f"    static constexpr uint16_t MIN_PAYLOAD_SIZE = {min_size};",
        f"",
        f"    /**",
        f"     * Encode struct to MIDI-safe bytes",
        f"     *",
        f"     * @param buffer Output buffer (must have >= MAX_PAYLOAD_SIZE bytes)",
        f"     * @param bufferSize Size of output buffer",
        f"     * @return Number of bytes written, or 0 if buffer too small",
        f"     */",
        f"    uint16_t encode(uint8_t* buffer, uint16_t bufferSize) const {{",
        f"        if (bufferSize < MAX_PAYLOAD_SIZE) return 0;",
        f"",
    ]

    # Only declare ptr if there are fields to encode
    if fields:
        lines.append(f"        uint8_t* ptr = buffer;")
        lines.append(f"")

    # Add encode calls for each field
    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            field_type_name = field.type_name.value
            if field.is_array():
                # Primitive array (e.g., string[16])
                lines.append(f"        encodeUint8(ptr, {field.name}.size());")
                lines.append(f"        for (const auto& item : {field.name}) {{")
                encoder_call = _get_encoder_call("item", field_type_name, type_registry)
                lines.append(f"            {encoder_call}")
                lines.append(f"        }}")
            else:
                # Scalar primitive
                encoder_call = _get_encoder_call(field.name, field_type_name, type_registry)
                lines.append(f"        {encoder_call}")
        else:  # Composite - encode array of structs
            assert isinstance(field, CompositeField)
            if field.array:
                # Encode array count first
                lines.append(f"        encodeUint8(ptr, {field.name}.size());")
                # Loop over array and encode each struct's fields
                lines.append(f"        for (const auto& item : {field.name}) {{")
                for nested_field in field.fields:
                    if nested_field.is_primitive():
                        assert isinstance(nested_field, PrimitiveField)
                        if nested_field.is_array():
                            # Nested array of primitives - encode count for dynamic arrays
                            lines.append(f"            encodeUint8(ptr, item.{nested_field.name}.size());")
                            lines.append(f"            for (const auto& type : item.{nested_field.name}) {{")
                            encoder_call = _get_encoder_call("type", nested_field.type_name.value, type_registry)
                            lines.append(f"                {encoder_call}")
                            lines.append(f"            }}")
                        else:
                            # Nested scalar primitive
                            encoder_call = _get_encoder_call(f"item.{nested_field.name}", nested_field.type_name.value, type_registry)
                            lines.append(f"            {encoder_call}")
                lines.append(f"        }}")
            else:
                # Single composite struct (not array)
                for nested_field in field.fields:
                    if nested_field.is_primitive():
                        assert isinstance(nested_field, PrimitiveField)
                        encoder_call = _get_encoder_call(f"{field.name}.{nested_field.name}", nested_field.type_name.value, type_registry)
                        lines.append(f"        {encoder_call}")

    # Return statement depends on whether we have fields
    lines.append("")
    if fields:
        lines.append("        return ptr - buffer;")
    else:
        lines.append("        return 0;")
    lines.extend([
        "    }",
        ""
    ])

    return "\n".join(lines)


def _generate_decode_function(
    struct_name: str,
    fields: Sequence[FieldBase],  # Sequence of Field objects
    type_registry: TypeRegistry,
    string_max_length: int
) -> str:
    """Generate static decode() function calling Encoder."""
    # Note: max_size and min_size are calculated in encode function, not needed here
    # as the decode function uses MIN_PAYLOAD_SIZE constant from the struct

    lines = [
        f"    /**",
        f"     * Decode struct from MIDI-safe bytes",
        f"     *",
        f"     * @param data Input buffer with encoded data",
        f"     * @param len Length of input buffer",
        f"     * @return Decoded struct, or etl::nullopt if invalid/insufficient data",
        f"     */",
        f"    static etl::optional<{struct_name}> decode(",
        f"        const uint8_t* data, uint16_t len) {{",
        f"",
        f"        if (len < MIN_PAYLOAD_SIZE) return etl::nullopt;",
        f"",
    ]

    # Only declare ptr and remaining if there are fields to decode
    if fields:
        lines.extend([
            f"        const uint8_t* ptr = data;",
            f"        size_t remaining = len;",
            f"",
            f"        // Decode fields",
        ])
    else:
        lines.append(f"        // No fields to decode")

    # Add decode calls for each field
    field_vars: list[str] = []
    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            field_type_name = field.type_name.value
            if field.is_array():
                # Primitive array (e.g., string[16])
                lines.append(f"        uint8_t count_{field.name};")
                lines.append(f"        if (!decodeUint8(ptr, remaining, count_{field.name})) return etl::nullopt;")
                cpp_type = _get_cpp_type_for_field(field, type_registry)
                var_name = f"{field.name}_data"
                lines.append(f"        {cpp_type} {var_name};")

                # For etl::vector, use push_back; for etl::array, use direct indexing
                if field.dynamic:
                    # Dynamic vector: decode into temp var and push_back
                    lines.append(f"        for (uint8_t i = 0; i < count_{field.name} && i < {field.array}; ++i) {{")
                    base_cpp_type = _get_cpp_type(field_type_name, type_registry)
                    lines.append(f"            {base_cpp_type} temp_item;")
                    decoder_call = _get_decoder_call(f"temp_item", field_type_name, type_registry, direct_target=f"temp_item")
                    lines.append(f"            {decoder_call}")
                    lines.append(f"            {var_name}.push_back(temp_item);")
                    lines.append(f"        }}")
                else:
                    # Fixed array: decode directly by index
                    lines.append(f"        for (uint8_t i = 0; i < count_{field.name} && i < {field.array}; ++i) {{")
                    decoder_call = _get_decoder_call(f"temp_item", field_type_name, type_registry, direct_target=f"{var_name}[i]")
                    lines.append(f"            {decoder_call}")
                    lines.append(f"        }}")

                field_vars.append(var_name)
            else:
                # Scalar primitive
                decoder_call = _get_decoder_call(field.name, field_type_name, type_registry)
                lines.append(f"        {decoder_call}")
                field_vars.append(field.name)
        else:  # Composite - decode array of structs
            assert isinstance(field, CompositeField)
            var_name = f"{field.name}_data"
            if field.array:
                # Decode array count (BUG FIX: use output parameter syntax)
                lines.append(f"        uint8_t count_{field.name};")
                lines.append(f"        if (!decodeUint8(ptr, remaining, count_{field.name})) return etl::nullopt;")
                # etl::array type (fixed size, but we fill based on count)
                cpp_type = _get_cpp_type_for_field(field, type_registry)
                lines.append(f"        {cpp_type} {var_name};")
                # Use PascalCase struct name for item type (BUG FIX #4)
                item_struct_name = _field_to_pascal_case(field.name)
                lines.append(f"        for (uint8_t i = 0; i < count_{field.name} && i < {field.array}; ++i) {{")
                lines.append(f"            {item_struct_name} item;")
                # Decode each field of the struct
                for nested_field in field.fields:
                    if nested_field.is_primitive():
                        assert isinstance(nested_field, PrimitiveField)
                        if nested_field.is_array():
                            # Nested array of primitives - decode count for dynamic arrays
                            lines.append(f"            uint8_t count_{nested_field.name};")
                            lines.append(f"            if (!decodeUint8(ptr, remaining, count_{nested_field.name})) return etl::nullopt;")

                            # For etl::vector, we need to use push_back instead of direct indexing
                            if nested_field.dynamic:
                                # Dynamic vector: decode into temp var and push_back
                                lines.append(f"            for (uint8_t j = 0; j < count_{nested_field.name} && j < {nested_field.array}; ++j) {{")
                                cpp_type = _get_cpp_type(nested_field.type_name.value, type_registry)
                                lines.append(f"                {cpp_type} temp_{nested_field.name};")
                                decoder_call = _get_decoder_call(
                                    f"temp_{nested_field.name}",
                                    nested_field.type_name.value,
                                    type_registry,
                                    direct_target=f"temp_{nested_field.name}"
                                )
                                lines.append(f"                {decoder_call}")
                                lines.append(f"                item.{nested_field.name}.push_back(temp_{nested_field.name});")
                                lines.append(f"            }}")
                            else:
                                # Fixed array: decode directly by index
                                lines.append(f"            for (uint8_t j = 0; j < count_{nested_field.name} && j < {nested_field.array}; ++j) {{")
                                direct_target = f"item.{nested_field.name}[j]"
                                decoder_call = _get_decoder_call(
                                    f"item_{nested_field.name}_j",
                                    nested_field.type_name.value,
                                    type_registry,
                                    direct_target=direct_target
                                )
                                lines.append(f"                {decoder_call}")
                                lines.append(f"            }}")
                        else:
                            # Nested scalar primitive
                            # OPTION B: Write directly to item struct member
                            direct_target = f"item.{nested_field.name}"
                            decoder_call = _get_decoder_call(
                                f"item_{nested_field.name}",  # Unused when direct_target set
                                nested_field.type_name.value,
                                type_registry,
                                direct_target=direct_target
                            )
                            lines.append(f"            {decoder_call}")
                lines.append(f"            {var_name}[i] = item;")  # Use array index instead of push_back
                lines.append(f"        }}")
            else:
                # Single composite struct (not array)
                # BUG FIX #2: Use capitalized struct type name instead of field name
                struct_type = field.name[0].upper() + field.name[1:]  # camelCase → PascalCase
                lines.append(f"        {struct_type} {var_name};")
                for nested_field in field.fields:
                    if nested_field.is_primitive():
                        assert isinstance(nested_field, PrimitiveField)
                        # OPTION B: Write directly to struct member (no temporary variable)
                        direct_target = f"{var_name}.{nested_field.name}"
                        decoder_call = _get_decoder_call(
                            f"{field.name}_{nested_field.name}",  # Unused when direct_target set
                            nested_field.type_name.value,
                            type_registry,
                            direct_target=direct_target
                        )
                        lines.append(f"        {decoder_call}")
            field_vars.append(var_name)

    # Construct and return struct
    # Primitives use output parameters (no dereference needed)
    # Composites return optional (need dereference)
    field_values: list[str] = []
    for i, field in enumerate(fields):
        var = field_vars[i]
        if field.is_primitive() and not field.is_array():
            field_values.append(var)  # Scalar primitive - output parameter, use directly
        else:
            field_values.append(var)  # Array or composite - use _data variable

    field_list = ", ".join(field_values)
    lines.extend([
        "",
        f"        return {struct_name}{{{field_list}}};",
        "    }",
        ""
    ])

    return "\n".join(lines)


def _generate_footer() -> str:
    """Generate struct closing brace."""
    return ""  # Closed in main function


def _get_cpp_type(field_type: str, type_registry: TypeRegistry) -> str:
    """
    Get C++ type for a field type string.

    Handles:
    - Builtin types (uint8 → uint8_t)
    - String (uses STRING_MAX_LENGTH constant instead of hardcoded <128>)
    - Array notation (uint8[8] → uint8_t[8])
    - Atomic types (ParameterValue → ParameterValue)

    Args:
        field_type: Type string from types.yaml
        type_registry: TypeRegistry instance

    Returns:
        C++ type string
    """
    # Check for array notation
    if '[' in field_type:
        base_type, array_size = field_type.split('[')
        array_size = array_size.rstrip(']')
        cpp_base = _get_cpp_type(base_type, type_registry)
        return f"{cpp_base}[{array_size}]"

    # Get atomic type
    if type_registry.is_atomic(field_type):
        atomic = type_registry.get(field_type)
        if atomic.is_builtin:
            # Always use STRING_MAX_LENGTH constant for strings
            if field_type == 'string':
                return "etl::string<STRING_MAX_LENGTH>"
            assert atomic.cpp_type is not None
            return atomic.cpp_type
        else:
            # Custom atomic type - use struct name
            return atomic.name

    raise ValueError(f"Unknown type: {field_type}")


def _get_encoder_call(field_name: str, field_type: str, type_registry: TypeRegistry) -> str:
    """
    Generate Encoder function call for encoding a field.

    Returns:
        C++ code line calling appropriate Encoder function
    """
    # Extract base type (handle arrays)
    base_type = field_type.split('[')[0]

    if not type_registry.is_atomic(base_type):
        raise ValueError(f"Unknown type: {base_type}")

    atomic = type_registry.get(base_type)

    if atomic.is_builtin:
        # Call encodeXXX() (already in Protocol namespace)
        encoder_name = f"encode{_capitalize_first(base_type)}"
        return f"{encoder_name}(ptr, {field_name});"
    else:
        # Nested struct - call its encode()
        return f"ptr += {field_name}.encode(ptr, bufferSize - (ptr - buffer));"


def _get_decoder_call(field_name: str, field_type: str, type_registry: TypeRegistry, direct_target: str | None = None) -> str:
    """
    Generate decoder function call for decoding a field.

    Args:
        field_name: Variable name for temporary storage (if direct_target is None)
        field_type: Type string
        type_registry: Type registry
        direct_target: Optional direct struct member path (e.g., "pageInfo_data.pageIndex")

    Returns:
        C++ code line(s) calling appropriate decoder function
    """
    base_type = field_type.split('[')[0]

    if not type_registry.is_atomic(base_type):
        raise ValueError(f"Unknown type: {base_type}")

    atomic = type_registry.get(base_type)
    cpp_type = _get_cpp_type(base_type, type_registry)

    if atomic.is_builtin:
        decoder_name = f"decode{_capitalize_first(base_type)}"
        target = direct_target if direct_target else field_name

        if base_type == 'string':
            decoder_call = f"{decoder_name}<STRING_MAX_LENGTH>(ptr, remaining, {target})"
        else:
            decoder_call = f"{decoder_name}(ptr, remaining, {target})"

        # OPTION B: Direct pattern if direct_target provided
        if direct_target:
            return f"if (!{decoder_call}) return etl::nullopt;"
        else:
            return f'''{cpp_type} {field_name};
        if (!{decoder_call}) return etl::nullopt;'''
    else:
        return f'''auto {field_name} = {base_type}::decode(ptr, remaining);
        if (!{field_name}) return etl::nullopt;
        ptr += {base_type}::MAX_PAYLOAD_SIZE;
        remaining -= {base_type}::MAX_PAYLOAD_SIZE;'''


def _calculate_max_payload_size(fields: Sequence[FieldBase], type_registry: TypeRegistry, string_max_length: int) -> int:
    """
    Calculate maximum payload size in bytes (7-bit encoded).
    Supports both primitive and composite fields.

    Args:
        fields: List of Field objects (primitive or composite)
        type_registry: TypeRegistry instance

    Returns:
        Maximum size in bytes
    """
    total_size = 0

    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            # Primitive field
            field_type_name = field.type_name.value
            array_size = field.array if field.array else 1

            # Get size for base type
            if type_registry.is_atomic(field_type_name):
                atomic = type_registry.get(field_type_name)

                if atomic.is_builtin:
                    # Builtin type - use size_bytes from YAML
                    if atomic.size_bytes == 'variable':
                        # String: 1 byte length prefix + STRING_MAX_LENGTH chars
                        base_size = 1 + string_max_length  # From sysex_protocol_config.yaml
                    else:
                        assert isinstance(atomic.size_bytes, int)
                        base_size = _get_encoded_size(field_type_name, atomic.size_bytes)
                else:
                    # Not builtin (shouldn't happen in Python-unified)
                    base_size = 10  # Conservative estimate

                total_size += base_size * array_size

        else:  # Composite field
            assert isinstance(field, CompositeField)
            # Recursively calculate size of nested fields
            nested_size = _calculate_max_payload_size(field.fields, type_registry, string_max_length)

            if field.array:
                # Array of composites: count byte + (nested_size * array_size)
                total_size += 1  # Array count byte
                total_size += nested_size * field.array
            else:
                # Single composite
                total_size += nested_size

    return total_size


def _calculate_min_payload_size(fields: Sequence[FieldBase], type_registry: TypeRegistry, string_max_length: int) -> int:
    """
    Calculate minimum payload size in bytes (7-bit encoded) with empty strings.
    Used for decode validation to allow variable-length messages.

    Args:
        fields: List of Field objects (primitive or composite)
        type_registry: TypeRegistry instance
        string_max_length: Maximum string length (unused, kept for signature compatibility)

    Returns:
        Minimum size in bytes
    """
    total_size = 0

    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            # Primitive field
            field_type_name = field.type_name.value
            array_size = field.array if field.array else 1

            # Get size for base type
            if type_registry.is_atomic(field_type_name):
                atomic = type_registry.get(field_type_name)

                if atomic.is_builtin:
                    # Builtin type - use size_bytes from YAML
                    if atomic.size_bytes == 'variable':
                        # String: 1 byte length prefix only (empty string)
                        base_size = 1
                    else:
                        assert isinstance(atomic.size_bytes, int)
                        base_size = _get_encoded_size(field_type_name, atomic.size_bytes)
                else:
                    # Not builtin (shouldn't happen in Python-unified)
                    base_size = 10  # Conservative estimate

                total_size += base_size * array_size

        else:  # Composite field
            assert isinstance(field, CompositeField)
            # Recursively calculate size of nested fields
            nested_size = _calculate_min_payload_size(field.fields, type_registry, string_max_length)

            if field.array:
                # Array of composites: count byte + (nested_size * array_size)
                total_size += 1  # Array count byte
                total_size += nested_size * field.array
            else:
                # Single composite
                total_size += nested_size

    return total_size


def _get_encoded_size(type_name: str, raw_size: int) -> int:
    """
    Get 7-bit encoded size for a builtin type.

    Args:
        type_name: Builtin type name (e.g., 'bool', 'uint8', 'float32')
        raw_size: Raw size in bytes

    Returns:
        Encoded size in bytes
    """
    # bool: 1 byte (0x00 or 0x01)
    if type_name == 'bool':
        return 1

    # uint8, int8: 1 byte (no encoding)
    if type_name in ('uint8', 'int8'):
        return 1

    # uint16, int16: 2 → 3 bytes
    if type_name in ('uint16', 'int16'):
        return 3

    # uint32, int32, float32: 4 → 5 bytes
    if type_name in ('uint32', 'int32', 'float32'):
        return 5

    # Default: assume 7-bit encoding (size * 8 / 7, rounded up)
    return ((raw_size * 8) + 6) // 7


def _capitalize_first(s: str) -> str:
    """
    Capitalize first letter only.

    Examples:
        uint8 → Uint8
        float32 → Float32
    """
    if not s:
        return s
    return s[0].upper() + s[1:]


def _to_pascal_case(s: str) -> str:
    """
    Convert SCREAMING_SNAKE_CASE to PascalCase.

    Examples:
        TRANSPORT_PLAY → TransportPlay
        DEVICE_PARAMS → DeviceParams
        transport_play → TransportPlay
    """
    if not s:
        return s
    # Split by underscore and capitalize each word
    words = s.split('_')
    return ''.join(word.capitalize() for word in words)


def _field_to_pascal_case(field_name: str) -> str:
    """
    Convert camelCase field name to PascalCase struct name.

    Examples:
        pageInfo → PageInfo
        parameters → Parameters
        deviceName → DeviceName
    """
    if not field_name:
        return field_name
    # Simply capitalize first letter (preserves rest of camelCase)
    return field_name[0].upper() + field_name[1:]


# ============================================================================
# COMPOSITE FIELD SUPPORT (Phase 2)
# ============================================================================

def _generate_composite_structs(fields: Sequence[FieldBase], type_registry: TypeRegistry, depth: int = 0) -> str:
    """
    Recursively generate all composite struct definitions from fields.
    Returns empty string if no composites found.
    """
    if depth > 3:
        return ""  # Safety: max recursion depth

    structs: list[str] = []
    for field in fields:
        if field.is_composite():
            assert isinstance(field, CompositeField)
            # Generate nested composites first (depth-first)
            nested = _generate_composite_structs(field.fields, type_registry, depth + 1)
            if nested:
                structs.append(nested)

            # Generate this composite struct
            struct_code = _generate_single_composite_struct(field, type_registry)
            structs.append(struct_code)

    return "\n".join(structs)


def _generate_single_composite_struct(field: CompositeField, type_registry: TypeRegistry) -> str:
    """
    Generate a single composite struct definition with include guards.

    BUG FIX #1: Wrap struct in #ifndef guard to prevent redefinition
    when multiple messages use the same composite type.
    """
    # BUG FIX #4: Convert camelCase to PascalCase (pageInfo → PageInfo, not Pageinfo)
    struct_name = _field_to_pascal_case(field.name)

    # Generate include guard macro (PROTOCOL_<STRUCT_NAME>_HPP)
    guard_macro = f"PROTOCOL_{struct_name.upper()}_STRUCT"

    lines = [
        f"#ifndef {guard_macro}",
        f"#define {guard_macro}",
        "",
        f"struct {struct_name} {{"
    ]

    # Add member fields
    for nested_field in field.fields:
        if nested_field.is_primitive():
            assert isinstance(nested_field, PrimitiveField)
            # Get base C++ type without array wrapper
            base_type = _get_cpp_type(nested_field.type_name.value, type_registry)
            if nested_field.array:
                # Use etl::vector for dynamic arrays, etl::array for fixed
                if nested_field.dynamic:
                    lines.append(f"    etl::vector<{base_type}, {nested_field.array}> {nested_field.name};")
                else:
                    lines.append(f"    etl::array<{base_type}, {nested_field.array}> {nested_field.name};")
            else:
                lines.append(f"    {base_type} {nested_field.name};")
        else:  # Nested composite
            assert isinstance(nested_field, CompositeField)
            nested_struct_name = _field_to_pascal_case(nested_field.name)
            if nested_field.array:
                lines.append(f"    etl::array<{nested_struct_name}, {nested_field.array}> {nested_field.name};")
            else:
                lines.append(f"    {nested_struct_name} {nested_field.name};")

    lines.append("};")
    lines.append("")
    lines.append(f"#endif // {guard_macro}")
    lines.append("")
    return "\n".join(lines)


def _get_cpp_type_for_field(field: FieldBase, type_registry: TypeRegistry) -> str:
    """Get C++ type for a field (handles primitive and composite)."""
    if field.is_primitive():
        assert isinstance(field, PrimitiveField)
        base_type = _get_cpp_type(field.type_name.value, type_registry)
        if field.array:
            # Use etl::vector for dynamic arrays, etl::array for fixed
            if field.dynamic:
                return f"etl::vector<{base_type}, {field.array}>"
            else:
                return f"etl::array<{base_type}, {field.array}>"
        return base_type
    else:  # Composite
        assert isinstance(field, CompositeField)
        # BUG FIX #4: Convert camelCase to PascalCase
        struct_name = _field_to_pascal_case(field.name)
        if field.array:
            return f"etl::array<{struct_name}, {field.array}>"
        return struct_name
