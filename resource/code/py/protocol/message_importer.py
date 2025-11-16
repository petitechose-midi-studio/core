"""
Dynamic Message Importer
Loads sysex_messages.py from plugin directory at runtime.

This module provides dynamic import of plugin-specific message definitions,
allowing the generator to work with any plugin without hardcoded dependencies.

Import Strategy:
- Use importlib for dynamic module loading
- Load from plugin/[name]/sysex_protocol/sysex_messages.py
- Extract ALL_MESSAGES list
- Validate module structure

Single Responsibility: Import message definitions from plugin.
"""

from __future__ import annotations
import importlib
import sys
from pathlib import Path
from typing import TYPE_CHECKING, cast

if TYPE_CHECKING:
    from types import ModuleType

from .message import Message


def import_sysex_messages(plugin_dir: Path) -> list[Message]:
    """
    Dynamically import ALL_MESSAGES from plugin's sysex_protocol package.

    This function loads the plugin-specific message definitions by treating
    sysex_protocol as a proper Python package, enabling clean relative imports
    within the package structure.

    Args:
        plugin_dir: Path to plugin directory
                   Example: Path('plugin/bitwig')

    Returns:
        List of Message instances from ALL_MESSAGES

    Raises:
        FileNotFoundError: If sysex_protocol package not found
        AttributeError: If ALL_MESSAGES not defined in package
        TypeError: If ALL_MESSAGES is not a list
        ValueError: If ALL_MESSAGES contains non-Message objects

    Example:
        >>> from pathlib import Path
        >>> plugin_dir = Path('plugin/bitwig')
        >>> messages = import_sysex_messages(plugin_dir)
        >>> print(f"Loaded {len(messages)} messages")
        Loaded 3 messages
        >>> messages[0].name
        'TransportPlay'
    """
    sysex_protocol_dir: Path = plugin_dir / 'sysex_protocol'

    # Validate package exists
    if not sysex_protocol_dir.exists():
        raise FileNotFoundError(
            f"sysex_protocol directory not found at: {sysex_protocol_dir}\n"
            f"Expected location: plugin/[name]/sysex_protocol/"
        )

    # Validate it's a package (has __init__.py)
    init_file: Path = sysex_protocol_dir / '__init__.py'
    if not init_file.exists():
        raise FileNotFoundError(
            f"sysex_protocol is not a package (missing __init__.py)\n"
            f"Expected: {init_file}"
        )

    # Add plugin directory to sys.path temporarily to enable package import
    plugin_parent: Path = plugin_dir.parent
    sys.path.insert(0, str(plugin_parent))

    try:
        # Import the package (enables relative imports within package)
        # Example: import bitwig.sysex_protocol
        package_name: str = f"{plugin_dir.name}.sysex_protocol"

        # Import the package
        module: ModuleType = importlib.import_module(package_name)

    finally:
        # Clean up sys.path
        if str(plugin_parent) in sys.path:
            sys.path.remove(str(plugin_parent))

    # Extract ALL_MESSAGES
    if not hasattr(module, 'ALL_MESSAGES'):
        raise AttributeError(
            f"ALL_MESSAGES not defined in {package_name}\n"
            f"Expected: ALL_MESSAGES = [] at module level"
        )

    all_messages_raw: object = module.ALL_MESSAGES

    # Validate type
    if not isinstance(all_messages_raw, list):
        raise TypeError(
            f"ALL_MESSAGES must be a list, got {type(all_messages_raw).__name__}"
        )

    # After isinstance check, we know it's a list but Pyright doesn't infer the type
    # Use cast to tell type checker this is a list (runtime check already done above)
    all_messages: list[object] = cast(list[object], all_messages_raw)

    # Validate contents and build typed list
    validated_messages: list[Message] = []
    for i, item in enumerate(all_messages):
        if not isinstance(item, Message):
            raise ValueError(
                f"ALL_MESSAGES[{i}] is not a Message instance: "
                f"got {type(item).__name__}\n"
                f"All items in ALL_MESSAGES must be Message instances"
            )
        validated_messages.append(item)

    return validated_messages


def validate_message_file_structure(plugin_dir: Path) -> bool:
    """
    Validate that sysex_messages.py has correct structure without importing.

    Performs basic checks:
    - File exists
    - File is readable
    - Contains "ALL_MESSAGES" string (basic check)

    Args:
        plugin_dir: Path to plugin directory

    Returns:
        True if structure looks valid, False otherwise

    Note:
        This is a lightweight check. Full validation happens during import.
    """
    messages_file = plugin_dir / 'sysex_protocol' / 'sysex_messages.py'

    if not messages_file.exists():
        return False

    try:
        content = messages_file.read_text(encoding='utf-8')
        return 'ALL_MESSAGES' in content
    except (IOError, UnicodeDecodeError):
        return False
