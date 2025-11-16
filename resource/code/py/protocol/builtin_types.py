"""
Built-in Primitive Types (Python-based, type-safe)

These are the fundamental data types used to compose atomic types.
Each type specifies its size, C++ type, and Java type mapping.

NOTE: Java has no unsigned types, so unsigned integers are mapped to
      the next larger signed type (uint8→int, uint16→int, uint32→long)

Architecture:
- No YAML parsing, pure Python with strong typing
- Immutable data classes for type safety
- Single source of truth for all type mappings
"""

from __future__ import annotations
from dataclasses import dataclass


@dataclass(frozen=True)
class BuiltinTypeDef:
    """Definition of a builtin primitive type"""
    name: str
    description: str
    size_bytes: int | str  # int for fixed size, 'variable' for strings
    cpp_type: str
    java_type: str


# ============================================================================
# Built-in Type Definitions
# ============================================================================

BUILTIN_TYPES: dict[str, BuiltinTypeDef] = {
    # Boolean
    'bool': BuiltinTypeDef(
        name='bool',
        description='Boolean value (true/false)',
        size_bytes=1,
        cpp_type='bool',
        java_type='boolean'
    ),

    # Unsigned integers
    'uint8': BuiltinTypeDef(
        name='uint8',
        description='8-bit unsigned integer (0-255)',
        size_bytes=1,
        cpp_type='uint8_t',
        java_type='int'  # Java has no unsigned, mapped to int
    ),

    'uint16': BuiltinTypeDef(
        name='uint16',
        description='16-bit unsigned integer (0-65535)',
        size_bytes=2,
        cpp_type='uint16_t',
        java_type='int'
    ),

    'uint32': BuiltinTypeDef(
        name='uint32',
        description='32-bit unsigned integer (0-4294967295)',
        size_bytes=4,
        cpp_type='uint32_t',
        java_type='long'  # Java int is only 32-bit signed
    ),

    # Signed integers
    'int8': BuiltinTypeDef(
        name='int8',
        description='8-bit signed integer (-128 to 127)',
        size_bytes=1,
        cpp_type='int8_t',
        java_type='byte'
    ),

    'int16': BuiltinTypeDef(
        name='int16',
        description='16-bit signed integer (-32768 to 32767)',
        size_bytes=2,
        cpp_type='int16_t',
        java_type='short'
    ),

    'int32': BuiltinTypeDef(
        name='int32',
        description='32-bit signed integer (-2147483648 to 2147483647)',
        size_bytes=4,
        cpp_type='int32_t',
        java_type='int'
    ),

    # Floating point
    'float32': BuiltinTypeDef(
        name='float32',
        description='32-bit IEEE 754 floating point',
        size_bytes=4,
        cpp_type='float',
        java_type='float'
    ),

    # String (variable length)
    'string': BuiltinTypeDef(
        name='string',
        description='Variable-length UTF-8 string (prefixed with uint8 length, max 16 chars)',
        size_bytes='variable',
        cpp_type='etl::string<STRING_MAX_LENGTH>',
        java_type='String'
    ),
}
