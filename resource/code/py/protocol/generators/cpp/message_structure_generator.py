"""
MessageStructure.hpp Generator

Generates umbrella header that includes all message structs.
This allows Protocol.hpp to include all messages with a single #include.
"""

from __future__ import annotations

from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.message import Message


def generate_message_structure_hpp(messages: list[Message], output_path: Path) -> str:
    """
    Generate MessageStructure.hpp umbrella header.

    Args:
        messages: List of message definitions
        output_path: Where to write MessageStructure.hpp

    Returns:
        Generated C++ code
    """
    # Generate includes for all message structs
    includes: list[str] = []
    for message in messages:
        # Convert SCREAMING_SNAKE_CASE to PascalCase
        pascal_name = ''.join(word.capitalize() for word in message.name.split('_'))
        struct_name = f"{pascal_name}Message"
        includes.append(f'#include "struct/{struct_name}.hpp"')

    includes_str = '\n'.join(includes)

    code = f'''/**
 * MessageStructure.hpp - Umbrella header for all protocol messages
 *
 * AUTO-GENERATED - DO NOT EDIT
 *
 * This file includes all message struct definitions.
 * Use this single include in your code instead of including individual structs.
 *
 * Usage:
 *   #include "MessageStructure.hpp"
 *
 *   TransportPlayMessage msg{{true}};
 *   msg.encode(buffer, bufferSize);
 */

#pragma once

// Include all message structs
{includes_str}
'''

    # Write to file
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(code, encoding='utf-8')

    return code
