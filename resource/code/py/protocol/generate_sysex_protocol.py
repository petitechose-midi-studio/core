#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MIDI Studio - ID-Based Protocol Generator (v2.0)
Generates complete protocol code (C++ + Java) from YAML definitions.

This is a complete rewrite using the new type-safe architecture:
- Atomic types (YAML) with builtin_types.yaml
- Message compositions (Python) with Flow enum
- Auto-generated Encoder library for consistency
- Strict validation with circular dependency detection
- Auto-allocated MessageIDs by Flow direction

Usage:
    From PlatformIO (recommended):
    pio run --target bitwig-protocol

    Direct invocation:
    uv run python protocol/generate_sysex_protocol.py <plugin_name>
    uv run python protocol/generate_sysex_protocol.py bitwig

Architecture (10 steps):
    1. Load configuration + type registry
    2. Generate Encoder library (C++ + Java)
    3. Import messages dynamically
    4. Validate messages strictly
    5. Allocate MessageIDs by Flow
    6. Generate MessageID enums (C++ + Java)
    7. Generate structs (C++ + Java)
    8. Generate Protocol class (C++ + Java) - unified send/receive API
    9. Generate ProtocolConstants (C++ + Java)
    10. Print summary

Part of Step 2.6 - Phase 4 (Main Orchestrator)
"""

from __future__ import annotations

import sys
import io
from pathlib import Path
from typing import Dict, List, TypedDict, Any, TYPE_CHECKING

if TYPE_CHECKING:
    from protocol.message import Message

# ============================================================================
# UNICODE FIX FOR WINDOWS
# ============================================================================
# Force UTF-8 encoding for stdout/stderr to support Unicode characters (✓, ❌, etc.)
# This prevents UnicodeEncodeError on Windows where the default encoding is cp1252
if sys.platform == 'win32':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

# Ensure we're importing from the correct location
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent.parent  # Go up 4 levels: protocol -> py -> code -> resource -> midi-studio

# Add protocol package to path
sys.path.insert(0, str(SCRIPT_DIR.parent))

# Import our new infrastructure
from protocol.type_loader import TypeRegistry
from protocol.message_importer import import_sysex_messages
from protocol.validator import ProtocolValidator
from protocol.message_id_allocator import allocate_message_ids
from protocol.sysex import load_sysex_config
from protocol.generators.cpp import (
    generate_encoder_hpp,
    generate_decoder_hpp,
    generate_messageid_hpp,
    generate_struct_hpp,
    generate_constants_hpp,
    generate_message_structure_hpp,
    generate_decoder_registry_hpp,
    generate_protocol_callbacks_hpp,
)
from protocol.generators.cpp.logger_generator import generate_logger_hpp
from protocol.generators.java import (
    generate_encoder_java,
    generate_decoder_java,
    generate_messageid_java,
    generate_struct_java,
    generate_constants_java,
    generate_decoder_registry_java,
    generate_protocol_callbacks_java,
)


# ============================================================================
# TYPE DEFINITIONS
# ============================================================================

class PluginConfig(TypedDict):
    """Type definition for plugin configuration dictionary."""
    plugin_name: str
    plugin_dir: Path
    sysex_dir: Path
    paths: Dict[str, Any]
    sysex_config: Any  # SysExConfig from protocol.sysex
    builtin_types_path: None  # DEPRECATED - now using Python config
    messages_path: Path


# ============================================================================
# CONFIGURATION
# ============================================================================

PLUGIN_DIR = PROJECT_ROOT / "plugin"


# ============================================================================
# STEP 1: LOAD CONFIGURATION
# ============================================================================

def load_plugin_config(plugin_name: str) -> PluginConfig:
    """
    Load all configuration for a plugin.

    Returns dict with:
        - plugin_dir: Path to plugin directory (plugin/bitwig, NOT plugin/bitwig/sysex_protocol)
        - sysex_dir: Path to sysex_protocol directory
        - paths: Output paths from plugin_paths.yaml
        - sysex_config: SysExConfig instance (Pydantic validated)
        - builtin_types_path: REMOVED - now using Python config
        - messages_path: Path to sysex_messages.py
    """
    plugin_dir = PLUGIN_DIR / plugin_name
    sysex_dir = plugin_dir / "sysex_protocol"

    if not sysex_dir.exists():
        raise FileNotFoundError(f"SysEx protocol directory not found: {sysex_dir}")

    # Load plugin_paths.py (pure Python configuration)
    paths_file = sysex_dir / "plugin_paths.py"
    if not paths_file.exists():
        raise FileNotFoundError(f"Missing {paths_file}")

    # Import plugin paths dynamically
    import importlib.util
    spec = importlib.util.spec_from_file_location("plugin_paths", paths_file)
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot load spec from {paths_file}")

    paths_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(paths_module)

    if not hasattr(paths_module, 'PLUGIN_PATHS'):
        raise ValueError(f"{paths_file} must define PLUGIN_PATHS")

    paths_config: dict[str, Any] = paths_module.PLUGIN_PATHS

    # Load SysEx configuration (builtin defaults + plugin overrides)
    sysex_config = load_sysex_config(sysex_dir)

    # Resolve paths
    # Builtin types are now in builtin_types.py (Python-based, type-safe)
    # No more YAML parsing for types
    messages_path = sysex_dir / "sysex_messages.py"

    # Validate required files exist
    if not messages_path.exists():
        raise FileNotFoundError(f"Missing {messages_path}")

    return {
        'plugin_name': plugin_name,  # For plugin-specific generation
        'plugin_dir': plugin_dir,  # For import_sysex_messages (plugin/bitwig)
        'sysex_dir': sysex_dir,
        'paths': paths_config,
        'sysex_config': sysex_config,  # Pydantic validated config
        'builtin_types_path': None,  # DEPRECATED - using Python config
        'messages_path': messages_path,
    }


def load_type_registry(config: PluginConfig) -> TypeRegistry:
    """
    Step 1: Load TypeRegistry with builtin types only.

    Note: Builtin types are now loaded from builtin_types.py (pure Python).
    Field composition is done directly in Python via fields.py.
    This provides better DRY, type safety, and IDE support.
    """
    registry = TypeRegistry()
    registry.load_builtins()  # No arguments - loads from builtin_types.py
    # No more load_custom_types() - fields.py handles composition

    # CRITICAL: Populate Type enum for sysex_messages.py imports
    from protocol.field import populate_type_names
    type_names = list(registry.types.keys())
    populate_type_names(type_names)

    # Generate field.pyi stub file for Pylance autocomplete
    generate_type_stubs(registry)

    return registry


def generate_type_stubs(type_registry: TypeRegistry) -> None:
    """
    Generate field.pyi stub file by calling the standalone generator.

    This delegates to generate_type_stubs.py which uses introspection
    to generate stubs from the actual dataclass definitions.
    """
    # Import and call the standalone generator
    from protocol.generate_type_stubs import generate_stub_file
    generate_stub_file()


# ============================================================================
# STEP 2: GENERATE ENCODER LIBRARY
# ============================================================================

def generate_encoders(type_registry: TypeRegistry, config: PluginConfig) -> None:
    """Step 2: Generate Encoder/Decoder/Logger files (C++ and Java) from builtin types."""
    paths = config['paths']

    # C++ Encoder
    cpp_output_dir = PROJECT_ROOT / paths['output_cpp']['encoder']
    cpp_output_dir.mkdir(parents=True, exist_ok=True)
    cpp_encoder_path = cpp_output_dir / "Encoder.hpp"
    cpp_encoder_code = generate_encoder_hpp(type_registry, cpp_encoder_path)
    cpp_encoder_path.write_text(cpp_encoder_code, encoding='utf-8')
    print(f"  ✓ Generated {cpp_encoder_path.relative_to(PROJECT_ROOT)}")

    # C++ Decoder
    cpp_decoder_path = cpp_output_dir / "Decoder.hpp"
    cpp_decoder_code = generate_decoder_hpp(type_registry, cpp_decoder_path)
    cpp_decoder_path.write_text(cpp_decoder_code, encoding='utf-8')
    print(f"  ✓ Generated {cpp_decoder_path.relative_to(PROJECT_ROOT)}")

    # C++ Logger (NEW)
    cpp_logger_path = cpp_output_dir / "Logger.hpp"
    cpp_logger_code = generate_logger_hpp(cpp_logger_path)
    cpp_logger_path.write_text(cpp_logger_code, encoding='utf-8')
    print(f"  ✓ Generated {cpp_logger_path.relative_to(PROJECT_ROOT)}")

    # Java Encoder
    java_output_dir = PROJECT_ROOT / paths['output_java']['encoder']
    java_output_dir.mkdir(parents=True, exist_ok=True)
    java_encoder_path = java_output_dir / "Encoder.java"
    java_encoder_code = generate_encoder_java(type_registry, java_encoder_path)
    java_encoder_path.write_text(java_encoder_code, encoding='utf-8')
    print(f"  ✓ Generated {java_encoder_path.relative_to(PROJECT_ROOT)}")

    # Java Decoder
    java_decoder_path = java_output_dir / "Decoder.java"
    java_decoder_code = generate_decoder_java(type_registry, java_decoder_path)
    java_decoder_path.write_text(java_decoder_code, encoding='utf-8')
    print(f"  ✓ Generated {java_decoder_path.relative_to(PROJECT_ROOT)}")


# ============================================================================
# STEP 3-5: IMPORT + VALIDATE + ALLOCATE
# ============================================================================

def import_and_validate_messages(config: PluginConfig, type_registry: TypeRegistry) -> tuple[List[Message], Dict[str, int]]:
    """Steps 3-5: Import messages, validate, and allocate IDs."""
    # Step 3: Import messages
    messages = import_sysex_messages(config['plugin_dir'])
    print(f"  ✓ Imported {len(messages)} messages from sysex_messages.py")

    # Step 4: Validate
    validator = ProtocolValidator(type_registry)
    errors = validator.validate_messages(messages)

    if errors:
        print("\n❌ Validation Errors:")
        for error in errors:
            print(f"  - {error}")
        raise ValueError(f"Protocol validation failed with {len(errors)} error(s)")

    print(f"  ✓ Validation passed (checked {len(messages)} messages)")

    # Step 5: Allocate IDs
    allocations = allocate_message_ids(messages)
    print(f"  ✓ Allocated MessageIDs:")
    print(f"    - Total messages: {len(messages)} (0x00-0x{len(messages)-1:02X})")

    return messages, allocations


# ============================================================================
# STEP 6: GENERATE MESSAGEID ENUMS
# ============================================================================

def generate_messageid_enums(messages: List[Message], allocations: Dict[str, int], type_registry: TypeRegistry, config: PluginConfig) -> None:
    """Step 6: Generate MessageID.hpp and MessageID.java."""
    paths = config['paths']

    # C++ MessageID
    cpp_output_dir = PROJECT_ROOT / paths['output_cpp']['messageid']
    cpp_output_dir.mkdir(parents=True, exist_ok=True)
    cpp_output_path = cpp_output_dir / "MessageID.hpp"

    cpp_code = generate_messageid_hpp(messages, allocations, type_registry, cpp_output_path)
    cpp_output_path.write_text(cpp_code, encoding='utf-8')
    print(f"  ✓ Generated {cpp_output_path.relative_to(PROJECT_ROOT)}")

    # Java MessageID
    java_output_dir = PROJECT_ROOT / paths['output_java']['messageid']
    java_output_dir.mkdir(parents=True, exist_ok=True)
    java_output_path = java_output_dir / "MessageID.java"

    java_code = generate_messageid_java(messages, allocations, type_registry, java_output_path)
    java_output_path.write_text(java_code, encoding='utf-8')
    print(f"  ✓ Generated {java_output_path.relative_to(PROJECT_ROOT)}")


# ============================================================================
# STEP 7: GENERATE STRUCTS (with cleanup)
# ============================================================================

def cleanup_obsolete_structs(messages: List[Message], config: PluginConfig) -> None:
    """
    Clean up struct files that no longer correspond to messages in ALL_MESSAGES.

    This ensures the generated code stays in sync with the message definitions.
    Files are removed if they don't match any message name.
    """
    paths = config['paths']

    # Get set of expected struct names
    expected_structs = {f"{message.name}Message" for message in messages}

    # Cleanup C++ structs
    cpp_struct_dir = PROJECT_ROOT / paths['output_cpp']['structs']
    if cpp_struct_dir.exists():
        cpp_removed: list[str] = []
        for hpp_file in cpp_struct_dir.glob("*.hpp"):
            struct_name = hpp_file.stem  # Filename without extension
            if struct_name not in expected_structs:
                hpp_file.unlink()
                cpp_removed.append(struct_name)

        if cpp_removed:
            print(f"  ✓ Removed {len(cpp_removed)} obsolete C++ struct(s): {', '.join(cpp_removed)}")

    # Cleanup Java structs
    java_struct_dir = PROJECT_ROOT / paths['output_java']['structs']
    if java_struct_dir.exists():
        java_removed: list[str] = []
        for java_file in java_struct_dir.glob("*.java"):
            class_name = java_file.stem  # Filename without extension
            if class_name not in expected_structs:
                java_file.unlink()
                java_removed.append(class_name)

        if java_removed:
            print(f"  ✓ Removed {len(java_removed)} obsolete Java class(es): {', '.join(java_removed)}")


def generate_structs(messages: List[Message], allocations: Dict[str, int], type_registry: TypeRegistry, config: PluginConfig) -> None:
    """Step 7: Generate struct/*.hpp and struct/*.java for messages."""
    paths = config['paths']

    if not messages:
        print("  ⚠️   No messages to generate")
        return

    # CLEANUP: Remove obsolete struct files before generating new ones
    cleanup_obsolete_structs(messages, config)

    # C++ structs
    cpp_struct_dir = PROJECT_ROOT / paths['output_cpp']['structs']
    cpp_struct_dir.mkdir(parents=True, exist_ok=True)

    for message in messages:
        # Convert SCREAMING_SNAKE_CASE to PascalCase for filenames
        pascal_name = ''.join(word.capitalize() for word in message.name.split('_'))
        struct_name = f"{pascal_name}Message"
        cpp_output_path = cpp_struct_dir / f"{struct_name}.hpp"

        # Get MessageID for this message
        message_id = allocations[message.name]

        string_max_length = config['sysex_config'].limits.string_max_length
        cpp_code = generate_struct_hpp(message, message_id, type_registry, cpp_output_path, string_max_length)
        cpp_output_path.write_text(cpp_code, encoding='utf-8')

    print(f"  ✓ Generated {len(messages)} C++ structs in {cpp_struct_dir.relative_to(PROJECT_ROOT)}")

    # Java message classes
    java_struct_dir = PROJECT_ROOT / paths['output_java']['structs']
    java_struct_dir.mkdir(parents=True, exist_ok=True)

    for message in messages:
        # Convert SCREAMING_SNAKE_CASE to PascalCase for filenames
        pascal_name = ''.join(word.capitalize() for word in message.name.split('_'))
        class_name = f"{pascal_name}Message"
        java_output_path = java_struct_dir / f"{class_name}.java"

        # Get MessageID for this message
        message_id = allocations[message.name]

        string_max_length = config['sysex_config'].limits.string_max_length
        java_code = generate_struct_java(message, message_id, type_registry, java_output_path, string_max_length)
        java_output_path.write_text(java_code, encoding='utf-8')

    print(f"  ✓ Generated {len(messages)} Java classes in {java_struct_dir.relative_to(PROJECT_ROOT)}")


# ============================================================================
# STEP 8: GENERATE MESSAGESTRUCTURE (Umbrella header for all messages)
# ============================================================================

def generate_message_structure_files(messages: List[Message], config: PluginConfig) -> None:
    """Step 8: Generate MessageStructure.hpp and MessageStructure.java (umbrella headers)."""
    plugin_name = config['plugin_name']

    # Java MessageStructure (DISABLED - not used, causes unnecessary import warnings)
    # java_protocol_dir = PROJECT_ROOT / f"plugin/{plugin_name}/host/src/main/java/com/midi_studio/protocol"
    # java_protocol_dir.mkdir(parents=True, exist_ok=True)
    # java_message_structure_path = java_protocol_dir / "MessageStructure.java"

    # package = "com.midi_studio"  # Fixed package for now
    # generate_message_structure_java(messages, package, java_message_structure_path)
    # print(f"  ✓ Generated {java_message_structure_path.relative_to(PROJECT_ROOT)}")

    java_protocol_dir = PROJECT_ROOT / f"plugin/{plugin_name}/host/src/main/java/com/midi_studio/protocol"
    java_protocol_dir.mkdir(parents=True, exist_ok=True)
    package = "com.midi_studio"  # Fixed package for now

    # Java ProtocolCallbacks
    java_callbacks_path = java_protocol_dir / "ProtocolCallbacks.java"
    generate_protocol_callbacks_java(messages, package, java_callbacks_path)
    print(f"  ✓ Generated {java_callbacks_path.relative_to(PROJECT_ROOT)}")

    # Java DecoderRegistry
    java_decoder_registry_path = java_protocol_dir / "DecoderRegistry.java"
    generate_decoder_registry_java(messages, package, java_decoder_registry_path)
    print(f"  ✓ Generated {java_decoder_registry_path.relative_to(PROJECT_ROOT)}")

    # C++ MessageStructure
    cpp_protocol_dir = PROJECT_ROOT / f"plugin/{plugin_name}/embedded/protocol"
    cpp_protocol_dir.mkdir(parents=True, exist_ok=True)
    cpp_message_structure_path = cpp_protocol_dir / "MessageStructure.hpp"

    generate_message_structure_hpp(messages, cpp_message_structure_path)
    print(f"  ✓ Generated {cpp_message_structure_path.relative_to(PROJECT_ROOT)}")

    # C++ ProtocolCallbacks
    cpp_callbacks_path = cpp_protocol_dir / "ProtocolCallbacks.hpp"
    generate_protocol_callbacks_hpp(messages, cpp_callbacks_path)
    print(f"  ✓ Generated {cpp_callbacks_path.relative_to(PROJECT_ROOT)}")

    # C++ DecoderRegistry
    cpp_decoder_registry_path = cpp_protocol_dir / "DecoderRegistry.hpp"
    generate_decoder_registry_hpp(messages, cpp_decoder_registry_path)
    print(f"  ✓ Generated {cpp_decoder_registry_path.relative_to(PROJECT_ROOT)}")


# ============================================================================
# STEP 9: GENERATE PROTOCOLCONSTANTS
# ============================================================================

def generate_constants(type_registry: TypeRegistry, config: PluginConfig) -> None:
    """Step 9: Generate ProtocolConstants.hpp and ProtocolConstants.java."""
    paths = config['paths']
    sysex_config = config['sysex_config']

    # Convert Pydantic config to dict format for generators (legacy compatibility)
    protocol_config_dict = sysex_config.to_dict()

    # C++ ProtocolConstants
    cpp_output_dir = PROJECT_ROOT / paths['output_cpp']['constants']
    cpp_output_dir.mkdir(parents=True, exist_ok=True)
    cpp_output_path = cpp_output_dir / "ProtocolConstants.hpp"

    cpp_code = generate_constants_hpp(protocol_config_dict, type_registry, cpp_output_path)
    cpp_output_path.write_text(cpp_code, encoding='utf-8')
    print(f"  ✓ Generated {cpp_output_path.relative_to(PROJECT_ROOT)}")

    # Java ProtocolConstants
    java_output_dir = PROJECT_ROOT / paths['output_java']['constants']
    java_output_dir.mkdir(parents=True, exist_ok=True)
    java_output_path = java_output_dir / "ProtocolConstants.java"

    java_code = generate_constants_java(protocol_config_dict, java_output_path)
    java_output_path.write_text(java_code, encoding='utf-8')
    print(f"  ✓ Generated {java_output_path.relative_to(PROJECT_ROOT)}")


# ============================================================================
# STEP 10: SUMMARY
# ============================================================================

def print_summary(messages: List[Message], type_registry: TypeRegistry, config: PluginConfig) -> None:
    """Step 10: Print generation summary."""
    paths = config['paths']

    builtin_count = sum(1 for t in type_registry.types.values() if t.is_builtin)
    atomic_count = sum(1 for t in type_registry.types.values() if not t.is_builtin)

    print("\n" + "=" * 70)
    print("GENERATION SUMMARY")
    print("=" * 70)
    print(f"Types:")
    print(f"  - Builtin types: {builtin_count}")
    print(f"  - Atomic types: {atomic_count}")
    print(f"  - Total: {builtin_count + atomic_count}")
    print()
    print(f"Messages: {len(messages)}")
    print()
    print(f"Output locations:")
    print(f"  - C++:  {paths['output_cpp']['base_path']}")
    print(f"  - Java: {paths['output_java']['base_path']}")
    print("=" * 70)


# ============================================================================
# MAIN ORCHESTRATOR
# ============================================================================

def generate_plugin_protocol(plugin_name: str) -> None:
    """
    Main orchestration function - executes all 10 steps in sequence.
    """
    print("=" * 70)
    print(f"MIDI Studio - ID-Based Protocol Generator v2.0")
    print(f"Plugin: {plugin_name}")
    print("=" * 70)
    print()

    try:
        # Step 1: Load configuration
        print("[Step 1/10] Loading configuration...")
        config = load_plugin_config(plugin_name)
        type_registry = load_type_registry(config)
        print(f"  ✓ Loaded {len(type_registry.types)} types")
        print()

        # Step 2: Generate Encoder library
        print("[Step 2/10] Generating Encoder library...")
        generate_encoders(type_registry, config)
        print()

        # Steps 3-5: Import + Validate + Allocate
        print("[Steps 3-5/10] Importing, validating, and allocating MessageIDs...")
        messages, allocations = import_and_validate_messages(config, type_registry)
        print()

        # Step 6: Generate MessageID enums
        print("[Step 6/10] Generating MessageID enums...")
        generate_messageid_enums(messages, allocations, type_registry, config)
        print()

        # Step 7: Generate structs
        print("[Step 7/10] Generating structs...")
        generate_structs(messages, allocations, type_registry, config)
        print()

        # Step 8: Generate MessageStructure (umbrella header)
        print("[Step 8/10] Generating MessageStructure...")
        generate_message_structure_files(messages, config)
        print()

        # Step 9: Generate ProtocolConstants
        print("[Step 9/10] Generating ProtocolConstants...")
        generate_constants(type_registry, config)
        print()

        # Step 10: Print summary
        print("[Step 10/10] Finalizing...")
        print_summary(messages, type_registry, config)
        print()

        print("✅ Protocol generation complete!")
        print()

    except Exception as e:
        print(f"\n❌ Error during generation: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


# ============================================================================
# ENTRY POINT
# ============================================================================

def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print("Usage: python generate_sysex_protocol.py <plugin_name>")
        print("Example: python generate_sysex_protocol.py bitwig")
        sys.exit(1)

    plugin_name = sys.argv[1]
    generate_plugin_protocol(plugin_name)


if __name__ == "__main__":
    main()
