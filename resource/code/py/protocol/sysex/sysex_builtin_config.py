"""
Builtin SysEx Configuration (Pure Python, Type-Safe)

Default configuration for SysEx protocol constants.
No YAML - everything defined in Python with strong typing.

This replaces sysex_protocol_config.yaml with pure Python configuration.
"""

from .sysex_config import (
    SysExConfig,
    SysExFraming,
    SysExStructure,
    SysExLimits,
    MessageIDRanges,
    MessageIDRange,
    ProtocolRoles,
)


# Default configuration instance
BUILTIN_SYSEX_CONFIG = SysExConfig(
    framing=SysExFraming(
        start=0xF0,              # MIDI SysEx start byte
        end=0xF7,                # MIDI SysEx end byte
        manufacturer_id=0x7D,    # Educational/development use
        device_id=0x00,          # All devices
    ),
    structure=SysExStructure(
        min_message_length=6,    # start + mfr + dev + type + end = minimum 5, +1 for safety
        message_type_offset=3,   # After start, mfr_id, dev_id
        from_host_offset=4,      # After message_type
        payload_offset=5,        # After from_host flag
    ),
    limits=SysExLimits(
        string_max_length=16,    # Max string length: 16 caractères
        array_max_items=32,      # Max array size: 32 éléments
        max_payload_size=128,    # Max payload bytes
        max_message_size=256,    # Max total message size
    ),
    message_id_ranges=MessageIDRanges(
        controller_to_host=MessageIDRange(start=0x00, end=0x3F),
        host_to_controller=MessageIDRange(start=0x40, end=0xBF),
        bidirectional=MessageIDRange(start=0xC0, end=0xC7),
    ),
    roles=ProtocolRoles(
        cpp='controller',        # C++ acts as controller (hardware)
        java='host',             # Java acts as host (DAW)
    ),
)
