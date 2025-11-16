"""
DecoderRegistry.hpp Generator

Generates registry that maps MessageID to decode+dispatch logic.
This allows Protocol to dispatch messages without manual switch statements.

Supports optimistic update reconciliation for messages marked with optimistic=True.
"""

from __future__ import annotations

from typing import TYPE_CHECKING
from pathlib import Path

if TYPE_CHECKING:
    from protocol.message import Message


def generate_decoder_registry_hpp(messages: list[Message], output_path: Path) -> str:
    """
    Generate DecoderRegistry.hpp.

    Args:
        messages: List of message definitions
        output_path: Where to write DecoderRegistry.hpp

    Returns:
        Generated C++ code
    """
    # Check if any message uses optimistic updates
    has_optimistic = any(getattr(message, 'optimistic', False) for message in messages)

    # Generate case statements for each message
    cases: list[str] = []
    for message in messages:
        # Convert SCREAMING_SNAKE_CASE to PascalCase
        pascal_name = ''.join(word.capitalize() for word in message.name.split('_'))
        class_name = f"{pascal_name}Message"
        callback_name = f"on{pascal_name}"

        # Check if this message uses optimistic updates
        is_optimistic = getattr(message, 'optimistic', False)

        if is_optimistic:
            # Generate optimistic reconciliation logic
            # ASSUMES: message has parameterIndex, parameterValue, sequenceNumber fields
            # This is currently hardcoded for DeviceMacroValueChange pattern
            # TODO: Make this more generic if other messages need optimistic updates
            cases.append(f'''        case MessageID::{message.name}:
            if (callbacks.{callback_name}) {{
                auto decoded = {class_name}::decode(payload, payloadLen);
                if (decoded.has_value()) {{
                    auto msg = decoded.value();
                    msg.fromHost = fromHost;  // Inject origin flag

                    // Optimistic reconciliation (only for messages from host)
                    if (fromHost && msg.sequenceNumber != 0) {{
                        // Check if this is an echo of an optimistic update
                        bool shouldSkip = OptimisticTracker::shouldSkipCallback(
                            msg.parameterIndex,
                            msg.sequenceNumber,
                            msg.parameterValue
                        );
                        if (shouldSkip) {{
                            // Already applied optimistically - skip callback
                            break;
                        }}
                        // Value differs or not tracked - call callback with adjusted value
                    }}

                    callbacks.{callback_name}(msg);
                }}
            }}
            break;''')
        else:
            # Standard passthrough (no optimistic logic)
            cases.append(f'''        case MessageID::{message.name}:
            if (callbacks.{callback_name}) {{
                auto decoded = {class_name}::decode(payload, payloadLen);
                if (decoded.has_value()) {{
                    auto msg = decoded.value();
                    msg.fromHost = fromHost;  // Inject origin flag
                    callbacks.{callback_name}(msg);
                }}
            }}
            break;''')

    cases_str = '\n'.join(cases)

    # Conditionally include OptimisticTracker header
    optimistic_include = ''
    if has_optimistic:
        optimistic_include = '#include "OptimisticTracker.hpp"'

    code = f'''/**
 * DecoderRegistry.hpp - MessageID to Decoder mapping
 *
 * AUTO-GENERATED - DO NOT EDIT
 *
 * Dispatches incoming messages to typed callbacks.
 * Called by Protocol.dispatch().
 *
 * Supports optimistic update reconciliation for messages with optimistic=True.
 */

#pragma once

#include "MessageID.hpp"
#include "MessageStructure.hpp"
#include "ProtocolCallbacks.hpp"
{optimistic_include}
#include <cstdint>

namespace Protocol {{

class DecoderRegistry {{
public:
    /**
     * Decode message and invoke appropriate callback
     *
     * @param callbacks Object with typed callbacks (ProtocolCallbacks)
     * @param messageId MessageID to decode
     * @param payload Raw payload bytes
     * @param payloadLen Payload length
     * @param fromHost Origin flag (true if message from host, false if from controller)
     */
    static void dispatch(
        ProtocolCallbacks& callbacks,
        MessageID messageId,
        const uint8_t* payload,
        uint16_t payloadLen,
        bool fromHost
    ) {{
        switch (messageId) {{
{cases_str}
            default:
                // Unknown message type - silently ignore
                break;
        }}
    }}
}};

}}  // namespace Protocol
'''

    # Write to file
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(code, encoding='utf-8')

    return code
