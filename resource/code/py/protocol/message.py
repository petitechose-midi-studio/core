"""
Generic Message class for SysEx protocol system

This module provides the Message class for defining SysEx messages across
all plugins. It is a generic, reusable component that can be used by any
plugin (Bitwig, Ableton, FL Studio, etc.).

The Message class represents a unit of communication with:
- Direction semantics (sent_by/listened_by)
- Field composition (using Field class)
- Validation rules

Single Responsibility: Represent a SysEx message definition.
Reusability: Used across all plugins, not specific to any one.
"""

from __future__ import annotations
from dataclasses import dataclass
from collections.abc import Sequence
from .field import FieldBase


@dataclass
class Message:
    """
    Pure data class for SysEx message definitions (no side effects).

    A message represents a unit of communication between the controller
    and host. All messages are bidirectional - the direction is determined
    by usage context (which class sends/receives the message).

    This class is generic and reusable across all plugins. Plugin-specific
    message definitions are created by instantiating this class in the
    plugin's message/*.py files.

    The message name is automatically derived from the variable name by
    the auto-discovery system in message/__init__.py.

    Attributes:
        description: Human-readable description
        fields: List of Field objects defining the message structure
        name: Message name (auto-injected by message/__init__.py, always set before use)
        optimistic: Enable optimistic updates for this message (default: False)

    Example:
        >>> from protocol import Message
        >>> from field.transport import transport_play
        >>>
        >>> TRANSPORT_PLAY = Message(
        ...     description='Transport play/pause state',
        ...     fields=[transport_play]
        ... )
        >>>
        >>> # Name is auto-injected by message/__init__.py
        >>> TRANSPORT_PLAY.name  # 'TRANSPORT_PLAY'
    """

    description: str                # Human-readable description
    fields: Sequence[FieldBase]     # Field definitions (can be PrimitiveField or CompositeField)
    optimistic: bool = False        # Enable optimistic updates (default: False)

    # Name is injected by auto-discovery (message/__init__.py)
    # Always set before messages are used, so we type it as str (not Optional[str])
    name: str = ''  # Default empty, but always overwritten by auto-discovery

    def __str__(self) -> str:
        """String representation for debugging and display"""
        name_str = self.name or "UNNAMED"
        return (
            f"Message({name_str}, "
            f"{len(self.fields)} fields)"
        )
