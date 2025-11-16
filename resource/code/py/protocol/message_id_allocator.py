"""
Message ID Allocator
Auto-assigns SysEx IDs sequentially.

This module allocates unique SysEx message IDs sequentially from 0x00 onwards.
IDs are allocated in alphabetical order by message name for deterministic
allocation across runs.

Allocation Strategy:
- All messages: 0x00-0xFF (256 slots available)
"""

from __future__ import annotations

from typing import Dict, List, Any
from .message import Message


def allocate_message_ids(
    messages: List[Message],
    start_id: int = 0x00
) -> Dict[str, int]:
    """
    Auto-allocate SysEx IDs for messages sequentially.

    IDs are allocated sequentially starting from start_id, with messages sorted
    alphabetically by name for deterministic allocation.

    Args:
        messages: List of Message instances to allocate IDs for
        start_id: Starting ID (default 0x00)

    Returns:
        Dict mapping message_name → sysex_id (int)
        Example: {'TRANSPORT_PLAY': 0x00, 'TRANSPORT_RECORD': 0x01}

    Raises:
        ValueError: If too many messages (> 256)

    Example:
        >>> msgs = [
        ...     Message(description='Transport play', fields=[...]),
        ...     Message(description='Device info', fields=[...])
        ... ]
        >>> allocations = allocate_message_ids(msgs)
        >>> allocations
        {'TRANSPORT_PLAY': 0, 'TRANSPORT_RECORD': 1}
    """
    if len(messages) > 256:
        raise ValueError(f"Too many messages: {len(messages)} (max 256)")

    allocations: Dict[str, int] = {}

    # Sort by name for deterministic allocation
    sorted_messages = sorted(messages, key=lambda m: m.name or "")

    # Allocate sequential IDs
    for i, msg in enumerate(sorted_messages):
        allocations[msg.name] = start_id + i

    return allocations


def load_ranges_from_config(protocol_config: Dict[str, Any]) -> int:
    """
    Load starting message ID from protocol_config.yaml.

    Args:
        protocol_config: Dict loaded from protocol_config.yaml

    Returns:
        Starting message ID (defaults to 0x00 if not found)
    """
    result: int = protocol_config.get('message_id_start', 0x00)
    return result
