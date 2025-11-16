"""
DecoderRegistry.java Generator

Generates registry that maps MessageID to decode+dispatch logic.
This allows Protocol to dispatch messages without manual switch statements.
"""

from __future__ import annotations

from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.message import Message


def generate_decoder_registry_java(messages: list[Message], package: str, output_path: Path) -> str:
    """
    Generate DecoderRegistry.java.

    Args:
        messages: List of message definitions
        package: Base package name (e.g., "com.midi_studio")
        output_path: Where to write DecoderRegistry.java

    Returns:
        Generated Java code
    """
    # Generate case statements for each message
    cases: list[str] = []
    for message in messages:
        # Convert SCREAMING_SNAKE_CASE to PascalCase
        pascal_name = ''.join(word.capitalize() for word in message.name.split('_'))
        class_name = f"{pascal_name}Message"
        callback_name = f"on{pascal_name}"

        cases.append(f'''            case {message.name}:
                if (callbacks.{callback_name} != null) {{
                    {class_name} msg = {class_name}.decode(payload);
                    msg.fromHost = fromHost;  // Inject origin flag
                    callbacks.{callback_name}.handle(msg);
                }}
                break;''')

    cases_str = '\n'.join(cases)

    code = f'''package {package}.protocol;

import {package}.protocol.MessageID;
import {package}.protocol.ProtocolCallbacks;
import {package}.protocol.struct.*;

/**
 * DecoderRegistry - MessageID to Decoder mapping
 *
 * AUTO-GENERATED - DO NOT EDIT
 *
 * Dispatches incoming messages to typed callbacks.
 * Called by Protocol.dispatch().
 */
public class DecoderRegistry {{

    /**
     * Decode message and invoke appropriate callback
     *
     * @param callbacks Object with typed callbacks (ProtocolCallbacks)
     * @param messageId MessageID to decode
     * @param payload Raw payload bytes
     * @param fromHost Origin flag (true if message from host, false if from controller)
     */
    public static void dispatch(
        ProtocolCallbacks callbacks,
        MessageID messageId,
        byte[] payload,
        boolean fromHost
    ) {{
        switch (messageId) {{
{cases_str}
            default:
                // Unknown message type - silently ignore
                break;
        }}
    }}

    // Utility class - prevent instantiation
    private DecoderRegistry() {{}}
}}
'''

    # Write to file
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(code, encoding='utf-8')

    return code
