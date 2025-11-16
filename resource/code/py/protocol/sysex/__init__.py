"""
SysEx Protocol Configuration Module

Provides validated, type-safe SysEx configuration with:
- Pydantic models for strict validation
- YAML loading with automatic validation
- Plugin override support with deep merge
- Builtin defaults for standard MIDI SysEx
"""

from .sysex_config import (
    SysExFraming,
    SysExStructure,
    SysExLimits,
    SysExConfig,
    BUILTIN_CONFIG,
    load_sysex_config,
)

__all__ = [
    'SysExFraming',
    'SysExStructure',
    'SysExLimits',
    'SysExConfig',
    'BUILTIN_CONFIG',
    'load_sysex_config',
]
