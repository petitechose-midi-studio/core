"""
MessageStructure.java Generator

Generates class that imports all message structs.
This provides a single import point for all protocol messages.
"""

from __future__ import annotations

from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.message import Message


def generate_message_structure_java(messages: list[Message], package: str, output_path: Path) -> str:
    """
    Generate MessageStructure.java.

    Args:
        messages: List of message definitions
        package: Base package name (e.g., "com.midi_studio")
        output_path: Where to write MessageStructure.java

    Returns:
        Generated Java code
    """
    # Generate imports for all message structs
    imports: list[str] = []
    for message in messages:
        # Convert SCREAMING_SNAKE_CASE to PascalCase
        pascal_name: str = ''.join(word.capitalize() for word in message.name.split('_'))
        class_name: str = f"{pascal_name}Message"
        imports.append(f'import {package}.protocol.struct.{class_name};')

    imports_str: str = '\n'.join(imports)

    code = f'''package {package}.protocol;

{imports_str}

/**
 * MessageStructure - Umbrella class for all protocol messages
 *
 * AUTO-GENERATED - DO NOT EDIT
 *
 * This class imports all message struct definitions.
 * Import this class to get access to all message types.
 *
 * Usage:
 *   import static {package}.protocol.MessageStructure.*;
 *
 *   TransportPlayMessage msg = new TransportPlayMessage(true);
 *   msg.encode();
 */
public class MessageStructure {{
    // This class only serves to provide a single import point
    // All message classes are already imported above
    private MessageStructure() {{
        // Utility class - prevent instantiation
    }}
}}
'''

    # Write to file
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(code, encoding='utf-8')

    return code
