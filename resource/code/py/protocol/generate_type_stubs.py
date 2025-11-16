#!/usr/bin/env python3
"""
Generate field.pyi stub file for Pylance autocomplete using introspection

This script loads types and uses introspection on dataclasses to generate
a .pyi stub file with correct type hints for IDE autocomplete.

Single source of truth: dataclass definitions in field.py and message.py.

Usage:
    Direct invocation:
    python resource/code/py/protocol/generate_type_stubs.py

    Or from the protocol directory:
    python generate_type_stubs.py
"""

import sys
import dataclasses
from pathlib import Path

# Add parent directory to path so we can import protocol as a module
sys.path.insert(0, str(Path(__file__).parent.parent))

from protocol.type_loader import TypeRegistry
from protocol.field import PrimitiveField, CompositeField
from protocol.message import Message


def _format_type_annotation(type_hint: object) -> str:
    """Format type annotation for stub file."""
    type_str = str(type_hint)
    # Clean up type string
    type_str = type_str.replace('typing.', '').replace('<class \'', '').replace('\'>', '')
    type_str = type_str.replace('protocol.field.', '').replace('protocol.message.', '')
    if 'Type' in type_str and 'Optional' not in type_str and 'Sequence' not in type_str:
        return 'Type'
    return type_str


def _format_default(default_val: object) -> str:
    """Format default value for stub file."""
    if isinstance(default_val, str):
        return f"'{default_val}'"
    elif isinstance(default_val, bool):
        return str(default_val)
    elif default_val is None:
        return 'None'
    else:
        return repr(default_val)


def _generate_dataclass_stub(cls: type, class_name: str) -> str:
    """Generate stub definition from actual dataclass using introspection."""
    if not dataclasses.is_dataclass(cls):
        raise ValueError(f"{class_name} is not a dataclass")

    # Get class docstring
    doc = cls.__doc__ or f"{class_name} class"
    doc_lines = [line.strip() for line in doc.strip().split('\n')]

    # Start stub definition
    stub = f'@dataclass\nclass {class_name}'

    # Add base classes if any
    if hasattr(cls, '__bases__') and cls.__bases__ and cls.__bases__[0] != object:
        bases = ', '.join(base.__name__ for base in cls.__bases__ if base != object)
        if bases:
            stub += f'({bases})'

    stub += ':\n'

    # Add docstring
    if len(doc_lines) == 1:
        stub += f'    """{doc_lines[0]}"""\n'
    else:
        stub += '    """\n'
        for line in doc_lines:
            stub += f'    {line}\n'
        stub += '    """\n'

    # Add field definitions from actual dataclass fields
    for field in dataclasses.fields(cls):
        type_str = _format_type_annotation(field.type)
        if field.default != dataclasses.MISSING:
            default_str = _format_default(field.default)
            stub += f'    {field.name}: {type_str} = {default_str}\n'
        elif field.default_factory != dataclasses.MISSING:
            stub += f'    {field.name}: {type_str}\n'
        else:
            stub += f'    {field.name}: {type_str}\n'

    # Add method stubs for methods defined directly on this class (not inherited)
    # This includes abstract method implementations from FieldBase
    import inspect
    for name, method in inspect.getmembers(cls, predicate=inspect.isfunction):
        # Skip private methods except __str__ and __post_init__
        if name.startswith('_') and name not in ('__str__', '__post_init__'):
            continue
        # Only include methods defined on this class (not inherited from base)
        if name in cls.__dict__:
            # Get method signature
            sig = inspect.signature(method)
            # Format parameters with type hints
            params: list[str] = []
            for param_name, param in sig.parameters.items():
                if param_name == 'self':
                    continue
                if param.annotation != inspect.Parameter.empty:
                    type_hint = _format_type_annotation(param.annotation)
                    if param.default != inspect.Parameter.empty:
                        default = _format_default(param.default)
                        params.append(f'{param_name}: {type_hint} = {default}')
                    else:
                        params.append(f'{param_name}: {type_hint}')
                else:
                    # No annotation - use object as fallback
                    if param.default != inspect.Parameter.empty:
                        default = _format_default(param.default)
                        params.append(f'{param_name}: object = {default}')
                    else:
                        params.append(f'{param_name}: object')

            params_str = ', '.join(params)

            # Format return type
            if sig.return_annotation != inspect.Signature.empty:
                return_type = _format_type_annotation(sig.return_annotation)
            else:
                return_type = '...'

            stub += f'    def {name}(self{", " + params_str if params_str else ""}) -> {return_type}: ...\n'

    return stub


def generate_stub_file() -> None:
    """
    Generate field.pyi stub file using introspection.

    Loads types and introspects dataclasses to generate correct stubs.
    Single source of truth: the actual dataclass definitions.
    """
    # Resolve paths
    script_dir = Path(__file__).parent

    print(f"Loading builtin types from builtin_types.py...")
    registry = TypeRegistry()
    registry.load_builtins()  # No arguments - loads from Python config

    builtin_types = [name for name, t in registry.types.items() if t.is_builtin]

    print(f"  Found {len(builtin_types)} builtin types")

    # Generate stub file content
    stub_content = f'''"""
Type stubs for field.py

This file provides type hints for Pylance/Pyright to enable autocomplete
for dynamically populated Type enum members and field classes.

AUTO-GENERATED by generate_type_stubs.py - DO NOT EDIT MANUALLY
Run: python resource/code/py/protocol/generate_type_stubs.py
"""

from abc import ABC, abstractmethod
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Optional
from enum import Enum


class Type(str, Enum):
    """
    Type-safe enum of builtin type names.

    Enum members are populated dynamically from builtin_types.py,
    but declared here for IDE autocomplete.
    """
    # Builtin types (from builtin_types.py)
'''

    # Add builtin type enum members
    for type_name in sorted(builtin_types):
        member_name = type_name.upper().replace('-', '_')
        stub_content += f'    {member_name} = "{type_name}"\n'

    stub_content += '''

    # Custom types (from field compositions)


def populate_type_names(type_names: list[str]) -> None:
    """Populate Type enum with available types."""
    ...


'''

    # Generate FieldBase stub (abstract class - manually defined since it's not a dataclass)
    stub_content += '''class FieldBase(ABC):
    """Abstract base class for all field types."""
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
        ...

    @abstractmethod
    def validate_depth(self, max_depth: int = 3, current_depth: int = 0) -> None:
        """Validate that nested field depth doesn't exceed max_depth"""
        ...

    @abstractmethod
    def __str__(self) -> str:
        """String representation for debugging"""
        ...


'''

    # Generate PrimitiveField stub from actual class using introspection
    stub_content += _generate_dataclass_stub(PrimitiveField, 'PrimitiveField')
    stub_content += '\n\n'

    # Generate CompositeField stub from actual class using introspection
    stub_content += _generate_dataclass_stub(CompositeField, 'CompositeField')
    stub_content += '\n\n'

    # Generate Message stub from actual class using introspection
    stub_content += _generate_dataclass_stub(Message, 'Message')
    stub_content += '\n'

    # Write stub file
    stub_path = script_dir / "field.pyi"
    stub_path.write_text(stub_content, encoding='utf-8')

    print(f"\n[OK] Generated {stub_path}")
    print(f"     Total: {len(builtin_types)} builtin types")
    print(f"     Classes: PrimitiveField, CompositeField, Message (via introspection)")


def main():
    """Main entry point"""
    generate_stub_file()


if __name__ == "__main__":
    main()
