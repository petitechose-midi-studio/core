"""
Field definition for message composition

This module provides the Field class hierarchy for defining fields in message compositions.
Fields can be either primitive (with a type_name) or composite (with nested fields).

Architecture:
- FieldBase: Abstract base class defining the interface
- PrimitiveField: Concrete class for primitive typed fields
- CompositeField: Concrete class for composite fields with nested structure
- Field(): Factory function for backward compatibility

Single Responsibility: Represent a single field with its name, type, and structure.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Optional, Union
from enum import Enum


class Type(str, Enum):
    """
    Enum of available atomic type names.

    This enum is populated dynamically from builtin_types.py
    via populate_type_names(). It provides strict type-safe field type
    references with IDE autocomplete support.

    Values are populated at runtime from builtin_types.py (pure Python).

    Usage:
        >>> Field('paramId', Type.UINT8)
        >>> Field('param', Type.PARAMETERVALUE)

    Note: This enum is populated by __init__.py on module import.
    The enum members are created dynamically from Python configuration.
    """
    pass


def populate_type_names(type_names: list[str]) -> None:
    """
    Populate Type enum with available types.

    This function is called by type_loader.py after loading all types
    from YAML files. It dynamically creates enum members for each type.

    Args:
        type_names: List of type names to register

    Example:
        >>> populate_type_names(['uint8', 'uint16', 'ParameterValue'])
        >>> Type.UINT8.value  # 'uint8'
        >>> Type.PARAMETERVALUE.value  # 'ParameterValue'
    """
    # Clear existing members (for testing/reloading)
    Type._member_map_.clear()
    Type._member_names_ = []
    Type._value2member_map_.clear()

    # Add each type as an enum member
    for type_name in type_names:
        # Convert to SCREAMING_SNAKE_CASE for enum member name
        member_name = type_name.upper().replace('-', '_')
        # Create enum member with original type name as value
        # Use str.__new__ because Type inherits from str
        member = str.__new__(Type, type_name)
        member._name_ = member_name
        member._value_ = type_name
        # Register the member in all internal dicts
        Type._member_map_[member_name] = member
        Type._member_names_.append(member_name)
        Type._value2member_map_[type_name] = member
        # CRITICAL: Set as class dict entry (bypass Enum __setattr__)
        type.__setattr__(Type, member_name, member)


# ============================================================================
# Field Base Class (Abstract)
# ============================================================================

class FieldBase(ABC):
    """
    Abstract base class for all field types.

    Defines the common interface that all fields must implement.
    Not a dataclass itself to avoid field ordering conflicts in subclasses.
    """
    name: str
    array: Optional[int]

    @abstractmethod
    def is_primitive(self) -> bool:
        """Check if this field is a primitive type"""
        ...

    @abstractmethod
    def is_composite(self) -> bool:
        """Check if this field is a composite type (contains nested fields)"""
        ...

    def is_array(self) -> bool:
        """Check if this field is an array"""
        return self.array is not None

    @abstractmethod
    def validate_depth(self, max_depth: int = 3, current_depth: int = 0) -> None:
        """Validate that nested field depth doesn't exceed max_depth"""
        ...

    @abstractmethod
    def __str__(self) -> str:
        """String representation for debugging"""
        ...


# ============================================================================
# Primitive Field (Type-Safe)
# ============================================================================

@dataclass
class PrimitiveField(FieldBase):
    """
    Primitive field with a type reference.

    Represents a field that references a primitive type like UINT8, STRING, etc.
    Type-safe: type_name is always defined (never None).

    Attributes:
        name: Field name
        type_name: Type enum reference (always defined for primitives)
        array: Array size (None = scalar, int > 0 = fixed-size array)
        dynamic: If True, generate etl::vector instead of etl::array (default: False)

    Examples:
        >>> PrimitiveField('paramId', type_name=Type.UINT8)
        >>> PrimitiveField('colors', type_name=Type.UINT8, array=8)
        >>> PrimitiveField('names', type_name=Type.STRING, array=32, dynamic=True)  # etl::vector
    """
    name: str
    type_name: Type
    array: Optional[int] = None
    dynamic: bool = False

    def __post_init__(self) -> None:
        """Validate primitive field"""
        if self.array is not None and self.array <= 0:
            raise ValueError(f"Array size must be positive, got {self.array}")
        if self.dynamic and self.array is None:
            raise ValueError(f"Field '{self.name}': dynamic=True requires array size to be specified")

    def is_primitive(self) -> bool:
        """Primitive fields always return True"""
        return True

    def is_composite(self) -> bool:
        """Primitive fields always return False"""
        return False

    def validate_depth(self, max_depth: int = 3, current_depth: int = 0) -> None:
        """Primitive fields have no depth"""
        if current_depth > max_depth:
            raise ValueError(
                f"Field '{self.name}' exceeds maximum nesting depth of {max_depth}"
            )

    def __str__(self) -> str:
        """String representation"""
        type_str = self.type_name.value
        if self.is_array():
            return f"{self.name}: {type_str}[{self.array}]"
        return f"{self.name}: {type_str}"


# ============================================================================
# Composite Field (Type-Safe)
# ============================================================================

@dataclass
class CompositeField(FieldBase):
    """
    Composite field with nested fields.

    Represents a field that contains nested fields (struct-like composition).
    Type-safe: fields is always defined and non-empty (never None).

    Attributes:
        name: Field name (typically PascalCase for composite types)
        fields: Sequence of nested Field instances (always defined and non-empty)
        array: Array size (None = scalar, int > 0 = fixed-size array)

    Examples:
        >>> CompositeField('Parameter', fields=[
        ...     PrimitiveField('id', type_name=Type.UINT8),
        ...     PrimitiveField('value', type_name=Type.FLOAT32)
        ... ])
    """
    name: str
    fields: Sequence[FieldBase]
    array: Optional[int] = None

    def __post_init__(self) -> None:
        """Validate composite field and convert to list if needed"""
        # Convert to list for internal storage
        object.__setattr__(self, 'fields', list(self.fields))

        if not self.fields:
            raise ValueError(f"CompositeField '{self.name}' must have at least one field")

        if self.array is not None and self.array <= 0:
            raise ValueError(f"Array size must be positive, got {self.array}")

    def is_primitive(self) -> bool:
        """Composite fields always return False"""
        return False

    def is_composite(self) -> bool:
        """Composite fields always return True"""
        return True

    def validate_depth(self, max_depth: int = 3, current_depth: int = 0) -> None:
        """
        Validate that nested field depth doesn't exceed max_depth.

        This prevents overly complex nested structures that are hard to maintain
        and may indicate a design issue.

        Args:
            max_depth: Maximum allowed nesting depth (default: 3)
            current_depth: Current depth in recursion (internal use)

        Raises:
            ValueError: If depth exceeds max_depth
        """
        if current_depth > max_depth:
            raise ValueError(
                f"Field '{self.name}' exceeds maximum nesting depth of {max_depth}. "
                f"Current depth: {current_depth}. "
                f"Deep nesting usually indicates a design issue. "
                f"Consider flattening your data structure."
            )

        # Recursively validate nested fields
        for field in self.fields:
            field.validate_depth(max_depth, current_depth + 1)

    def __str__(self) -> str:
        """String representation"""
        fields_str = f"{len(self.fields)} fields"
        if self.is_array():
            return f"{self.name}: struct({fields_str})[{self.array}]"
        return f"{self.name}: struct({fields_str})"


# ============================================================================
# Type Alias for Union Type
# ============================================================================

# Type alias for functions that accept either PrimitiveField or CompositeField
FieldType = Union[PrimitiveField, CompositeField]


# ============================================================================
# Auto-initialize Type enum with builtin types
# ============================================================================
# This ensures Type enum is always populated when the module is imported,
# enabling immediate usage without calling populate_type_names() manually.

# Type enum is populated by __init__.py on module import
# No auto-population needed here - pure Python configuration
