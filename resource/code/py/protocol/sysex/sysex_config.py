"""
SysEx Protocol Configuration

Pydantic-based configuration system for SysEx protocol constants.
Provides type-safe, validated configuration with YAML loading and plugin override support.

Key Features:
- Strict validation (ranges, types, constraints)
- YAML loading with automatic validation
- Deep merge for plugin overrides
- Builtin defaults for standard MIDI SysEx
- IDE autocompletion support
- Clear error messages

Architecture:
- Builtin defaults: resource/code/py/protocol/sysex_protocol_config.yaml
- Plugin overrides: plugin/*/sysex_protocol/sysex_protocol_config.yaml (optional)
- Merge strategy: Plugin overrides > Builtin defaults

Example:
    >>> from protocol.sysex import load_sysex_config
    >>> config = load_sysex_config(Path('plugin/bitwig/sysex_protocol'))
    >>> print(config.framing.manufacturer_id)  # 0x7F (from Bitwig override)
"""

from __future__ import annotations

from pydantic import BaseModel, Field, field_validator
from pathlib import Path
from typing import Any


class SysExFraming(BaseModel):
    """
    SysEx message framing bytes (MIDI standard).

    Defines the bytes used to frame SysEx messages according to MIDI specification.
    """
    start: int = Field(
        default=0xF0,
        ge=0x00,
        le=0xFF,
        description="SysEx start byte (MIDI standard: 0xF0)"
    )

    end: int = Field(
        default=0xF7,
        ge=0x00,
        le=0xFF,
        description="SysEx end byte (MIDI standard: 0xF7)"
    )

    manufacturer_id: int = Field(
        default=0x7D,
        ge=0x00,
        le=0xFF,
        description="MIDI Manufacturer ID (0x7D = Educational/development use)"
    )

    device_id: int = Field(
        default=0x01,
        ge=0x00,
        le=0x7F,
        description="Device identifier (MIDI 7-bit: 0x00-0x7F)"
    )

    class Config:
        validate_assignment = True
        extra = "forbid"  # Reject unknown fields


class SysExStructure(BaseModel):
    """
    SysEx message structure offsets.

    Defines the byte positions within a SysEx message:
    [F0] [MID] [DID] [TYPE] [FROM] [PAYLOAD...] [F7]
     0     1     2     3      4      5+           N
    """
    min_message_length: int = Field(
        default=6,
        gt=0,
        le=256,
        description="Minimum valid SysEx message length (6: [F0 MID DID TYPE FROM F7])"
    )

    message_type_offset: int = Field(
        default=3,
        ge=0,
        le=255,
        description="Byte position of MessageID within SysEx message"
    )

    from_host_offset: int = Field(
        default=4,
        ge=0,
        le=255,
        description="Byte position of fromHost flag within SysEx message"
    )

    payload_offset: int = Field(
        default=5,
        ge=0,
        le=255,
        description="Byte position where payload data starts"
    )

    @field_validator('from_host_offset')
    @classmethod
    def from_host_after_type(cls, v: int, info: Any) -> int:
        """Ensure fromHost flag comes after message type."""
        message_type_offset: int | None = info.data.get('message_type_offset')
        if message_type_offset is not None and v <= message_type_offset:
            raise ValueError(
                f"from_host_offset ({v}) must be > message_type_offset ({message_type_offset})"
            )
        return v

    @field_validator('payload_offset')
    @classmethod
    def payload_after_from_host(cls, v: int, info: Any) -> int:
        """Ensure payload starts after fromHost flag."""
        from_host_offset: int | None = info.data.get('from_host_offset')
        if from_host_offset is not None and v <= from_host_offset:
            raise ValueError(
                f"payload_offset ({v}) must be > from_host_offset ({from_host_offset})"
            )
        return v

    class Config:
        validate_assignment = True
        extra = "forbid"


class SysExLimits(BaseModel):
    """
    Protocol encoding limits.

    Defines maximum sizes for various data types to prevent buffer overflows
    and ensure MIDI-safe encoding.
    """
    string_max_length: int = Field(
        default=16,
        gt=0,
        le=127,
        description="Max characters per string field (7-bit length encoding: 0-127)"
    )

    array_max_items: int = Field(
        default=8,
        gt=0,
        le=255,
        description="Max items per array field"
    )

    max_payload_size: int = Field(
        default=256,
        gt=0,
        le=65535,
        description="Max payload bytes (7-bit encoded, excludes framing)"
    )

    max_message_size: int = Field(
        default=261,
        gt=0,
        le=65535,
        description="Max total message bytes (framing + payload + end)"
    )

    @field_validator('max_message_size')
    @classmethod
    def message_larger_than_payload(cls, v: int, info: Any) -> int:
        """Ensure total message size accounts for framing overhead."""
        max_payload_size: int | None = info.data.get('max_payload_size')
        if max_payload_size is not None and v < max_payload_size:
            raise ValueError(
                f"max_message_size ({v}) must be >= max_payload_size ({max_payload_size})"
            )
        return v

    class Config:
        validate_assignment = True
        extra = "forbid"


class MessageIDRange(BaseModel):
    """MessageID allocation range for a Flow direction."""
    start: int = Field(ge=0x00, le=0xFF, description="Range start (inclusive)")
    end: int = Field(ge=0x00, le=0xFF, description="Range end (inclusive)")

    @field_validator('end')
    @classmethod
    def end_after_start(cls, v: int, info: Any) -> int:
        """Ensure range end is after start."""
        start: int | None = info.data.get('start')
        if start is not None and v < start:
            raise ValueError(f"Range end ({v}) must be >= start ({start})")
        return v

    @property
    def slots(self) -> int:
        """Number of available slots in this range."""
        return self.end - self.start + 1

    class Config:
        validate_assignment = True
        extra = "forbid"


class MessageIDRanges(BaseModel):
    """MessageID allocation ranges by Flow direction."""
    controller_to_host: MessageIDRange = Field(
        default=MessageIDRange(start=0x00, end=0x3F),
        description="Controller→Host messages (hardware actions)"
    )

    host_to_controller: MessageIDRange = Field(
        default=MessageIDRange(start=0x40, end=0xBF),
        description="Host→Controller messages (DAW feedback)"
    )

    bidirectional: MessageIDRange = Field(
        default=MessageIDRange(start=0xC0, end=0xC7),
        description="Bidirectional messages (ping, handshake, diagnostics)"
    )

    class Config:
        validate_assignment = True
        extra = "forbid"


class ProtocolRoles(BaseModel):
    """
    Protocol role configuration.

    Defines which code (cpp/java) acts as host vs controller.
    This determines the fromHost flag value when encoding messages.
    """
    cpp: str = Field(
        default="controller",
        description="Role for C++ code (controller or host)"
    )

    java: str = Field(
        default="host",
        description="Role for Java code (controller or host)"
    )

    @field_validator('cpp', 'java')
    @classmethod
    def validate_role(cls, v: str) -> str:
        """Ensure role is either 'host' or 'controller'."""
        if v not in ['host', 'controller']:
            raise ValueError(f"Role must be 'host' or 'controller', got '{v}'")
        return v

    class Config:
        validate_assignment = True
        extra = "forbid"


class SysExConfig(BaseModel):
    """
    Complete SysEx protocol configuration.

    Top-level configuration container with five main sections:
    - framing: SysEx message delimiters and IDs
    - structure: Message byte offsets
    - limits: Encoding size constraints
    - message_id_ranges: MessageID allocation by Flow direction
    - roles: Host/controller role assignment per language

    Usage:
        >>> config = SysExConfig()  # Use builtin defaults
        >>> config.framing.manufacturer_id = 0x7F  # Override specific value
    """
    framing: SysExFraming = Field(
        default=SysExFraming(),
        description="SysEx framing bytes and identifiers"
    )

    structure: SysExStructure = Field(
        default=SysExStructure(),
        description="Message structure offsets"
    )

    limits: SysExLimits = Field(
        default=SysExLimits(),
        description="Protocol encoding limits"
    )

    message_id_ranges: MessageIDRanges = Field(
        default=MessageIDRanges(),
        description="MessageID allocation ranges by Flow direction"
    )

    roles: ProtocolRoles = Field(
        default=ProtocolRoles(),
        description="Host/controller role assignment per language"
    )


    def to_dict(self) -> dict[str, Any]:
        """
        Export configuration as dictionary.

        Useful for compatibility with legacy code expecting dict format.

        Returns:
            Nested dictionary representation
        """
        return {
            'sysex': {
                'start': self.framing.start,
                'end': self.framing.end,
                'manufacturer_id': self.framing.manufacturer_id,
                'device_id': self.framing.device_id,
                'min_message_length': self.structure.min_message_length,
                'message_type_offset': self.structure.message_type_offset,
                'from_host_offset': self.structure.from_host_offset,
                'payload_offset': self.structure.payload_offset,
            },
            'limits': {
                'string_max_length': self.limits.string_max_length,
                'array_max_items': self.limits.array_max_items,
                'max_payload_size': self.limits.max_payload_size,
                'max_message_size': self.limits.max_message_size,
            },
            'roles': {
                'cpp': self.roles.cpp,
                'java': self.roles.java,
            }
        }

    class Config:
        validate_assignment = True
        extra = "forbid"


# ============================================================================
# BUILTIN DEFAULT CONFIGURATION
# ============================================================================

# Import builtin config from pure Python module
from .sysex_builtin_config import BUILTIN_SYSEX_CONFIG as BUILTIN_CONFIG


# ============================================================================
# CONFIGURATION LOADER WITH OVERRIDE SUPPORT
# ============================================================================

def load_sysex_config(plugin_dir: Path) -> SysExConfig:
    """
    Load SysEx configuration with plugin override support.

    Loading strategy:
    1. Start with BUILTIN_CONFIG (global defaults from Python)
    2. Try to import plugin-specific config from sysex_protocol_config.py
    3. If found, use plugin config (which can inherit/override builtin)
    4. Return validated configuration

    Args:
        plugin_dir: Path to plugin directory (e.g., plugin/bitwig/sysex_protocol)

    Returns:
        Validated SysExConfig instance (builtin + plugin overrides)

    Example:
        >>> config = load_sysex_config(Path('plugin/bitwig/sysex_protocol'))
        >>> # Uses plugin-specific config or builtin defaults
    """
    # Check for plugin-specific Python config
    plugin_config_file = plugin_dir / "sysex_protocol_config.py"

    if plugin_config_file.exists():
        try:
            # Import plugin config dynamically
            import sys
            import importlib.util

            spec = importlib.util.spec_from_file_location("plugin_sysex_config", plugin_config_file)
            if spec is None or spec.loader is None:
                raise ImportError(f"Cannot load spec from {plugin_config_file}")

            plugin_module = importlib.util.module_from_spec(spec)
            sys.modules["plugin_sysex_config"] = plugin_module
            spec.loader.exec_module(plugin_module)

            # Get the PLUGIN_SYSEX_CONFIG from the module
            if hasattr(plugin_module, 'PLUGIN_SYSEX_CONFIG'):
                plugin_config: SysExConfig = plugin_module.PLUGIN_SYSEX_CONFIG
                print(f"  > Loaded SysEx config with overrides from {plugin_config_file.name}")
                return plugin_config
            else:
                print(f"  ! Warning: {plugin_config_file.name} doesn't define PLUGIN_SYSEX_CONFIG")
                print(f"     Using builtin defaults only.")
        except Exception as e:
            print(f"  ! Warning: Failed to load plugin sysex_protocol_config.py: {e}")
            print(f"     Using builtin defaults only.")

    # Use builtin defaults
    return BUILTIN_CONFIG
