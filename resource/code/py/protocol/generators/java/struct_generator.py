"""
Java Struct Generator
Generates Message*.java files from messages with Encoder calls.

This generator creates immutable Java classes that call Encoder methods
instead of duplicating encoding logic. This achieves significant code
reduction while maintaining clean separation of concerns.

Key Features:
- Immutable classes (final fields)
- Calls Encoder.encodeXXX() instead of inline logic (DRY)
- Static factory decode() method
- MAX_PAYLOAD_SIZE constant for validation
- Clean getters following Java conventions
- toString() method for YAML logging (matches C++ format)

Generated Output:
- One .java file per message (e.g., TransportPlayMessage.java)
- ~80-120 lines per class (compact, readable)
- Package: com.midi_studio.protocol.struct
"""

from __future__ import annotations

from typing import TYPE_CHECKING
from pathlib import Path

# Import field classes for runtime isinstance checks
from protocol.field import FieldBase, PrimitiveField, CompositeField

# Import logger generator
from protocol.generators.java.logger_generator import generate_log_method

if TYPE_CHECKING:
    from collections.abc import Sequence
    from protocol.message import Message
    from protocol.type_loader import TypeRegistry


def generate_struct_java(message: Message, message_id: int, type_registry: TypeRegistry, output_path: Path, string_max_length: int) -> str:
    """
    Generate Java message class for a message.

    Args:
        message: Message instance to generate class for
        type_registry: TypeRegistry for resolving field types
        output_path: Path where message .java will be written

    Returns:
        Generated Java code as string

    Example:
        >>> transport_play = messages['TRANSPORT_PLAY']
        >>> code = generate_struct_java(transport_play, registry, Path('TransportPlayMessage.java'))
    """
    # Convert SCREAMING_SNAKE_CASE to PascalCase
    pascal_name = _to_pascal_case(message.name)
    class_name = f"{pascal_name}Message"
    fields = message.fields
    description = f"{message.name} message"

    # Analyze what imports are needed based on fields
    needs_encoder = len(fields) > 0
    needs_decoder = len(fields) > 0
    needs_list = _needs_list_import(fields)
    needs_arraylist = _needs_list_import(fields)
    needs_constants = _needs_constants_import(fields, type_registry)

    header = _generate_header(class_name, description, needs_encoder, needs_decoder, needs_list, needs_arraylist, needs_constants)
    message_id_constant = _generate_message_id_constant(message.name)
    inner_classes = _generate_inner_classes(fields, type_registry)
    field_declarations = _generate_field_declarations(fields, type_registry)
    constructor = _generate_constructor(class_name, fields, type_registry)
    getters = _generate_getters(fields, type_registry)
    encode_method = _generate_encode_method(class_name, fields, type_registry, string_max_length)
    decode_method = _generate_decode_method(class_name, fields, type_registry, string_max_length)
    log_method = generate_log_method(class_name, fields, type_registry)
    footer = _generate_footer()

    full_code = f"{header}\n{message_id_constant}\n{inner_classes}\n{field_declarations}\n{constructor}\n{getters}\n{encode_method}\n{decode_method}\n{log_method}\n{footer}"
    return full_code


def _generate_header(class_name: str, description: str, needs_encoder: bool, needs_decoder: bool,
                     needs_list: bool, needs_arraylist: bool, needs_constants: bool) -> str:
    """Generate file header with package and class declaration, importing only what's needed."""
    imports = ["import com.midi_studio.protocol.MessageID;"]

    if needs_encoder:
        imports.append("import com.midi_studio.protocol.Encoder;")
    if needs_decoder:
        imports.append("import com.midi_studio.protocol.Decoder;")
    if needs_constants:
        imports.append("import com.midi_studio.protocol.ProtocolConstants;")
    if needs_list:
        imports.append("import java.util.List;")
    if needs_arraylist:
        imports.append("import java.util.ArrayList;")

    imports_str = "\n".join(imports)

    return f'''package com.midi_studio.protocol.struct;

{imports_str}

/**
 * {class_name} - Auto-generated Protocol Message
 *
 * AUTO-GENERATED - DO NOT EDIT
 * Generated from: types.yaml
 *
 * Description: {description}
 *
 * This class is immutable and uses Encoder for encode/decode operations.
 * All encoding is 7-bit MIDI-safe.
 */
public final class {class_name} {{
'''


def _generate_message_id_constant(message_name: str) -> str:
    """Generate MESSAGE_ID constant for auto-detection in protocol.send()."""
    return f'''
    // ============================================================================
    // Auto-detected MessageID for protocol.send()
    // ============================================================================

    public static final MessageID MESSAGE_ID = MessageID.{message_name};
'''


def _generate_field_declarations(fields: Sequence[FieldBase], type_registry: TypeRegistry) -> str:
    """Generate private final field declarations (supports composites)."""
    lines: list[str] = ["    // ============================================================================"]
    lines.append("    // Fields")
    lines.append("    // ============================================================================")
    lines.append("")

    # Add fromHost field (injected by DecoderRegistry, ignored during encode)
    lines.append("    // Origin tracking (set by DecoderRegistry during decode)")
    lines.append("    public boolean fromHost = false;")
    lines.append("")

    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            field_type_name = field.type_name.value
            java_type = _get_java_type(field_type_name, type_registry)
            if field.is_array():
                lines.append(f"    private final List<{java_type}> {field.name};")
            else:
                lines.append(f"    private final {java_type} {field.name};")
        else:  # Composite
            class_name = _field_to_pascal_case(field.name)
            if field.array:
                lines.append(f"    private final List<{class_name}> {field.name};")
            else:
                lines.append(f"    private final {class_name} {field.name};")

    lines.append("")
    return "\n".join(lines)


def _generate_constructor(class_name: str, fields: Sequence[FieldBase], type_registry: TypeRegistry) -> str:
    """Generate public constructor."""
    lines: list[str] = ["    // ============================================================================"]
    lines.append("    // Constructor")
    lines.append("    // ============================================================================")
    lines.append("")
    lines.append("    /**")
    lines.append(f"     * Construct a new {class_name}")
    lines.append("     *")

    # Add parameter docs
    for field in fields:
        lines.append(f"     * @param {field.name} The {field.name} value")

    lines.append("     */")

    # Constructor signature
    params: list[str] = []
    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            field_type_name = field.type_name.value
            java_type = _get_java_type(field_type_name, type_registry)
            if field.is_array():
                params.append(f"List<{java_type}> {field.name}")
            else:
                params.append(f"{java_type} {field.name}")
        else:  # Composite
            class_name_inner = _field_to_pascal_case(field.name)
            if field.array:
                params.append(f"List<{class_name_inner}> {field.name}")
            else:
                params.append(f"{class_name_inner} {field.name}")

    param_str = ", ".join(params)
    lines.append(f"    public {class_name}({param_str}) {{")

    # Assignments
    for field in fields:
        lines.append(f"        this.{field.name} = {field.name};")

    lines.append("    }")
    lines.append("")

    return "\n".join(lines)


def _generate_getters(fields: Sequence[FieldBase], type_registry: TypeRegistry) -> str:
    """Generate public getters."""
    lines: list[str] = ["    // ============================================================================"]
    lines.append("    // Getters")
    lines.append("    // ============================================================================")
    lines.append("")

    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            field_type_name = field.type_name.value
            java_type = _get_java_type(field_type_name, type_registry)
            if field.is_array():
                java_type = f"List<{java_type}>"
        else:  # Composite
            class_name = _field_to_pascal_case(field.name)
            java_type = f"List<{class_name}>" if field.array else class_name

        getter_name = _to_getter_name(field.name)

        lines.append(f"    /**")
        lines.append(f"     * Get the {field.name} value")
        lines.append(f"     *")
        lines.append(f"     * @return {field.name}")
        lines.append(f"     */")
        lines.append(f"    public {java_type} {getter_name}() {{")
        lines.append(f"        return {field.name};")
        lines.append(f"    }}")
        lines.append("")

    return "\n".join(lines)


def _generate_encode_method(class_name: str, fields: Sequence[FieldBase], type_registry: TypeRegistry, string_max_length: int) -> str:
    """Generate encode() method calling Encoder."""
    max_size = _calculate_max_payload_size(fields, type_registry, string_max_length)

    lines: list[str] = ["    // ============================================================================"]
    lines.append("    // Encoding")
    lines.append("    // ============================================================================")
    lines.append("")
    lines.append(f"    /**")
    lines.append(f"     * Maximum payload size in bytes (7-bit encoded)")
    lines.append(f"     */")
    lines.append(f"    public static final int MAX_PAYLOAD_SIZE = {max_size};")
    lines.append("")
    lines.append("    /**")
    lines.append("     * Encode message to MIDI-safe bytes")
    lines.append("     *")
    lines.append("     * @return Encoded byte array")
    lines.append("     */")
    lines.append("    public byte[] encode() {")
    lines.append("        byte[] buffer = new byte[MAX_PAYLOAD_SIZE];")

    # Only declare offset if there are fields to encode
    if fields:
        lines.append("        int offset = 0;")
        lines.append("")

    # Add encode calls for each field
    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            field_type_name = field.type_name.value
            if field.is_array():
                # Primitive array (e.g., List<String>)
                lines.append(f"        byte[] {field.name}_count = Encoder.encodeUint8({field.name}.size());")
                lines.append(f"        System.arraycopy({field.name}_count, 0, buffer, offset, 1);")
                lines.append(f"        offset += 1;")
                lines.append(f"")
                lines.append(f"        for ({_get_java_type(field_type_name, type_registry)} item : {field.name}) {{")
                encoder_call = _get_encoder_call("item", field_type_name, type_registry)
                for line in encoder_call.split('\n'):
                    lines.append(f"    {line}")
                lines.append(f"        }}")
                lines.append(f"")
            else:
                # Scalar primitive
                encoder_call = _get_encoder_call(field.name, field_type_name, type_registry)
                lines.append(f"        {encoder_call}")
        else:  # Composite
            assert isinstance(field, CompositeField)
            if field.array:
                # Encode array count
                lines.append(f"        byte[] {field.name}_count = Encoder.encodeUint8({field.name}.size());")
                lines.append(f"        System.arraycopy({field.name}_count, 0, buffer, offset, 1);")
                lines.append(f"        offset += 1;")
                lines.append(f"")
                # Encode each item
                class_name = _field_to_pascal_case(field.name)
                lines.append(f"        for ({class_name} item : {field.name}) {{")
                for nested_field in field.fields:
                    if nested_field.is_primitive():
                        assert isinstance(nested_field, PrimitiveField)
                        getter_name = _to_getter_name(nested_field.name)
                        if nested_field.is_array():
                            # Nested array of primitives - encode count for dynamic arrays
                            java_type = _get_java_type(nested_field.type_name.value, type_registry)
                            lines.append(f"            byte[] count_{nested_field.name} = Encoder.encodeUint8((byte) item.{getter_name}().length);")
                            lines.append(f"            System.arraycopy(count_{nested_field.name}, 0, buffer, offset, 1);")
                            lines.append(f"            offset += 1;")
                            lines.append(f"            for ({java_type} type : item.{getter_name}()) {{")
                            encoder_call = _get_encoder_call("type", nested_field.type_name.value, type_registry)
                            # Indent by 16 spaces (4 levels)
                            for line in encoder_call.split('\n'):
                                lines.append(f"        {line}")
                            lines.append(f"            }}")
                        else:
                            encoder_call = _get_encoder_call(f"item.{getter_name}()", nested_field.type_name.value, type_registry)
                            # Indent by 12 spaces (3 levels)
                            for line in encoder_call.split('\n'):
                                lines.append(f"    {line}")
                lines.append(f"        }}")
                lines.append(f"")
            else:
                # Single composite - encode nested fields directly
                for nested_field in field.fields:
                    if nested_field.is_primitive():
                        assert isinstance(nested_field, PrimitiveField)
                        getter_name = _to_getter_name(nested_field.name)
                        encoder_call = _get_encoder_call(f"{field.name}.{getter_name}()", nested_field.type_name.value, type_registry)
                        lines.append(f"        {encoder_call}")

    lines.append("")
    lines.append("        return buffer;")
    lines.append("    }")
    lines.append("")

    return "\n".join(lines)


def _generate_decode_method(class_name: str, fields: Sequence[FieldBase], type_registry: TypeRegistry, string_max_length: int) -> str:
    """Generate static decode() factory method."""
    min_size = _calculate_min_payload_size(fields, type_registry, string_max_length)

    lines: list[str] = ["    // ============================================================================"]
    lines.append("    // Decoding")
    lines.append("    // ============================================================================")
    lines.append("")
    lines.append("    /**")
    lines.append("     * Minimum payload size in bytes (with empty strings)")
    lines.append("     */")
    lines.append(f"    private static final int MIN_PAYLOAD_SIZE = {min_size};")
    lines.append("")
    lines.append("    /**")
    lines.append("     * Decode message from MIDI-safe bytes")
    lines.append("     *")
    lines.append("     * @param data Input buffer with encoded data")
    lines.append(f"     * @return Decoded {class_name} instance")
    lines.append("     * @throws IllegalArgumentException if data is invalid or insufficient")
    lines.append("     */")
    lines.append(f"    public static {class_name} decode(byte[] data) {{")
    lines.append(f"        if (data.length < MIN_PAYLOAD_SIZE) {{")
    lines.append(f'            throw new IllegalArgumentException("Insufficient data for {class_name} decode");')
    lines.append("        }")
    lines.append("")

    # Only declare offset if there are fields to decode
    if fields:
        lines.append("        int offset = 0;")
        lines.append("")

    # Add decode calls for each field
    field_vars: list[str] = []
    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            field_type_name = field.type_name.value
            java_type = _get_java_type(field_type_name, type_registry)
            if field.is_array():
                # Primitive array (e.g., List<String>)
                lines.append(f"        int count_{field.name} = Decoder.decodeUint8(data, offset);")
                lines.append(f"        offset += 1;")
                lines.append(f"")
                lines.append(f"        List<{java_type}> {field.name}_list = new ArrayList<>();")
                lines.append(f"        for (int i = 0; i < count_{field.name}; i++) {{")
                decoder_call = _get_decoder_call(f"item_{field.name}", field_type_name, java_type, type_registry)
                for line in decoder_call.split('\n'):
                    lines.append(f"    {line}")
                lines.append(f"            {field.name}_list.add(item_{field.name});")
                lines.append(f"        }}")
                lines.append(f"")
                field_vars.append(f"{field.name}_list")
            else:
                # Scalar primitive
                decoder_call = _get_decoder_call(field.name, field_type_name, java_type, type_registry)
                lines.append(f"        {decoder_call}")
                field_vars.append(field.name)
        else:  # Composite
            assert isinstance(field, CompositeField)
            if field.array:
                # Decode array count
                lines.append(f"        int count_{field.name} = Decoder.decodeUint8(data, offset);")
                lines.append(f"        offset += 1;")
                lines.append(f"")
                # Decode items
                lines.append(f"        List<{_field_to_pascal_case(field.name)}> {field.name}_list = new ArrayList<>();")
                lines.append(f"        for (int i = 0; i < count_{field.name}; i++) {{")
                # Decode each nested field
                for nested_field in field.fields:
                    if nested_field.is_primitive():
                        assert isinstance(nested_field, PrimitiveField)
                        java_type = _get_java_type(nested_field.type_name.value, type_registry)
                        if nested_field.is_array():
                            # Nested array of primitives - decode count for dynamic arrays
                            lines.append(f"            byte count_{nested_field.name} = (byte) Decoder.decodeUint8(data, offset);")
                            lines.append(f"            offset += 1;")
                            lines.append(f"            {java_type}[] item_{nested_field.name} = new {java_type}[count_{nested_field.name}];")
                            lines.append(f"            for (int j = 0; j < count_{nested_field.name} && j < {nested_field.array}; j++) {{")
                            decoder_call = _get_decoder_call(f"item_{nested_field.name}_j", nested_field.type_name.value, java_type, type_registry)
                            # Indent by 16 spaces (4 levels)
                            for line in decoder_call.split('\n'):
                                lines.append(f"        {line}")
                            lines.append(f"                item_{nested_field.name}[j] = item_{nested_field.name}_j;")
                            lines.append(f"            }}")
                        else:
                            decoder_call = _get_decoder_call(f"item_{nested_field.name}", nested_field.type_name.value, java_type, type_registry)
                            for line in decoder_call.split('\n'):
                                lines.append(f"    {line}")
                # Construct item
                item_params: list[str] = []
                for nested_field in field.fields:
                    if nested_field.is_primitive():
                        item_params.append(f"item_{nested_field.name}")
                item_params_str = ", ".join(item_params)
                lines.append(f"            {field.name}_list.add(new {_field_to_pascal_case(field.name)}({item_params_str}));")
                lines.append(f"        }}")
                lines.append(f"")
                field_vars.append(f"{field.name}_list")
            else:
                # Single composite - decode nested fields
                for nested_field in field.fields:
                    if nested_field.is_primitive():
                        assert isinstance(nested_field, PrimitiveField)
                        java_type = _get_java_type(nested_field.type_name.value, type_registry)
                        decoder_call = _get_decoder_call(f"{field.name}_{nested_field.name}", nested_field.type_name.value, java_type, type_registry)
                        lines.append(f"        {decoder_call}")
                # Construct composite
                composite_params: list[str] = []
                for nested_field in field.fields:
                    if nested_field.is_primitive():
                        composite_params.append(f"{field.name}_{nested_field.name}")
                composite_params_str = ", ".join(composite_params)
                lines.append(f"        {_field_to_pascal_case(field.name)} {field.name} = new {_field_to_pascal_case(field.name)}({composite_params_str});")
                lines.append(f"")
                field_vars.append(field.name)

    # Construct and return instance
    field_list = ", ".join(field_vars)
    lines.append("")
    lines.append(f"        return new {class_name}({field_list});")
    lines.append("    }")
    lines.append("")

    return "\n".join(lines)


def _generate_footer() -> str:
    """Generate class closing."""
    return '}  // class Message\n'


def _get_java_type(field_type: str, type_registry: TypeRegistry) -> str:
    """
    Get Java type for a field type string.

    Handles:
    - Builtin types (uint8 → byte, float32 → float)
    - Array notation (uint8[8] → byte[])
    - Atomic types (ParameterValue → ParameterValueMessage)

    Args:
        field_type: Type string from types.yaml
        type_registry: TypeRegistry instance

    Returns:
        Java type string
    """
    # Check for array notation
    if '[' in field_type:
        base_type, array_size = field_type.split('[')
        array_size = array_size.rstrip(']')
        java_base = _get_java_type(base_type, type_registry)
        return f"{java_base}[]"

    # Get atomic type
    if type_registry.is_atomic(field_type):
        atomic = type_registry.get(field_type)
        if atomic.is_builtin:
            java_type = atomic.java_type
            if java_type is None:
                raise ValueError(f"Missing Java type mapping for builtin type: {field_type}")
            return java_type
        else:
            # Custom atomic type - use class name with Message suffix
            return f"{atomic.name}Message"

    raise ValueError(f"Unknown type: {field_type}")


def _sanitize_var_name(field_expr: str) -> str:
    """
    Sanitize field expression to create valid Java variable name.

    Examples:
        pageInfo.getPageIndex() → pageInfo_pageIndex
        item.getParameterValue() → item_parameterValue
        deviceName → deviceName
    """
    # Remove parentheses
    name: str = field_expr.replace('()', '')
    # Replace dot with underscore
    name = name.replace('.', '_')
    # Remove 'get' prefix after dot if present
    if '_get' in name:
        parts: list[str] = name.split('_')
        result: list[str] = []
        for part in parts:
            if part.startswith('get') and len(part) > 3:
                # Remove 'get' and lowercase first letter
                result.append(part[3].lower() + part[4:])
            else:
                result.append(part)
        name = '_'.join(result)
    return name


def _get_encoder_call(field_name: str, field_type: str, type_registry: TypeRegistry) -> str:
    """
    Generate Encoder method call for encoding a field.

    Returns:
        Java code lines calling appropriate Encoder method
    """
    # Extract base type (handle arrays)
    base_type = field_type.split('[')[0]

    if not type_registry.is_atomic(base_type):
        raise ValueError(f"Unknown type: {base_type}")

    atomic = type_registry.get(base_type)

    # Create valid variable name from field expression
    var_name = _sanitize_var_name(field_name)

    if atomic.is_builtin:
        # Call Encoder.encodeXXX()
        encoder_name = f"encode{_capitalize_first(base_type)}"

        if base_type == 'string':
            # String needs max length parameter (using ProtocolConstants.STRING_MAX_LENGTH from config)
            return f"byte[] {var_name}_encoded = Encoder.{encoder_name}({field_name}, ProtocolConstants.STRING_MAX_LENGTH);\n        System.arraycopy({var_name}_encoded, 0, buffer, offset, {var_name}_encoded.length);\n        offset += {var_name}_encoded.length;"
        else:
            # Other types
            return f"byte[] {var_name}_encoded = Encoder.{encoder_name}({field_name});\n        System.arraycopy({var_name}_encoded, 0, buffer, offset, {var_name}_encoded.length);\n        offset += {var_name}_encoded.length;"
    else:
        # Nested struct - call its encode()
        return f"byte[] {var_name}_encoded = {field_name}.encode();\n        System.arraycopy({var_name}_encoded, 0, buffer, offset, {var_name}_encoded.length);\n        offset += {var_name}_encoded.length;"


def _get_decoder_call(field_name: str, field_type: str, java_type: str, type_registry: TypeRegistry) -> str:
    """
    Generate Encoder method call for decoding a field.

    Returns:
        Java code lines calling appropriate Encoder method
    """
    base_type = field_type.split('[')[0]

    if not type_registry.is_atomic(base_type):
        raise ValueError(f"Unknown type: {base_type}")

    atomic = type_registry.get(base_type)

    if atomic.is_builtin:
        # Call Encoder.decodeXXX()
        decoder_name = f"decode{_capitalize_first(base_type)}"

        if base_type == 'string':
            # String needs max length parameter (using ProtocolConstants.STRING_MAX_LENGTH from config)
            return f"{java_type} {field_name} = Decoder.{decoder_name}(data, offset, ProtocolConstants.STRING_MAX_LENGTH);\n        offset += 1 + {field_name}.length();"
        else:
            # Other types - calculate size based on type
            encoded_size = _get_encoded_size(base_type, 0)  # Size hardcoded per type
            return f"{java_type} {field_name} = Decoder.{decoder_name}(data, offset);\n        offset += {encoded_size};"
    else:
        # Nested struct - call its decode()
        return f"{java_type} {field_name} = {java_type}.decode(data);\n        offset += {java_type}.MAX_PAYLOAD_SIZE;"


def _calculate_max_payload_size(fields: Sequence[FieldBase], type_registry: TypeRegistry, string_max_length: int) -> int:
    """
    Calculate maximum payload size in bytes (7-bit encoded).

    Args:
        fields: List of Field objects
        type_registry: TypeRegistry instance

    Returns:
        Maximum size in bytes
    """
    total_size = 0

    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            field_type_name = field.type_name.value
            base_type = field_type_name
            array_size = field.array if field.array else 1

            # Get size for base type
            if type_registry.is_atomic(base_type):
                atomic = type_registry.get(base_type)

                if atomic.is_builtin:
                    # Builtin type - use size_bytes from YAML
                    if atomic.size_bytes == 'variable':
                        # String: 1 byte length prefix + STRING_MAX_LENGTH chars
                        base_size = 1 + string_max_length  # From sysex_protocol_config.yaml
                    else:
                        if atomic.size_bytes is None or isinstance(atomic.size_bytes, str):
                            raise ValueError(f"Invalid size_bytes for {base_type}: {atomic.size_bytes}")
                        base_size = _get_encoded_size(base_type, atomic.size_bytes)
                else:
                    # Nested struct - not supported in Python-unified architecture
                    raise ValueError(f"Nested structs not supported: {base_type}")

                total_size += base_size * array_size
        else:  # Composite
            assert isinstance(field, CompositeField)
            # Recursively calculate size of nested fields
            nested_size = _calculate_max_payload_size(field.fields, type_registry, string_max_length)

            if field.array:
                # Array of composites: count byte + (nested_size × array_size)
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
        fields: List of Field objects
        type_registry: TypeRegistry instance
        string_max_length: Maximum string length (unused, kept for signature compatibility)

    Returns:
        Minimum size in bytes
    """
    total_size = 0

    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            field_type_name = field.type_name.value
            base_type = field_type_name
            array_size = field.array if field.array else 1

            # Get size for base type
            if type_registry.is_atomic(base_type):
                atomic = type_registry.get(base_type)

                if atomic.is_builtin:
                    # Builtin type - use size_bytes from YAML
                    if atomic.size_bytes == 'variable':
                        # String: 1 byte length prefix only (empty string)
                        base_size = 1
                    else:
                        if atomic.size_bytes is None or isinstance(atomic.size_bytes, str):
                            raise ValueError(f"Invalid size_bytes for {base_type}: {atomic.size_bytes}")
                        base_size = _get_encoded_size(base_type, atomic.size_bytes)
                else:
                    # Nested struct - not supported in Python-unified architecture
                    raise ValueError(f"Nested structs not supported: {base_type}")

                total_size += base_size * array_size
        else:  # Composite
            assert isinstance(field, CompositeField)
            # Recursively calculate size of nested fields
            nested_size = _calculate_min_payload_size(field.fields, type_registry, string_max_length)

            if field.array:
                # Array of composites: count byte + (nested_size × array_size)
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
    """
    if not s:
        return s
    words = s.split('_')
    return ''.join(word.capitalize() for word in words)


def _to_getter_name(field_name: str) -> str:
    """
    Convert field name to getter name.

    Examples:
        value → getValue
        isActive → isActive
        maxValue → getMaxValue

    Args:
        field_name: Field name

    Returns:
        Getter method name
    """
    # Special case for boolean fields starting with "is"
    if field_name.startswith('is') and len(field_name) > 2 and field_name[2].isupper():
        return field_name

    # Standard getter
    return f"get{field_name[0].upper()}{field_name[1:]}"


# ============================================================================
# COMPOSITE FIELD SUPPORT (Phase 5 - Java)
# ============================================================================

def _field_to_pascal_case(field_name: str) -> str:
    """
    Convert camelCase field name to PascalCase class name.

    Examples:
        pageInfo → PageInfo
        parameters → Parameters
        deviceName → DeviceName
    """
    if not field_name:
        return field_name
    return field_name[0].upper() + field_name[1:]


def _generate_inner_classes(fields: Sequence[FieldBase], type_registry: TypeRegistry, depth: int = 0) -> str:
    """
    Recursively generate inner static classes for composite fields.

    Returns empty string if no composites found.
    Safety: Max depth 3 to prevent infinite recursion.
    """
    if depth > 3:
        return ""

    classes: list[str] = []
    for field in fields:
        if field.is_composite():
            assert isinstance(field, CompositeField)
            # Generate nested composites first (depth-first like C++)
            nested = _generate_inner_classes(field.fields, type_registry, depth + 1)
            if nested:
                classes.append(nested)

            # Generate this composite class
            class_code = _generate_single_inner_class(field, type_registry)
            classes.append(class_code)

    return "\n".join(classes)


def _generate_single_inner_class(field: CompositeField, type_registry: TypeRegistry) -> str:
    """Generate a single inner static class for a composite field."""
    class_name = _field_to_pascal_case(field.name)

    lines: list[str] = [
        f"    // ============================================================================",
        f"    // Inner Class: {class_name}",
        f"    // ============================================================================",
        f"",
        f"    public static final class {class_name} {{"
    ]

    # Fields
    for nested_field in field.fields:
        if nested_field.is_primitive():
            assert isinstance(nested_field, PrimitiveField)
            java_type = _get_java_type(nested_field.type_name.value, type_registry)
            if nested_field.is_array():
                # Primitive array in inner class (e.g., childrenTypes which is uint8[4])
                lines.append(f"        private final {java_type}[] {nested_field.name};")
            else:
                lines.append(f"        private final {java_type} {nested_field.name};")
        else:  # Nested composite
            nested_class_name = _field_to_pascal_case(nested_field.name)
            if nested_field.array:
                lines.append(f"        private final List<{nested_class_name}> {nested_field.name};")
            else:
                lines.append(f"        private final {nested_class_name} {nested_field.name};")

    lines.append("")

    # Constructor
    params: list[str] = []
    for nested_field in field.fields:
        if nested_field.is_primitive():
            assert isinstance(nested_field, PrimitiveField)
            java_type = _get_java_type(nested_field.type_name.value, type_registry)
            if nested_field.is_array():
                params.append(f"{java_type}[] {nested_field.name}")
            else:
                params.append(f"{java_type} {nested_field.name}")
        else:
            nested_class_name = _field_to_pascal_case(nested_field.name)
            if nested_field.array:
                params.append(f"List<{nested_class_name}> {nested_field.name}")
            else:
                params.append(f"{nested_class_name} {nested_field.name}")

    param_str = ", ".join(params)
    lines.append(f"        public {class_name}({param_str}) {{")
    for nested_field in field.fields:
        lines.append(f"            this.{nested_field.name} = {nested_field.name};")
    lines.append("        }")
    lines.append("")

    # Getters
    for nested_field in field.fields:
        getter_name = _to_getter_name(nested_field.name)
        if nested_field.is_primitive():
            assert isinstance(nested_field, PrimitiveField)
            java_type = _get_java_type(nested_field.type_name.value, type_registry)
            if nested_field.is_array():
                java_type = f"{java_type}[]"
        else:
            nested_class_name = _field_to_pascal_case(nested_field.name)
            java_type = f"List<{nested_class_name}>" if nested_field.array else nested_class_name

        lines.append(f"        public {java_type} {getter_name}() {{")
        lines.append(f"            return {nested_field.name};")
        lines.append(f"        }}")
        lines.append("")

    lines.append("    }")
    lines.append("")

    return "\n".join(lines)


# ============================================================================
# IMPORT ANALYSIS HELPERS
# ============================================================================

def _needs_list_import(fields: Sequence[FieldBase]) -> bool:
    """Check if List import is needed (any array field)."""
    for field in fields:
        if field.is_array():
            return True
        if field.is_composite():
            assert isinstance(field, CompositeField)
            # Check nested fields recursively
            if _needs_list_import(field.fields):
                return True
    return False


def _needs_constants_import(fields: Sequence[FieldBase], type_registry: TypeRegistry) -> bool:
    """Check if ProtocolConstants import is needed (any string field)."""
    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField)
            if field.type_name.value == 'string':
                return True
        if field.is_composite():
            assert isinstance(field, CompositeField)
            # Check nested fields recursively
            if _needs_constants_import(field.fields, type_registry):
                return True
    return False
