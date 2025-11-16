"""
Type stubs for protocol package

AUTO-GENERATED - Provides type hints for IDE autocomplete
"""

from typing import List

from .field import (
    Type,
    populate_type_names,
    FieldBase,
    PrimitiveField,
    CompositeField,
)
from .message import Message
from .type_loader import AtomicType, TypeRegistry, load_atomic_types, get_type_registry
from .message_id_allocator import allocate_message_ids, load_ranges_from_config
from .validator import ProtocolValidator
from .message_importer import import_sysex_messages

__version__: str
__author__: str

__all__: List[str]

# Re-export for type checking
__all__ = [
    "FieldBase",
    "PrimitiveField",
    "CompositeField",
    "Type",
    "Message",
    "AtomicType",
    "TypeRegistry",
    "populate_type_names",
    "load_atomic_types",
    "get_type_registry",
    "allocate_message_ids",
    "load_ranges_from_config",
    "ProtocolValidator",
    "import_sysex_messages",
    "__version__",
    "__author__",
]
