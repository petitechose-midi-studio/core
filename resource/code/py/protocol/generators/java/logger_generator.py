"""
Java Logger Generator
Generates toString() methods for protocol message classes.

This generator creates toString() methods that output YAML format for debugging,
mirroring the C++ Logger implementation but adapted for Java.

Key Features:
- YAML multiline format with 2-space indentation
- StringBuilder for efficient string building (no static buffer needed)
- Handles composites, arrays, and all primitive types
- Float formatting with 4 decimal precision
- Edge case handling (NaN, Inf, -Inf)

Architecture:
- generate_log_method() generates toString() for each message class
- Called by struct_generator.py for each generated class
- Uses recursive field formatting to handle nested structures

Differences from C++:
- Uses StringBuilder instead of static buffer (GC-based memory)
- No memory warnings needed (StringBuilder is thread-safe per instance)
- Uses getter methods instead of direct field access
- String.format() for float formatting instead of custom function
"""

from __future__ import annotations

from typing import TYPE_CHECKING

# Import field classes for runtime isinstance checks
from protocol.field import FieldBase, PrimitiveField, CompositeField

if TYPE_CHECKING:
    from collections.abc import Sequence
    from protocol.type_loader import TypeRegistry


# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

def _to_camel_case(pascal_case: str) -> str:
    """
    Convert PascalCase to camelCase.

    Examples:
        DevicePageChange -> devicePageChange
        TransportPlay -> transportPlay
    """
    if not pascal_case:
        return pascal_case
    return pascal_case[0].lower() + pascal_case[1:]


def _get_indent_string(indent_level: int) -> str:
    """
    Get indentation string (2 spaces per level).

    Args:
        indent_level: Indentation depth (0 = no indent)

    Returns:
        String of spaces (2 * indent_level)
    """
    return "  " * indent_level


def _to_getter_name(field_name: str, is_boolean: bool = False) -> str:
    """
    Convert field name to Java getter method name.

    Args:
        field_name: Field name (camelCase)
        is_boolean: True if field is boolean type

    Returns:
        Getter method name (e.g., "getValue" or "isPlaying")

    Examples:
        ("value", False) -> "getValue"
        ("isPlaying", True) -> "isPlaying"
        ("deviceName", False) -> "getDeviceName"
    """
    # Boolean getters starting with "is" keep the "is" prefix
    if is_boolean and field_name.startswith('is') and len(field_name) > 2 and field_name[2].isupper():
        return field_name

    # Standard getter: capitalize first letter and prepend "get"
    return 'get' + field_name[0].upper() + field_name[1:]


def _get_java_type_name(type_name: str, _type_registry: TypeRegistry) -> str:
    """
    Get Java type name from protocol type name.

    Args:
        type_name: Protocol type name (e.g., "uint8", "string", "float32")
        type_registry: Type registry for lookups

    Returns:
        Java type name (e.g., "int", "String", "float")
    """
    # Map protocol types to Java types
    type_map = {
        'bool': 'boolean',
        'uint8': 'int',
        'uint16': 'int',
        'uint32': 'long',
        'int8': 'byte',
        'int16': 'short',
        'int32': 'int',
        'float32': 'float',
        'string': 'String'
    }

    return type_map.get(type_name, type_name)


# ============================================================================
# FIELD FORMATTING
# ============================================================================

def _format_field_for_log(
    field: FieldBase,
    type_registry: TypeRegistry,
    indent: int
) -> list[str]:
    """
    Route field to appropriate formatting function.

    Args:
        field: Field to format
        type_registry: Type registry for type lookups
        indent: Current indentation level

    Returns:
        List of Java code lines for formatting this field
    """
    if field.is_primitive():
        assert isinstance(field, PrimitiveField), "Primitive field must be PrimitiveField instance"
        if field.is_array():
            return _format_primitive_array(field, type_registry, indent)
        else:
            return _format_primitive_scalar(field, type_registry, indent)
    else:  # Composite
        assert isinstance(field, CompositeField), "Composite field must be CompositeField instance"
        if field.array:
            return _format_composite_array(field, type_registry, indent)
        else:
            return _format_composite_scalar(field, type_registry, indent)


def _format_primitive_scalar(
    field: PrimitiveField,
    type_registry: TypeRegistry,
    indent: int
) -> list[str]:
    """
    Format primitive scalar field (e.g., bool, int, float, string).

    Args:
        field: Primitive field to format
        type_registry: Type registry
        indent: Indentation level

    Returns:
        List of Java code lines

    Examples:
        bool:   sb.append("  isPlaying: ").append(isPlaying ? "true" : "false").append("\n");
        string: sb.append("  name: \"").append(name).append("\"\n");
        int:    sb.append("  count: ").append(count).append("\n");
        float:  sb.append("  value: ").append(formatFloat(value)).append("\n");
    """
    indent_str = _get_indent_string(indent + 1)
    type_name = field.type_name.value
    java_type = _get_java_type_name(type_name, type_registry)
    is_boolean = java_type == 'boolean'
    getter = _to_getter_name(field.name, is_boolean)

    lines: list[str] = []

    if type_name == 'bool':
        # Boolean: true/false (no quotes)
        lines.append(f'        sb.append("{indent_str}{field.name}: ").append({getter}() ? "true" : "false").append("\\n");')
    elif type_name == 'string':
        # String: "value" (with quotes)
        lines.append(f'        sb.append("{indent_str}{field.name}: \\"").append({getter}()).append("\\"\\n");')
    elif type_name == 'float32':
        # Float: 0.7500 (4 decimals via formatFloat)
        lines.append(f'        sb.append("{indent_str}{field.name}: ").append(formatFloat({getter}())).append("\\n");')
    else:
        # Integer types: direct append
        lines.append(f'        sb.append("{indent_str}{field.name}: ").append({getter}()).append("\\n");')

    return lines


def _format_primitive_array(
    field: PrimitiveField,
    type_registry: TypeRegistry,
    indent: int
) -> list[str]:
    """
    Format primitive array field (e.g., string[], int[]).

    Args:
        field: Primitive array field
        type_registry: Type registry
        indent: Indentation level

    Returns:
        List of Java code lines

    Format:
        items:
          - "item1"
          - "item2"

        Or if empty:
        items: []
    """
    indent_str = _get_indent_string(indent + 1)
    item_indent_str = _get_indent_string(indent + 2)
    type_name = field.type_name.value
    java_type = _get_java_type_name(type_name, type_registry)
    getter = _to_getter_name(field.name, False)

    lines: list[str] = []
    lines.append(f'        sb.append("{indent_str}{field.name}:");')
    lines.append(f'        if ({getter}().isEmpty()) {{')
    lines.append(f'            sb.append(" []\\n");')
    lines.append(f'        }} else {{')
    lines.append(f'            sb.append("\\n");')

    # Loop through array items
    if type_name == 'string':
        # String array: with quotes
        lines.append(f'            for (String item : {getter}()) {{')
        lines.append(f'                sb.append("{item_indent_str}- \\"").append(item).append("\\"\\n");')
        lines.append(f'            }}')
    elif type_name == 'float32':
        # Float array: with formatFloat
        lines.append(f'            for (float item : {getter}()) {{')
        lines.append(f'                sb.append("{item_indent_str}- ").append(formatFloat(item)).append("\\n");')
        lines.append(f'            }}')
    elif type_name == 'bool':
        # Boolean array
        lines.append(f'            for (boolean item : {getter}()) {{')
        lines.append(f'                sb.append("{item_indent_str}- ").append(item ? "true" : "false").append("\\n");')
        lines.append(f'            }}')
    else:
        # Integer arrays
        lines.append(f'            for ({java_type} item : {getter}()) {{')
        lines.append(f'                sb.append("{item_indent_str}- ").append(item).append("\\n");')
        lines.append(f'            }}')

    lines.append(f'        }}')

    return lines


def _format_composite_scalar(
    field: CompositeField,
    type_registry: TypeRegistry,
    indent: int
) -> list[str]:
    """
    Format composite scalar field (nested struct).

    Args:
        field: Composite field
        type_registry: Type registry
        indent: Indentation level

    Returns:
        List of Java code lines

    Format:
        pageInfo:
          devicePageIndex: 2
          devicePageCount: 5
    """
    indent_str = _get_indent_string(indent + 1)
    getter = _to_getter_name(field.name, False)

    lines: list[str] = []
    lines.append(f'        sb.append("{indent_str}{field.name}:\\n");')

    # Format nested fields - need to access via composite object
    for nested_field in field.fields:
        nested_lines = _format_composite_nested_field(nested_field, type_registry, indent + 1, getter)
        lines.extend(nested_lines)

    return lines


def _format_composite_nested_field(
    field: FieldBase,
    type_registry: TypeRegistry,
    indent: int,
    parent_getter: str
) -> list[str]:
    """
    Format a field that's nested inside a composite scalar.

    Args:
        field: Nested field to format
        type_registry: Type registry
        indent: Indentation level
        parent_getter: Getter name of parent composite (e.g., "getPageInfo")

    Returns:
        List of Java code lines
    """
    indent_str = _get_indent_string(indent + 1)

    if field.is_primitive():
        assert isinstance(field, PrimitiveField), "Primitive field must be PrimitiveField instance"
        type_name = field.type_name.value
        java_type = _get_java_type_name(type_name, type_registry)
        is_boolean = java_type == 'boolean'
        nested_getter = _to_getter_name(field.name, is_boolean)

        lines: list[str] = []

        if type_name == 'bool':
            lines.append(f'        sb.append("{indent_str}{field.name}: ").append({parent_getter}().{nested_getter}() ? "true" : "false").append("\\n");')
        elif type_name == 'string':
            lines.append(f'        sb.append("{indent_str}{field.name}: \\"").append({parent_getter}().{nested_getter}()).append("\\"\\n");')
        elif type_name == 'float32':
            lines.append(f'        sb.append("{indent_str}{field.name}: ").append(formatFloat({parent_getter}().{nested_getter}())).append("\\n");')
        else:
            lines.append(f'        sb.append("{indent_str}{field.name}: ").append({parent_getter}().{nested_getter}()).append("\\n");')

        return lines
    else:
        # Nested composite - would need recursive handling
        # For now, not implemented as it's not used in current messages
        return []


def _format_composite_array(
    field: CompositeField,
    _type_registry: TypeRegistry,
    indent: int
) -> list[str]:
    """
    Format array of composite fields.

    Args:
        field: Composite array field
        type_registry: Type registry
        indent: Indentation level

    Returns:
        List of Java code lines

    Format:
        macros:
          - parameterIndex: 0
            parameterValue: 0.7500
          - parameterIndex: 1
            parameterValue: 0.3200
    """
    indent_str = _get_indent_string(indent + 1)
    item_indent_str = _get_indent_string(indent + 2)
    getter = _to_getter_name(field.name, False)

    # Capitalize first letter of field name for inner class type
    class_name = field.name[0].upper() + field.name[1:]

    lines: list[str] = []
    lines.append(f'        sb.append("{indent_str}{field.name}:\\n");')
    lines.append(f'        for ({class_name} item : {getter}()) {{')

    # First field gets "- " prefix, rest are indented normally
    first_field = True
    for nested_field in field.fields:
        is_bool = (isinstance(nested_field, PrimitiveField) and
                   nested_field.is_primitive() and
                   nested_field.type_name.value == 'bool')
        nested_getter = _to_getter_name(nested_field.name, is_bool)

        if nested_field.is_primitive() and not nested_field.is_array():
            # Primitive scalar in composite array
            assert isinstance(nested_field, PrimitiveField), "Primitive field must be PrimitiveField instance"
            type_name = nested_field.type_name.value

            if first_field:
                # First field: inline with "- "
                if type_name == 'bool':
                    lines.append(f'            sb.append("{item_indent_str}- {nested_field.name}: ").append(item.{nested_getter}() ? "true" : "false").append("\\n");')
                elif type_name == 'string':
                    lines.append(f'            sb.append("{item_indent_str}- {nested_field.name}: \\"").append(item.{nested_getter}()).append("\\"\\n");')
                elif type_name == 'float32':
                    lines.append(f'            sb.append("{item_indent_str}- {nested_field.name}: ").append(formatFloat(item.{nested_getter}())).append("\\n");')
                else:
                    lines.append(f'            sb.append("{item_indent_str}- {nested_field.name}: ").append(item.{nested_getter}()).append("\\n");')
                first_field = False
            else:
                # Subsequent fields: indented (2 extra spaces after "- ")
                field_indent = item_indent_str + "  "
                if type_name == 'bool':
                    lines.append(f'            sb.append("{field_indent}{nested_field.name}: ").append(item.{nested_getter}() ? "true" : "false").append("\\n");')
                elif type_name == 'string':
                    lines.append(f'            sb.append("{field_indent}{nested_field.name}: \\"").append(item.{nested_getter}()).append("\\"\\n");')
                elif type_name == 'float32':
                    lines.append(f'            sb.append("{field_indent}{nested_field.name}: ").append(formatFloat(item.{nested_getter}())).append("\\n");')
                else:
                    lines.append(f'            sb.append("{field_indent}{nested_field.name}: ").append(item.{nested_getter}()).append("\\n");')
        else:
            # Arrays or nested composites need special handling
            # For now, we'll handle them similarly but may need refinement
            # TODO: Handle nested arrays and composites within composite arrays if needed
            pass

    lines.append(f'        }}')

    return lines


# ============================================================================
# HELPER: DETECT FLOAT FIELDS
# ============================================================================

def _contains_float_fields(fields: Sequence[FieldBase]) -> bool:
    """
    Check if any field (or nested field) contains float32 type.

    Args:
        fields: List of fields to check

    Returns:
        True if any field contains float32, False otherwise
    """
    for field in fields:
        if field.is_primitive():
            assert isinstance(field, PrimitiveField), "Primitive field must be PrimitiveField"
            if field.type_name.value == 'float32':
                return True
        elif field.is_composite():
            assert isinstance(field, CompositeField), "Composite field must be CompositeField"
            # Recursively check nested fields
            if _contains_float_fields(field.fields):
                return True
    return False


# ============================================================================
# MAIN GENERATION FUNCTION
# ============================================================================

def generate_log_method(
    class_name: str,
    fields: Sequence[FieldBase],
    type_registry: TypeRegistry
) -> str:
    """
    Generate toString() method for Java message class.

    Args:
        class_name: Class name (e.g., "DevicePageChangeMessage")
        fields: List of fields in the message
        type_registry: Type registry for type lookups

    Returns:
        Java code for toString() method

    Format:
        @Override
        public String toString() {
            StringBuilder sb = new StringBuilder(256);
            sb.append("# DevicePageChange\\n");
            sb.append("devicePageChange:\\n");
            // ... field formatting
            return sb.toString();
        }
    """
    # Remove "Message" suffix for display name
    display_name = class_name.replace("Message", "")
    message_key = _to_camel_case(display_name)

    # Check if message contains any float fields
    has_floats = _contains_float_fields(fields)

    lines = [
        "    // ============================================================================",
        "    // Logging",
        "    // ============================================================================",
        "    ",
    ]

    # Only generate formatFloat if message contains float fields
    if has_floats:
        lines.extend([
            "    /**",
            "     * Format float with 4 decimal places, handling edge cases.",
            "     * ",
            "     * @param value Float value to format",
            "     * @return Formatted string (e.g., \"3.1416\", \"NaN\", \"Inf\")",
            "     */",
            "    private static String formatFloat(float value) {",
            "        if (Float.isNaN(value)) return \"NaN\";",
            "        if (Float.isInfinite(value)) return value > 0 ? \"Inf\" : \"-Inf\";",
            "        return String.format(\"%.4f\", value);",
            "    }",
            "    ",
        ])

    lines.extend([
        "    /**",
        "     * Convert message to YAML format for logging.",
        "     * ",
        "     * @return YAML string representation",
        "     */",
        "    @Override",
        "    public String toString() {",
        "        StringBuilder sb = new StringBuilder(256);",
        f'        sb.append("# {display_name}\\n");',
        f'        sb.append("{message_key}:\\n");',
    ])

    # Generate field formatting
    for field in fields:
        field_lines = _format_field_for_log(field, type_registry, indent=0)
        lines.extend(field_lines)

    lines.extend([
        "        return sb.toString();",
        "    }",
    ])

    return "\n".join(lines)
