"""
MessageID.java Generator

Generates Java MessageID enum with auto-allocated message IDs.
Simplified version - all messages are sequential, no flow-based grouping.
"""

from __future__ import annotations
from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.message import Message
    from protocol.type_loader import TypeRegistry


def generate_messageid_java(
    messages: list[Message],
    allocations: dict[str, int],
    type_registry: TypeRegistry,
    output_path: Path
) -> str:
    """
    Generate MessageID.java with sequential message IDs.

    Args:
        messages: List of Message objects
        allocations: Dict mapping message_name → message_id
        type_registry: TypeRegistry for type lookups
        output_path: Output file path (for package detection)

    Returns:
        Generated Java code as string
    """
    # Sort messages by allocated ID
    sorted_messages: list[Message] = sorted(messages, key=lambda m: allocations[m.name])

    header = _generate_header(len(messages))
    enum_body = _generate_enum_body(sorted_messages, allocations)
    methods = _generate_methods()
    footer = _generate_footer()

    full_code = f"{header}\n{enum_body}\n{methods}\n{footer}"
    return full_code


def _generate_header(message_count: int) -> str:
    """Generate file header."""
    return f'''package com.midi_studio.protocol;

/**
 * MessageID - SysEx Message ID Enum
 *
 * AUTO-GENERATED - DO NOT EDIT
 * Generated from: sysex_messages.py + message_id_allocator
 *
 * This enum defines all valid SysEx message identifiers.
 * IDs are auto-allocated sequentially starting from 0x00.
 *
 * Total messages: {message_count}
 */
public enum MessageID {{
'''


def _generate_enum_body(sorted_messages: list[Message], allocations: dict[str, int]) -> str:
    """Generate enum constants sorted by ID."""
    lines: list[str] = []
    lines.append("    // ========================================")
    lines.append("    // Protocol Messages")
    lines.append("    // ========================================")
    lines.append("")

    for i, msg in enumerate(sorted_messages):
        msg_id = allocations[msg.name]
        enum_name = msg.name  # Already in SCREAMING_SNAKE_CASE
        value_str = f"0x{msg_id:02X}"
        comment = f"  // {msg.description}"

        # Last message gets semicolon
        trailing = ";" if i == len(sorted_messages) - 1 else ","
        lines.append(f"    {enum_name}({value_str}){trailing}{comment}")

    return "\n".join(lines)


def _generate_methods() -> str:
    """Generate enum methods."""
    return '''

    private final byte value;

    MessageID(int value) {
        this.value = (byte) value;
    }

    public byte getValue() {
        return value;
    }

    /**
     * Get MessageID from byte value
     * @param value The byte value
     * @return MessageID enum constant, or null if not found
     */
    public static MessageID fromValue(byte value) {
        for (MessageID id : values()) {
            if (id.value == value) {
                return id;
            }
        }
        return null;
    }

    /**
     * Total number of defined messages
     */
    public static int getMessageCount() {
        return values().length;
    }
'''


def _generate_footer() -> str:
    """Generate file footer."""
    return '''}
'''
