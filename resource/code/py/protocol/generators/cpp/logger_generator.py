"""
C++ Logger Generator
Generates Logger.hpp utility file and toString() methods for protocol messages.

This generator creates:
1. Logger.hpp - Utility functions for float formatting (floatToString)
2. toString() methods - YAML representation for each message struct

Key Features:
- floatToString(): Hybrid snprintf approach with 4 decimal precision
- YAML multiline format with 2-space indentation
- 32KB EXTMEM static buffer (shared across all messages)
- Handles composites, arrays, and all primitive types
- No truncation - displays all fields and array elements

Architecture:
- Logger.hpp is generated once by generate_logger_hpp()
- toString() is generated per-message by generate_log_method()
- struct_generator.py calls generate_log_method() for each struct
"""

from __future__ import annotations

from typing import TYPE_CHECKING

# Import field classes for runtime isinstance checks
from protocol.field import FieldBase, PrimitiveField, CompositeField

if TYPE_CHECKING:
    from collections.abc import Sequence
    from pathlib import Path
    from protocol.type_loader import TypeRegistry


# ============================================================================
# LOGGER.HPP GENERATION (utility file)
# ============================================================================

def generate_logger_hpp(output_path: Path) -> str:
    """
    Generate Logger.hpp utility file with floatToString() function.

    This file is generated once and provides shared utilities for all message
    toString() methods.

    Args:
        output_path: Path where Logger.hpp will be written

    Returns:
        Generated C++ code as string
    """
    return '''/**
 * Logger.hpp - Protocol Logging Utilities
 *
 * AUTO-GENERATED - DO NOT EDIT
 *
 * Provides utility functions for converting protocol messages to human-readable
 * YAML format for debugging via Serial USB.
 *
 * Key Features:
 * - floatToString(): Hybrid snprintf approach for precise float formatting
 * - Handles edge cases: NaN, Inf, -Inf
 * - 4 decimal places precision
 * - Optimized for embedded systems (no dynamic allocation)
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>

namespace Protocol {

/**
 * Convert float to string with 4 decimal places using hybrid approach.
 *
 * Edge Cases Handled:
 * - NaN → "NaN"
 * - +Infinity → "Inf"
 * - -Infinity → "-Inf"
 * - Normal values → "123.4567" format
 *
 * Hybrid Algorithm:
 * 1. Use snprintf for integer part and formatting (reliable, fast)
 * 2. Manual extraction of 4 decimals with proper rounding
 * 3. Handle rounding overflow (e.g., 9.9999 → 10.0000)
 *
 * @param buffer Output buffer (must have at least 16 bytes)
 * @param bufferSize Size of output buffer
 * @param value Float value to convert
 * @return Number of characters written (excluding null terminator)
 *
 * Examples:
 *   3.14159 → "3.1416" (rounded)
 *   -0.5    → "-0.5000"
 *   0.0     → "0.0000"
 *   NaN     → "NaN"
 */
static inline int floatToString(char* buffer, size_t bufferSize, float value) {
    // Edge case 1: NaN (Not a Number)
    if (isnan(value)) {
        return snprintf(buffer, bufferSize, "NaN");
    }

    // Edge case 2: Infinity (positive or negative)
    if (isinf(value)) {
        return snprintf(buffer, bufferSize, value > 0 ? "Inf" : "-Inf");
    }

    // Handle negative numbers
    bool negative = value < 0;
    if (negative) {
        value = -value;  // Work with absolute value
    }

    // Extract integer part
    int32_t intPart = (int32_t)value;

    // Extract 4 decimal places with rounding
    // Multiply by 10000 to shift 4 decimals to integer range
    // Add 0.5 for proper rounding (banker's rounding)
    int32_t fracPart = (int32_t)((value - intPart) * 10000.0f + 0.5f);

    // Handle rounding overflow
    // Example: 9.99995 → intPart=9, fracPart=10000 → should become 10.0000
    if (fracPart >= 10000) {
        intPart++;
        fracPart = 0;
    }

    // Use snprintf for final formatting
    // Format: [sign]integer.decimal (with leading zeros on decimal)
    // Use %ld for portability (int32_t may be long on some platforms)
    return snprintf(buffer, bufferSize, "%s%ld.%04ld",
                    negative ? "-" : "",  // Add negative sign if needed
                    (long)intPart,        // Integer part (cast for portability)
                    (long)fracPart);      // Fractional part (4 digits, zero-padded)
}

}  // namespace Protocol
'''


# ============================================================================
# TOSTRING() METHOD GENERATION (per-message)
# ============================================================================

def generate_log_method(struct_name: str, fields: Sequence[FieldBase], type_registry: TypeRegistry) -> str:
    """
    Generate toString() method for a message struct.

    Returns YAML multiline format with 2-space indentation:

    # MessageName
    messageName:
      field1: value
      field2:
        nested: value
      arrayField:
        - item: value

    Args:
        struct_name: Name of the struct (e.g., "DevicePageChangeMessage")
        fields: List of Field objects (primitive or composite)
        type_registry: TypeRegistry for type resolution

    Returns:
        C++ code for the toString() method
    """
    # Convert PascalCase to camelCase for YAML key
    message_key = _to_camel_case(struct_name.replace("Message", ""))

    lines = [
        "    /**",
        "     * Convert message to YAML format for logging",
        "     * ",
        "     * WARNING: Uses static 32KB buffer - log immediately!",
        "     * Multiple calls will overwrite previous results.",
        "     * ",
        "     * @return YAML string representation",
        "     */",
        "    const char* toString() const {",
        "        #ifdef EXTMEM",
        "        static EXTMEM char buffer[32768];  // Use external memory on Teensy",
        "        #else",
        "        static char buffer[32768];  // Standard static buffer",
        "        #endif",
        "        char* ptr = buffer;",
        "        const char* end = buffer + sizeof(buffer) - 1;",
        "        ",
    ]

    # Generate YAML header: # MessageName\nmessageName:
    lines.append(f'        ptr += snprintf(ptr, end - ptr, "# {struct_name.replace("Message", "")}\\n{message_key}:\\n");')
    lines.append("        ")

    # Generate field formatting code
    for i, field in enumerate(fields):
        is_last = (i == len(fields) - 1)
        field_lines = _format_field_for_log(field, type_registry, indent=1, is_last=is_last)
        lines.extend(field_lines)

    lines.extend([
        "        ",
        "        *ptr = '\\0';",
        "        return buffer;",
        "    }",
        ""
    ])

    return "\n".join(lines)


# ============================================================================
# FIELD FORMATTING (recursive)
# ============================================================================

def _format_field_for_log(
    field: FieldBase,
    type_registry: TypeRegistry,
    indent: int,
    is_last: bool
) -> list[str]:
    """
    Route to appropriate formatter based on field type.

    Args:
        field: Field to format
        type_registry: Type registry
        indent: Current indentation level (0 = root)
        is_last: Whether this is the last field at this level

    Returns:
        List of C++ code lines
    """
    if field.is_primitive():
        assert isinstance(field, PrimitiveField)
        if field.is_array():
            return _format_primitive_array(field, type_registry, indent, is_last)
        else:
            return _format_primitive_scalar(field, type_registry, indent, is_last)
    else:
        assert isinstance(field, CompositeField)
        if field.array:
            return _format_composite_array(field, type_registry, indent, is_last)
        else:
            return _format_composite_scalar(field, type_registry, indent, is_last)


def _format_primitive_scalar(
    field: PrimitiveField,
    type_registry: TypeRegistry,
    indent: int,
    is_last: bool
) -> list[str]:
    """
    Format a primitive scalar field.

    Examples:
      fieldName: 123
      stringField: "value"
      boolField: true
      floatField: 0.7500
    """
    indent_str = _get_indent_string(indent)
    field_access = field.name  # e.g., "pageInfo.devicePageIndex" or "macros[i].parameterIndex"
    field_display = _get_display_name(field.name)  # e.g., "devicePageIndex" or "parameterIndex"
    type_name = field.type_name.value

    # Get format specifier based on type
    if type_name == 'bool':
        # Boolean: true/false (no quotes)
        return [f'        ptr += snprintf(ptr, end - ptr, "{indent_str}{field_display}: %s\\n", {field_access} ? "true" : "false");']

    elif type_name == 'string':
        # String: "value" (with quotes)
        return [f'        ptr += snprintf(ptr, end - ptr, "{indent_str}{field_display}: \\"%s\\"\\n", {field_access}.c_str());']

    elif type_name in ('uint8', 'uint16', 'uint32'):
        # Unsigned integers: cast to unsigned long for %lu (portable across platforms)
        return [f'        ptr += snprintf(ptr, end - ptr, "{indent_str}{field_display}: %lu\\n", (unsigned long){field_access});']

    elif type_name in ('int8', 'int16', 'int32'):
        # Signed integers: cast to long for %ld (portable across platforms)
        return [f'        ptr += snprintf(ptr, end - ptr, "{indent_str}{field_display}: %ld\\n", (long){field_access});']

    elif type_name == 'float32':
        # Float: use floatToString() for 4 decimal precision
        # Create a safe variable name (no dots, brackets, etc.)
        safe_var_name = _make_safe_var_name(field.name)
        lines = [
            f"        {{",
            f"            char floatBuf_{safe_var_name}[16];",
            f"            floatToString(floatBuf_{safe_var_name}, sizeof(floatBuf_{safe_var_name}), {field_access});",
            f'            ptr += snprintf(ptr, end - ptr, "{indent_str}{field_display}: %s\\n", floatBuf_{safe_var_name});',
            f"        }}"
        ]
        return lines

    else:
        raise ValueError(f"Unknown primitive type: {type_name}")


def _format_primitive_array(
    field: PrimitiveField,
    type_registry: TypeRegistry,
    indent: int,
    is_last: bool
) -> list[str]:
    """
    Format a primitive array field.

    Examples:
      arrayField:
        - "item1"
        - "item2"

      emptyArray: []
    """
    indent_str = _get_indent_string(indent)
    next_indent_str = _get_indent_string(indent + 1)
    field_access = field.name  # e.g., "tags" or "macros[i].discreteValueNames"
    field_display = _get_display_name(field.name)
    type_name = field.type_name.value

    # Determine loop index variable (avoid conflicts with existing [i])
    loop_var = _get_next_loop_var(field.name)

    lines: list[str] = []

    # Check if array is empty
    lines.append(f'        ptr += snprintf(ptr, end - ptr, "{indent_str}{field_display}:");')
    lines.append(f'        if ({field_access}.size() == 0) {{')
    lines.append(f'            ptr += snprintf(ptr, end - ptr, " []\\n");')
    lines.append(f'        }} else {{')
    lines.append(f'            ptr += snprintf(ptr, end - ptr, "\\n");')

    # Loop over array items
    if type_name == 'bool':
        lines.append(f'            for (size_t {loop_var} = 0; {loop_var} < {field_access}.size(); ++{loop_var}) {{')
        lines.append(f'                ptr += snprintf(ptr, end - ptr, "{next_indent_str}- %s\\n", {field_access}[{loop_var}] ? "true" : "false");')
        lines.append(f'            }}')

    elif type_name == 'string':
        lines.append(f'            for (size_t {loop_var} = 0; {loop_var} < {field_access}.size(); ++{loop_var}) {{')
        lines.append(f'                ptr += snprintf(ptr, end - ptr, "{next_indent_str}- \\"%s\\"\\n", {field_access}[{loop_var}].c_str());')
        lines.append(f'            }}')

    elif type_name in ('uint8', 'uint16', 'uint32'):
        lines.append(f'            for (size_t {loop_var} = 0; {loop_var} < {field_access}.size(); ++{loop_var}) {{')
        lines.append(f'                ptr += snprintf(ptr, end - ptr, "{next_indent_str}- %lu\\n", (unsigned long){field_access}[{loop_var}]);')
        lines.append(f'            }}')

    elif type_name in ('int8', 'int16', 'int32'):
        lines.append(f'            for (size_t {loop_var} = 0; {loop_var} < {field_access}.size(); ++{loop_var}) {{')
        lines.append(f'                ptr += snprintf(ptr, end - ptr, "{next_indent_str}- %ld\\n", (long){field_access}[{loop_var}]);')
        lines.append(f'            }}')

    elif type_name == 'float32':
        safe_var_name = _make_safe_var_name(field.name)
        lines.append(f'            {{')
        lines.append(f'                char floatBuf_{safe_var_name}[16];')
        lines.append(f'                for (size_t {loop_var} = 0; {loop_var} < {field_access}.size(); ++{loop_var}) {{')
        lines.append(f'                    floatToString(floatBuf_{safe_var_name}, sizeof(floatBuf_{safe_var_name}), {field_access}[{loop_var}]);')
        lines.append(f'                    ptr += snprintf(ptr, end - ptr, "{next_indent_str}- %s\\n", floatBuf_{safe_var_name});')
        lines.append(f'                }}')
        lines.append(f'            }}')

    else:
        raise ValueError(f"Unknown primitive type: {type_name}")

    lines.append(f'        }}')

    return lines


def _format_composite_scalar(
    field: CompositeField,
    type_registry: TypeRegistry,
    indent: int,
    is_last: bool
) -> list[str]:
    """
    Format a single composite (struct) field.

    Example:
      pageInfo:
        devicePageIndex: 0
        devicePageName: "Main"
    """
    indent_str = _get_indent_string(indent)
    field_name = field.name

    lines: list[str] = []
    lines.append(f'        ptr += snprintf(ptr, end - ptr, "{indent_str}{field_name}:\\n");')

    # Format nested fields
    for i, nested_field in enumerate(field.fields):
        is_last_nested = (i == len(field.fields) - 1)
        # Access nested field via field_name.nested_field_name
        nested_field_copy = _create_prefixed_field(nested_field, field_name)
        nested_lines = _format_field_for_log(nested_field_copy, type_registry, indent + 1, is_last_nested)
        lines.extend(nested_lines)

    return lines


def _format_composite_array(
    field: CompositeField,
    type_registry: TypeRegistry,
    indent: int,
    is_last: bool
) -> list[str]:
    """
    Format an array of composite (struct) fields.

    Example:
      macros:
        - parameterIndex: 0
          parameterValue: 0.7500
        - parameterIndex: 1
          parameterValue: 0.3200
    """
    indent_str = _get_indent_string(indent)
    field_name = field.name

    lines: list[str] = []

    # Array header
    lines.append(f'        ptr += snprintf(ptr, end - ptr, "{indent_str}{field_name}:\\n");')

    # Loop over array items
    lines.append(f'        for (size_t i = 0; i < {field_name}.size(); ++i) {{')

    # Format each nested field with inline style for first field
    for j, nested_field in enumerate(field.fields):
        is_first = (j == 0)
        is_last_nested = (j == len(field.fields) - 1)

        # Access via field_name[i].nested_field_name
        nested_field_copy = _create_array_indexed_field(nested_field, field_name)

        if is_first:
            # First field: inline with "- " prefix
            nested_lines = _format_field_for_log_inline(nested_field_copy, indent + 1)
        else:
            # Subsequent fields: normal indentation (indent + 2)
            nested_lines = _format_field_for_log(nested_field_copy, type_registry, indent + 2, is_last_nested)

        lines.extend(nested_lines)

    lines.append(f'        }}')

    return lines


def _format_field_for_log_inline(
    field: FieldBase,
    indent: int
) -> list[str]:
    """
    Format first field of array item with "- " prefix (inline style).

    Example:
        - parameterIndex: 0
    """
    # Only handle primitive scalars for inline (arrays and composites don't make sense inline)
    if not field.is_primitive():
        raise ValueError("Inline format only supports primitive scalar fields")

    assert isinstance(field, PrimitiveField)

    if field.is_array():
        raise ValueError("Inline format doesn't support array fields")

    # Generate with "- " prefix instead of normal indentation
    indent_str = _get_indent_string(indent)
    access_path = field.name
    display_name = _get_display_name(field.name)
    type_name = field.type_name.value

    # Get format specifier based on type
    if type_name == 'bool':
        return [f'            ptr += snprintf(ptr, end - ptr, "{indent_str}- {display_name}: %s\\n", {access_path} ? "true" : "false");']

    elif type_name == 'string':
        return [f'            ptr += snprintf(ptr, end - ptr, "{indent_str}- {display_name}: \\"%s\\"\\n", {access_path}.c_str());']

    elif type_name in ('uint8', 'uint16', 'uint32'):
        return [f'            ptr += snprintf(ptr, end - ptr, "{indent_str}- {display_name}: %lu\\n", (unsigned long){access_path});']

    elif type_name in ('int8', 'int16', 'int32'):
        return [f'            ptr += snprintf(ptr, end - ptr, "{indent_str}- {display_name}: %ld\\n", (long){access_path});']

    elif type_name == 'float32':
        safe_var_name = _make_safe_var_name(field.name)
        lines = [
            f"            {{",
            f"                char floatBuf_{safe_var_name}[16];",
            f"                floatToString(floatBuf_{safe_var_name}, sizeof(floatBuf_{safe_var_name}), {access_path});",
            f'                ptr += snprintf(ptr, end - ptr, "{indent_str}- {display_name}: %s\\n", floatBuf_{safe_var_name});',
            f"            }}"
        ]
        return lines

    else:
        raise ValueError(f"Unknown primitive type: {type_name}")


# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

def _get_indent_string(indent_level: int) -> str:
    """
    Get indentation string for YAML (2 spaces per level).

    Args:
        indent_level: Indentation level (0 = root)

    Returns:
        Indentation string (e.g., "  " for level 1, "    " for level 2)
    """
    return "  " * indent_level


def _get_display_name(field_access: str) -> str:
    """
    Extract display name from field access path.

    Examples:
        "devicePageIndex" → "devicePageIndex"
        "pageInfo.devicePageIndex" → "devicePageIndex"
        "macros[i].parameterIndex" → "parameterIndex"
    """
    # Split by both '.' and '[' to get last component
    parts = field_access.replace('[', '.').replace(']', '').split('.')
    return parts[-1]


def _make_safe_var_name(field_access: str) -> str:
    """
    Convert field access path to safe C++ variable name.

    Replaces dots, brackets with underscores.

    Examples:
        "parameterValue" → "parameterValue"
        "pageInfo.devicePageIndex" → "pageInfo_devicePageIndex"
        "macros[i].parameterValue" → "macros_i_parameterValue"
    """
    safe = field_access.replace('.', '_').replace('[', '_').replace(']', '')
    return safe


def _get_next_loop_var(field_access: str) -> str:
    """
    Determine next available loop variable name based on field access depth.

    Avoids conflicts by using different variable names based on nesting:
    - Top level: 'i'
    - Nested once (has [i]): 'j'
    - Nested twice (has [i] and [j]): 'k'

    Examples:
        "macros" → "i"
        "macros[i].discreteValueNames" → "j"
        "foo[i].bar[j].items" → "k"
    """
    # Count how many loop indices already exist in the path
    depth = field_access.count('[')

    loop_vars = ['i', 'j', 'k', 'l', 'm', 'n']
    if depth < len(loop_vars):
        return loop_vars[depth]
    else:
        # Fallback for deeply nested loops (unlikely)
        return f'idx{depth}'


def _to_camel_case(pascal_name: str) -> str:
    """
    Convert PascalCase to camelCase.

    Examples:
        DevicePageChange → devicePageChange
        TransportPlay → transportPlay
    """
    if not pascal_name:
        return pascal_name
    return pascal_name[0].lower() + pascal_name[1:]


def _create_prefixed_field(field: FieldBase, prefix: str) -> FieldBase:
    """
    Create a copy of field with prefixed name for nested access.

    Example:
        field.name = "devicePageIndex"
        prefix = "pageInfo"
        result.name = "pageInfo.devicePageIndex"
    """
    if field.is_primitive():
        assert isinstance(field, PrimitiveField)
        return PrimitiveField(
            name=f"{prefix}.{field.name}",
            type_name=field.type_name,
            array=field.array
        )
    else:
        assert isinstance(field, CompositeField)
        return CompositeField(
            name=f"{prefix}.{field.name}",
            fields=field.fields,
            array=field.array
        )


def _create_array_indexed_field(field: FieldBase, array_name: str) -> FieldBase:
    """
    Create a copy of field with array indexing for nested access.

    Example:
        field.name = "parameterIndex"
        array_name = "macros"
        result.name = "macros[i].parameterIndex"
    """
    if field.is_primitive():
        assert isinstance(field, PrimitiveField)
        return PrimitiveField(
            name=f"{array_name}[i].{field.name}",
            type_name=field.type_name,
            array=field.array
        )
    else:
        assert isinstance(field, CompositeField)
        return CompositeField(
            name=f"{array_name}[i].{field.name}",
            fields=field.fields,
            array=field.array
        )
