"""
Java Constants Generator
Generates ProtocolConstants.java from protocol_config.yaml.

This generator extracts SysEx framing constants and protocol limits
from the YAML configuration and generates Java constants.

Key Features:
- public static final for constants
- SysEx framing (manufacturer_id, device_id, etc.)
- Message structure offsets
- Encoding limits (max string length, max array items, etc.)

Generated Output:
- ProtocolConstants.java (~50-100 lines)
- Package: com.midi_studio.protocol
- All constants are public static final
"""

from __future__ import annotations

from typing import TypedDict
from pathlib import Path


class SysExConfig(TypedDict, total=False):
    """SysEx framing configuration"""
    start: int
    end: int
    manufacturer_id: int
    device_id: int
    min_message_length: int
    message_type_offset: int
    from_host_offset: int
    payload_offset: int


class LimitsConfig(TypedDict, total=False):
    """Protocol encoding limits"""
    string_max_length: int
    array_max_items: int
    max_payload_size: int
    max_message_size: int


class MessageIDRange(TypedDict, total=False):
    """Message ID range configuration"""
    start: int
    end: int


class MessageIDRangesConfig(TypedDict, total=False):
    """Message ID ranges for different message directions"""
    controller_to_host: MessageIDRange
    host_to_controller: MessageIDRange
    bidirectional: MessageIDRange


class RolesConfig(TypedDict, total=False):
    """Role configuration for each platform"""
    cpp: str
    java: str
    python: str


class ProtocolConfig(TypedDict, total=False):
    """Protocol configuration structure from protocol_config.yaml"""
    sysex: SysExConfig
    limits: LimitsConfig
    roles: RolesConfig
    message_id_ranges: MessageIDRangesConfig
    message_id_start: int


def generate_constants_java(protocol_config: ProtocolConfig, output_path: Path) -> str:
    """
    Generate ProtocolConstants.java from protocol_config.yaml.

    Args:
        protocol_config: Dict loaded from protocol_config.yaml
        output_path: Path where ProtocolConstants.java will be written

    Returns:
        Generated Java code as string

    Example:
        >>> with open('protocol_config.yaml') as f:
        ...     config = yaml.safe_load(f)
        >>> code = generate_constants_java(config, Path('ProtocolConstants.java'))
    """
    header = _generate_header()
    sysex_constants = _generate_sysex_constants(protocol_config.get('sysex', {}))
    limits = _generate_limits(protocol_config.get('limits', {}))
    ranges = _generate_ranges(protocol_config.get('message_id_ranges', {}))
    role_constants = _generate_role_constants(protocol_config.get('roles', {}))
    footer = _generate_footer()

    full_code = f"{header}\n{sysex_constants}\n{limits}\n{ranges}\n{role_constants}\n{footer}"
    return full_code


def _generate_header() -> str:
    """Generate file header with package and class declaration."""
    return '''package com.midi_studio.protocol;

/**
 * ProtocolConstants - Protocol Configuration Constants
 *
 * AUTO-GENERATED - DO NOT EDIT
 * Generated from: protocol_config.yaml
 *
 * This class contains all protocol constants including SysEx framing,
 * message structure offsets, and encoding limits.
 *
 * All constants are public static final (compile-time constants).
 */
public final class ProtocolConstants {

    // Private constructor prevents instantiation (utility class)
    private ProtocolConstants() {
        throw new AssertionError("Utility class cannot be instantiated");
    }

    // ============================================================================
    // SYSEX FRAMING CONSTANTS
    // ============================================================================
'''


def _generate_sysex_constants(sysex_config: SysExConfig) -> str:
    """Generate SysEx framing constants."""
    if not sysex_config:
        return "    // No SysEx config found\n"

    lines: list[str] = []

    # Message delimiters
    start: int = sysex_config.get('start', 0xF0)
    end: int = sysex_config.get('end', 0xF7)

    # Cast to byte for values > 127
    start_str: str = f"(byte) {start:#04x}" if start >= 0x80 else f"{start:#04x}"
    end_str: str = f"(byte) {end:#04x}" if end >= 0x80 else f"{end:#04x}"

    lines.append(f"    /** SysEx start byte */")
    lines.append(f"    public static final byte SYSEX_START = {start_str};")
    lines.append("")
    lines.append(f"    /** SysEx end byte */")
    lines.append(f"    public static final byte SYSEX_END = {end_str};")
    lines.append("")

    # Protocol identifiers
    manufacturer_id: int = sysex_config.get('manufacturer_id', 0x7F)
    device_id: int = sysex_config.get('device_id', 0x01)

    lines.append(f"    /** MIDI manufacturer ID */")
    lines.append(f"    public static final byte MANUFACTURER_ID = {manufacturer_id:#04x};")
    lines.append("")
    lines.append(f"    /** Device identifier */")
    lines.append(f"    public static final byte DEVICE_ID = {device_id:#04x};")
    lines.append("")

    # Message structure
    min_length: int = sysex_config.get('min_message_length', 6)
    type_offset: int = sysex_config.get('message_type_offset', 3)
    from_host_offset: int = sysex_config.get('from_host_offset', 4)
    payload_offset: int = sysex_config.get('payload_offset', 5)

    lines.append(f"    /** Minimum valid SysEx message length */")
    lines.append(f"    public static final int MIN_MESSAGE_LENGTH = {min_length};")
    lines.append("")
    lines.append(f"    /** Position of MessageID byte in SysEx message */")
    lines.append(f"    public static final int MESSAGE_TYPE_OFFSET = {type_offset};")
    lines.append("")
    lines.append(f"    /** Position of fromHost flag in SysEx message */")
    lines.append(f"    public static final int FROM_HOST_OFFSET = {from_host_offset};")
    lines.append("")
    lines.append(f"    /** Start of payload data in SysEx message */")
    lines.append(f"    public static final int PAYLOAD_OFFSET = {payload_offset};")

    return "\n".join(lines)


def _generate_limits(limits_config: LimitsConfig) -> str:
    """Generate encoding limits constants."""
    if not limits_config:
        return ""

    lines: list[str] = [
        "",
        "    // ============================================================================",
        "    // ENCODING LIMITS",
        "    // ============================================================================",
        ""
    ]

    # String limits
    string_max: int = limits_config.get('string_max_length', 16)
    lines.append(f"    /** Maximum characters per string field */")
    lines.append(f"    public static final int STRING_MAX_LENGTH = {string_max};")
    lines.append("")

    # Array limits
    array_max: int = limits_config.get('array_max_items', 8)
    lines.append(f"    /** Maximum items per array field */")
    lines.append(f"    public static final int ARRAY_MAX_ITEMS = {array_max};")
    lines.append("")

    # Payload limits
    max_payload: int = limits_config.get('max_payload_size', 256)
    max_message: int = limits_config.get('max_message_size', 261)
    lines.append(f"    /** Maximum payload bytes */")
    lines.append(f"    public static final int MAX_PAYLOAD_SIZE = {max_payload};")
    lines.append("")
    lines.append(f"    /** Maximum total message bytes */")
    lines.append(f"    public static final int MAX_MESSAGE_SIZE = {max_message};")

    return "\n".join(lines)


def _generate_ranges(ranges_config: MessageIDRangesConfig) -> str:
    """Generate message ID ranges constants."""
    if not ranges_config:
        return ""

    lines: list[str] = [
        "",
        "    // ============================================================================",
        "    // MESSAGE ID RANGES",
        "    // ============================================================================",
        ""
    ]

    # Controller → Host
    c2h: MessageIDRange = ranges_config.get('controller_to_host', {})
    c2h_start: int = c2h.get('start', 0x00)
    c2h_end: int = c2h.get('end', 0x3F)

    lines.append(f"    /** Controller → Host range start */")
    lines.append(f"    public static final byte ID_RANGE_CONTROLLER_TO_HOST_START = {c2h_start:#04x};")
    lines.append("")
    lines.append(f"    /** Controller → Host range end */")
    lines.append(f"    public static final byte ID_RANGE_CONTROLLER_TO_HOST_END = {c2h_end:#04x};")
    lines.append("")

    # Host → Controller
    h2c: MessageIDRange = ranges_config.get('host_to_controller', {})
    h2c_start: int = h2c.get('start', 0x40)
    h2c_end: int = h2c.get('end', 0xBF)

    h2c_start_str: str = f"(byte) {h2c_start:#04x}" if h2c_start >= 0x80 else f"{h2c_start:#04x}"
    h2c_end_str: str = f"(byte) {h2c_end:#04x}" if h2c_end >= 0x80 else f"{h2c_end:#04x}"

    lines.append(f"    /** Host → Controller range start */")
    lines.append(f"    public static final byte ID_RANGE_HOST_TO_CONTROLLER_START = {h2c_start_str};")
    lines.append("")
    lines.append(f"    /** Host → Controller range end */")
    lines.append(f"    public static final byte ID_RANGE_HOST_TO_CONTROLLER_END = {h2c_end_str};")
    lines.append("")

    # Bidirectional
    bidi: MessageIDRange = ranges_config.get('bidirectional', {})
    bidi_start: int = bidi.get('start', 0xC0)
    bidi_end: int = bidi.get('end', 0xC7)

    bidi_start_str: str = f"(byte) {bidi_start:#04x}" if bidi_start >= 0x80 else f"{bidi_start:#04x}"
    bidi_end_str: str = f"(byte) {bidi_end:#04x}" if bidi_end >= 0x80 else f"{bidi_end:#04x}"

    lines.append(f"    /** Bidirectional range start */")
    lines.append(f"    public static final byte ID_RANGE_BIDIRECTIONAL_START = {bidi_start_str};")
    lines.append("")
    lines.append(f"    /** Bidirectional range end */")
    lines.append(f"    public static final byte ID_RANGE_BIDIRECTIONAL_END = {bidi_end_str};")

    return "\n".join(lines)


def _generate_role_constants(roles_config: RolesConfig) -> str:
    """Generate role constants (IS_HOST flag for protocol direction)."""
    if not roles_config:
        # Default: Java is host
        is_host: bool = True
    else:
        java_role: str = roles_config.get('java', 'host')
        is_host: bool = (java_role == 'host')

    lines: list[str] = [
        "",
        "    // ============================================================================",
        "    // ROLE CONFIGURATION",
        "    // ============================================================================",
        "",
        f"    /** This code's role in the protocol */",
        f"    public static final boolean IS_HOST = {str(is_host).lower()};"
    ]

    return "\n".join(lines)


def _generate_footer() -> str:
    """Generate class closing."""
    return '''

}  // class ProtocolConstants
'''
