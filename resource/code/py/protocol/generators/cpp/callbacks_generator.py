"""
ProtocolCallbacks.hpp Generator

Generates base class with typed callbacks for each message.
Protocol inherits this and users assign callbacks.
"""

from __future__ import annotations

from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.message import Message


def generate_protocol_callbacks_hpp(messages: list[Message], output_path: Path) -> str:
    """
    Generate ProtocolCallbacks.hpp.

    Args:
        messages: List of message definitions
        output_path: Where to write ProtocolCallbacks.hpp

    Returns:
        Generated C++ code
    """
    # Generate callback declarations for each message
    callbacks: list[str] = []
    for message in messages:
        # Convert SCREAMING_SNAKE_CASE to PascalCase
        pascal_name = ''.join(word.capitalize() for word in message.name.split('_'))
        class_name = f"{pascal_name}Message"

        # Callback name: onTransportPlay, onParameterSet, etc.
        callback_name = f"on{pascal_name}"

        callbacks.append(f'    std::function<void(const {class_name}&)> {callback_name};')

    callbacks_str = '\n'.join(callbacks)

    code = f'''/**
 * ProtocolCallbacks.hpp - Typed callbacks for all messages
 *
 * AUTO-GENERATED - DO NOT EDIT
 *
 * Base class providing typed callbacks for each message type.
 * Protocol inherits this and DecoderRegistry calls these callbacks.
 *
 * Usage:
 *   protocol.onTransportPlay = [](const TransportPlayMessage& msg) {{
 *       // Handle message
 *   }};
 */

#pragma once

#include "MessageStructure.hpp"
#include <functional>

namespace Protocol {{

/**
 * Base class with typed callbacks for each message
 */
class ProtocolCallbacks {{
public:
    // ========================================================================
    // Typed callbacks (one per message)
    // ========================================================================

{callbacks_str}

protected:
    ProtocolCallbacks() = default;
    virtual ~ProtocolCallbacks() = default;
}};

}}  // namespace Protocol
'''

    # Write to file
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(code, encoding='utf-8')

    return code
