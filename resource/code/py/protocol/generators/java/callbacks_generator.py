"""
ProtocolCallbacks.java Generator

Generates base class with typed callbacks for each message.
Protocol extends this and users assign callbacks.
"""

from __future__ import annotations

from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.message import Message


def generate_protocol_callbacks_java(messages: list[Message], package: str, output_path: Path) -> str:
    """
    Generate ProtocolCallbacks.java.

    Args:
        messages: List of message definitions
        package: Base package name (e.g., "com.midi_studio")
        output_path: Where to write ProtocolCallbacks.java

    Returns:
        Generated Java code
    """
    # Generate callback declarations for each message
    callbacks: list[str] = []
    for message in messages:
        # Convert SCREAMING_SNAKE_CASE to PascalCase
        pascal_name = ''.join(word.capitalize() for word in message.name.split('_'))
        class_name = f"{pascal_name}Message"

        # Callback name: onTransportPlay, onParameterSet, etc.
        callback_name = f"on{pascal_name}"

        callbacks.append(f'    public MessageHandler<{class_name}> {callback_name};')

    callbacks_str = '\n'.join(callbacks)

    code = f'''package {package}.protocol;

import {package}.protocol.struct.*;

/**
 * ProtocolCallbacks - Typed callbacks for all messages
 *
 * AUTO-GENERATED - DO NOT EDIT
 *
 * Base class providing typed callbacks for each message type.
 * Protocol extends this and DecoderRegistry calls these callbacks.
 *
 * Usage:
 *   protocol.onTransportPlay = msg -> {{
 *       System.out.println("Playing: " + msg.isPlaying());
 *   }};
 */
public class ProtocolCallbacks {{

    /**
     * Functional interface for message handlers
     */
    @FunctionalInterface
    public interface MessageHandler<T> {{
        void handle(T message);
    }}

    // ========================================================================
    // Typed callbacks (one per message)
    // ========================================================================

{callbacks_str}

    protected ProtocolCallbacks() {{}}
}}
'''

    # Write to file
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(code, encoding='utf-8')

    return code
