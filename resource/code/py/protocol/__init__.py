"""
Protocol Generation Tools (Python-based, type-safe)

Generic, reusable tools for SysEx protocol generation across all plugins.
This package provides the core building blocks for defining and generating
type-safe SysEx protocols.

Architecture:
- field.py: PrimitiveField and CompositeField classes for message composition
- message.py: Message class for message definitions
- type_loader.py: TypeRegistry for loading and validating types
- builtin_types.py: Builtin type definitions (pure Python, no YAML)

Usage:
    from protocol import PrimitiveField, CompositeField, Message, Type, load_atomic_types

    # Load types first (populates Type enum with builtins)
    load_atomic_types()

    # Define a message with type-safe fields
    msg = Message(
        description='Set parameter value',
        fields=[
            PrimitiveField('paramId', Type.UINT8),
            PrimitiveField('value', Type.FLOAT32)
        ]
    )

    # Access type registry
    registry = get_type_registry()
"""

__version__ = "2.0.0"  # Major version bump: No more YAML, pure Python
__author__ = "MIDI Studio Team"

from .field import (
    Type,
    populate_type_names,
    FieldBase,
    PrimitiveField,
    CompositeField,
)
from .message import Message
from .type_loader import (
    AtomicType,
    TypeRegistry,
    load_atomic_types,
    get_type_registry,
)
from .builtin_types import BUILTIN_TYPES
from .message_id_allocator import allocate_message_ids, load_ranges_from_config
from .validator import ProtocolValidator
from .message_importer import import_sysex_messages

# Auto-populate Type enum with builtin types at module import time
# This ensures Type.UINT8, Type.STRING, etc. are available immediately
populate_type_names(list(BUILTIN_TYPES.keys()))

__all__ = [
    # Core classes
    'FieldBase',
    'PrimitiveField',
    'CompositeField',
    'Message',
    'AtomicType',
    'TypeRegistry',
    # Type-safe enums and helpers
    'Type',
    'populate_type_names',
    # Type loading functions
    'load_atomic_types',
    'get_type_registry',
    # Utilities
    'allocate_message_ids',
    'load_ranges_from_config',
    'ProtocolValidator',
    'import_sysex_messages',
    # Metadata
    '__version__',
    '__author__',
]
