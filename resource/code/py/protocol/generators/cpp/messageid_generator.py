"""
MessageID.hpp Generator (C++)

Generates enum class MessageID with auto-allocated message IDs.
Simplified version - all messages are sequential, no flow-based grouping.
"""

from __future__ import annotations

from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.message import Message
    from protocol.type_loader import TypeRegistry


def generate_messageid_hpp(
    messages: list[Message],
    allocations: dict[str, int],
    type_registry: TypeRegistry,
    output_path: Path
) -> str:
    """
    Generate MessageID.hpp with sequential message IDs.

    Args:
        messages: List of Message objects
        allocations: Dict mapping message_name → message_id
        type_registry: TypeRegistry for type lookups
        output_path: Output file path (for reference only)

    Returns:
        Generated C++ code as string
    """
    uint8_cpp = type_registry.get('uint8').cpp_type

    if uint8_cpp is None:
        raise ValueError("Missing C++ type mapping for uint8 in builtin_types.yaml")

    # Sort messages by allocated ID
    sorted_messages: list[Message] = sorted(messages, key=lambda m: allocations[m.name])

    header = _generate_header(len(messages), uint8_cpp)
    enum_body = _generate_enum_body(sorted_messages, allocations)
    counts = _generate_counts(len(messages), uint8_cpp)
    footer = _generate_footer()

    full_code = f"{header}\n{enum_body}\n{counts}\n{footer}"
    return full_code


def _generate_header(message_count: int, uint8_type: str) -> str:
    """Generate file header."""
    return f'''/**
 * MessageID.hpp - SysEx Message ID Enum
 *
 * AUTO-GENERATED - DO NOT EDIT
 * Generated from: sysex_messages.py + message_id_allocator
 *
 * This file defines the MessageID enum containing all valid SysEx message
 * identifiers. IDs are auto-allocated sequentially starting from 0x00.
 *
 * Total messages: {message_count}
 */

#pragma once

#include <cstdint>

namespace Protocol {{

/**
 * SysEx Message Identifiers
 *
 * Type-safe enum for all protocol messages.
 * Values are auto-allocated sequentially.
 */
enum class MessageID : {uint8_type} {{
'''


def _generate_enum_body(sorted_messages: list[Message], allocations: dict[str, int]) -> str:
    """Generate enum constants sorted by ID."""
    lines: list[str] = []
    lines.append("    // ========================================")
    lines.append("    // Protocol Messages")
    lines.append("    // ========================================")
    lines.append("")

    for msg in sorted_messages:
        msg_id: int = allocations[msg.name]
        enum_name: str = msg.name  # Already in SCREAMING_SNAKE_CASE
        value_str: str = f"0x{msg_id:02X}"
        comment: str = f"  // {msg.description}"
        lines.append(f"    {enum_name} = {value_str},{comment}")

    return "\n".join(lines)


def _generate_counts(message_count: int, uint8_type: str) -> str:
    """Generate message count constants."""
    return f'''
}};

/**
 * Total number of defined messages
 */
constexpr {uint8_type} MESSAGE_COUNT = {message_count};
'''


def _generate_footer() -> str:
    """Generate file footer."""
    return '''
}  // namespace Protocol
'''
