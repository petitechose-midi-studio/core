"""
Atomic Type Loader (Python-based, type-safe)

This module manages atomic types with strong typing and no YAML parsing.
It provides a TypeRegistry that manages both builtin types (primitives)
and custom atomic types.

Architecture:
- AtomicType: Data class representing a type definition
- TypeRegistry: Central registry for all types with validation
- No YAML, pure Python configuration
- Global registry pattern for easy access across modules

Single Responsibility: Load, store, and validate type definitions.
DRY Principle: Centralized type management, no duplication.
"""

from __future__ import annotations
from dataclasses import dataclass
from typing import cast

try:
    from .builtin_types import BUILTIN_TYPES as _BUILTIN_TYPES, BuiltinTypeDef
except ImportError:
    from builtin_types import BUILTIN_TYPES as _BUILTIN_TYPES, BuiltinTypeDef  # type: ignore[import-not-found]

BUILTIN_TYPES: dict[str, BuiltinTypeDef] = cast(dict[str, BuiltinTypeDef], _BUILTIN_TYPES)


@dataclass
class AtomicType:
    """
    Represents an atomic type (builtin or custom).

    An atomic type is either:
    - A builtin primitive (uint8, float32, etc.) with no fields
    - A custom composite type with fields referencing other types

    Attributes:
        name: Type name (e.g., 'uint8', 'ParameterValue')
        description: Human-readable description
        fields: list of (field_name, field_type) tuples (empty for builtins)
        is_builtin: True if this is a builtin primitive type
        size_bytes: Size in bytes (for builtins, may be 'variable')
        cpp_type: C++ type mapping (for builtins)
        java_type: Java type mapping (for builtins)
    """

    name: str
    description: str
    fields: list[tuple[str, str]]  # [(field_name, field_type_with_array)]
    is_builtin: bool = False
    size_bytes: int | str | None = None  # int or 'variable' for builtins
    cpp_type: str | None = None          # For builtins
    java_type: str | None = None         # For builtins

    def __str__(self) -> str:
        """String representation for debugging"""
        origin = "builtin" if self.is_builtin else "custom"
        return f"AtomicType({self.name}, {origin}, {len(self.fields)} fields)"


class TypeRegistry:
    """
    Registry for all atomic types (builtin + custom).

    This class manages the loading, storage, and validation of all types
    used in the protocol system. It enforces strict validation rules and
    collects all errors before failing.

    Design:
    - Load builtins first (they have no dependencies)
    - Load custom types second (they may reference builtins or other customs)
    - Validate all references after loading
    - Collect ALL errors before raising (helps with debugging)
    """

    def __init__(self):
        """Initialize empty registry"""
        self.types: dict[str, AtomicType] = {}
        self._errors: list[str] = []

    def load_builtins(self) -> None:
        """
        Load built-in primitive types from Python configuration.

        Builtin types have no fields (they are atomic primitives) but include
        mapping information for C++ and Java code generation.

        No arguments needed - types are defined in builtin_types.py
        """
        for type_name, builtin_def in BUILTIN_TYPES.items():
            self.types[type_name] = AtomicType(
                name=builtin_def.name,
                description=builtin_def.description,
                fields=[],  # Builtins have no fields
                is_builtin=True,
                size_bytes=builtin_def.size_bytes,
                cpp_type=builtin_def.cpp_type,
                java_type=builtin_def.java_type
            )

    def add_custom_type(
        self,
        name: str,
        description: str,
        fields: list[tuple[str, str]]
    ) -> None:
        """
        Add a custom atomic type programmatically.

        Custom types are composite structures built from primitives or other
        atomic types. They define fields with types and optional array notation.

        Args:
            name: Type name
            description: Human-readable description
            fields: List of (field_name, field_type) tuples

        Example:
            >>> registry.add_custom_type(
            ...     'ParameterValue',
            ...     'Parameter value with ID',
            ...     [('paramId', 'uint8'), ('value', 'float32')]
            ... )
        """
        self.types[name] = AtomicType(
            name=name,
            description=description,
            fields=fields,
            is_builtin=False
        )

    def validate_references(self):
        """
        Validate that all field types reference existing types.

        This method checks every field in every custom type to ensure:
        1. The referenced type exists (either builtin or custom)
        2. Array notation is handled correctly (type[N] -> type)

        Validation is STRICT: All errors are collected, then raised together.

        Error Collection:
            - Unknown type reference: "Type 'Foo' field 'bar' references unknown type 'Baz'"
        """
        for type_name, atomic_type in self.types.items():
            # Skip builtins (they have no fields to validate)
            if atomic_type.is_builtin:
                continue

            for field_name, field_type in atomic_type.fields:
                # Handle array notation: "uint8[8]" -> "uint8"
                base_type = field_type.split('[')[0]

                if not self.is_atomic(base_type):
                    self._errors.append(
                        f"Type '{type_name}' field '{field_name}' references unknown type '{base_type}'"
                    )

    def get(self, name: str) -> AtomicType:
        """
        Get type by name.

        Args:
            name: Type name (e.g., 'uint8', 'ParameterValue')

        Returns:
            AtomicType instance

        Raises:
            KeyError: If type not found
        """
        return self.types[name]

    def is_atomic(self, name: str) -> bool:
        """
        Check if type exists in registry.

        Args:
            name: Type name to check

        Returns:
            True if type exists, False otherwise
        """
        return name in self.types

    def get_errors(self) -> list[str]:
        """
        Get all collected errors.

        Returns:
            list of error messages (empty if no errors)
        """
        return self._errors

    def has_errors(self) -> bool:
        """
        Check if any errors occurred during loading/validation.

        Returns:
            True if errors exist, False otherwise
        """
        return len(self._errors) > 0

    def clear_errors(self):
        """Clear all collected errors (useful for testing)"""
        self._errors = []


# ============================================================================
# Global Registry Pattern
# ============================================================================
# Single global instance for easy access across modules
# This follows the module-level singleton pattern common in Python

_registry = TypeRegistry()


def load_atomic_types() -> None:
    """
    Load all atomic types (builtin only, no YAML).

    This is the main entry point for loading the type system.
    Custom types are added programmatically via add_custom_type().

    After loading, it automatically populates TypeNames class for IDE autocomplete.

    Raises:
        ValueError: If any errors occurred during loading or validation,
                   with all errors concatenated in the message

    Example:
        >>> load_atomic_types()
        >>> # Now TypeNames is populated and available
        >>> from protocol import TypeNames
        >>> TypeNames.U8  # 'u8'
    """
    try:
        from .field import populate_type_names
    except ImportError:
        from field import populate_type_names  # type: ignore[import-not-found]

    # Clear any previous state (important for testing)
    _registry.clear_errors()

    # Load builtins from Python configuration (no YAML)
    _registry.load_builtins()

    # Validate all references after loading
    _registry.validate_references()

    # Strict validation: fail if ANY errors occurred
    if _registry.has_errors():
        error_msg = "Type loading failed:\n" + "\n".join(_registry.get_errors())
        raise ValueError(error_msg)

    # Populate TypeNames for IDE autocomplete
    populate_type_names(list(_registry.types.keys()))


def get_type_registry() -> TypeRegistry:
    """
    Get the global type registry.

    Returns:
        Global TypeRegistry instance

    Example:
        >>> registry = get_type_registry()
        >>> param_type = registry.get('ParameterValue')
        >>> print(param_type.fields)
        [('paramId', 'uint8'), ('normalizedValue', 'float32')]
    """
    return _registry
