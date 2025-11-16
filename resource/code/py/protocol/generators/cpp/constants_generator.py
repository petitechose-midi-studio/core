"""
C++ Constants Generator
Generates ProtocolConstants.hpp from protocol_config.yaml.

This generator extracts SysEx framing constants and protocol limits
from the YAML configuration and generates C++ constants.

Key Features:
- Constexpr for compile-time constants
- SysEx framing (manufacturer_id, device_id, etc.)
- Message structure offsets
- Encoding limits (max string length, max array items, etc.)
- Uses builtin_types.yaml for type consistency (SSOT)

Generated Output:
- ProtocolConstants.hpp (~50-100 lines)
- Namespace: Protocol
- All constexpr (zero runtime cost)
"""

from __future__ import annotations

from typing import TYPE_CHECKING, TypedDict
from pathlib import Path

if TYPE_CHECKING:
    from protocol.type_loader import TypeRegistry


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
    message_id_start: int


def generate_constants_hpp(protocol_config: ProtocolConfig, type_registry: TypeRegistry, output_path: Path) -> str:
    """
    Generate ProtocolConstants.hpp from protocol_config.yaml.

    Args:
        protocol_config: Dict loaded from protocol_config.yaml
        type_registry: TypeRegistry instance with loaded builtin types
        output_path: Path where ProtocolConstants.hpp will be written

    Returns:
        Generated C++ code as string

    Example:
        >>> with open('protocol_config.yaml') as f:
        ...     config = yaml.safe_load(f)
        >>> registry = TypeRegistry()
        >>> registry.load_builtins(Path('builtin_types.yaml'))
        >>> code = generate_constants_hpp(config, registry, Path('ProtocolConstants.hpp'))
    """
    # Extract C++ types from builtin_types.yaml (SSOT)
    uint8_cpp = type_registry.get('uint8').cpp_type
    uint16_cpp = type_registry.get('uint16').cpp_type

    if uint8_cpp is None or uint16_cpp is None:
        raise ValueError("Missing C++ type mappings for uint8 or uint16 in builtin_types.yaml")

    header = _generate_header()
    sysex_constants = _generate_sysex_constants(protocol_config.get('sysex', {}), uint8_cpp)
    limits = _generate_limits(protocol_config.get('limits', {}), uint8_cpp, uint16_cpp)
    role_constants = _generate_role_constants(protocol_config.get('roles', {}))
    footer = _generate_footer()

    full_code = f"{header}\n{sysex_constants}\n{limits}\n{role_constants}\n{footer}"
    return full_code


def _generate_header() -> str:
    """Generate file header."""
    return '''/**
 * ProtocolConstants.hpp - Protocol Configuration Constants
 *
 * AUTO-GENERATED - DO NOT EDIT
 * Generated from: protocol_config.yaml
 *
 * This file contains all protocol constants including SysEx framing,
 * message structure offsets, and encoding limits.
 *
 * All constants are constexpr (compile-time, zero runtime cost).
 */

#pragma once

#include <cstdint>

namespace Protocol {

// ============================================================================
// SYSEX FRAMING CONSTANTS
// ============================================================================
'''


def _generate_sysex_constants(sysex_config: SysExConfig, uint8_type: str) -> str:
    """Generate SysEx framing constants using types from builtin_types.yaml."""
    if not sysex_config:
        return "// No SysEx config found\n"

    lines: list[str] = []

    # Message delimiters
    start: int = sysex_config.get('start', 0xF0)
    end: int = sysex_config.get('end', 0xF7)
    lines.append(f"constexpr {uint8_type} SYSEX_START = {start:#04x};  // SysEx start byte")
    lines.append(f"constexpr {uint8_type} SYSEX_END = {end:#04x};    // SysEx end byte")
    lines.append("")

    # Protocol identifiers
    manufacturer_id: int = sysex_config.get('manufacturer_id', 0x7F)
    device_id: int = sysex_config.get('device_id', 0x01)
    lines.append(f"constexpr {uint8_type} MANUFACTURER_ID = {manufacturer_id:#04x};  // MIDI manufacturer ID")
    lines.append(f"constexpr {uint8_type} DEVICE_ID = {device_id:#04x};        // Device identifier")
    lines.append("")

    # Message structure
    min_length: int = sysex_config.get('min_message_length', 6)
    type_offset: int = sysex_config.get('message_type_offset', 3)
    from_host_offset: int = sysex_config.get('from_host_offset', 4)
    payload_offset: int = sysex_config.get('payload_offset', 5)
    lines.append(f"constexpr {uint8_type} MIN_MESSAGE_LENGTH = {min_length};  // Minimum valid SysEx message")
    lines.append(f"constexpr {uint8_type} MESSAGE_TYPE_OFFSET = {type_offset};  // Position of MessageID byte")
    lines.append(f"constexpr {uint8_type} FROM_HOST_OFFSET = {from_host_offset};      // Position of fromHost flag")
    lines.append(f"constexpr {uint8_type} PAYLOAD_OFFSET = {payload_offset};      // Start of payload data")

    return "\n".join(lines)


def _generate_limits(limits_config: LimitsConfig, uint8_type: str, uint16_type: str) -> str:
    """Generate encoding limits constants using types from builtin_types.yaml."""
    if not limits_config:
        return ""

    lines: list[str] = [
        "",
        "// ============================================================================",
        "// ENCODING LIMITS",
        "// ============================================================================",
        ""
    ]

    # String limits
    string_max: int = limits_config.get('string_max_length', 16)
    lines.append(f"constexpr {uint8_type} STRING_MAX_LENGTH = {string_max};  // Max chars per string field")

    # Array limits
    array_max: int = limits_config.get('array_max_items', 8)
    lines.append(f"constexpr {uint8_type} ARRAY_MAX_ITEMS = {array_max};      // Max items per array field")

    # Payload limits
    max_payload: int = limits_config.get('max_payload_size', 256)
    max_message: int = limits_config.get('max_message_size', 261)
    lines.append(f"constexpr {uint16_type} MAX_PAYLOAD_SIZE = {max_payload};    // Max payload bytes")
    lines.append(f"constexpr {uint16_type} MAX_MESSAGE_SIZE = {max_message};    // Max total message bytes")

    return "\n".join(lines)


def _generate_role_constants(roles_config: RolesConfig) -> str:
    """Generate role constants (IS_HOST flag for protocol direction)."""
    if not roles_config:
        # Default: C++ is controller
        is_host: bool = False
    else:
        cpp_role: str = roles_config.get('cpp', 'controller')
        is_host: bool = (cpp_role == 'host')

    lines: list[str] = [
        "",
        "// ============================================================================",
        "// ROLE CONFIGURATION",
        "// ============================================================================",
        "",
        f"constexpr bool IS_HOST = {'true' if is_host else 'false'};  // This code's role in the protocol"
    ]

    return "\n".join(lines)


def _generate_footer() -> str:
    """Generate namespace closing."""
    return '''

}  // namespace Protocol
'''
