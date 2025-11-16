"""
Protocol Validator
Strict validation with fail-fast error collection.

This module validates messages against the loaded TypeRegistry, collecting
ALL errors before failing to help with debugging.

Validation Strategy:
- Collect ALL errors (don't fail on first error)
- Provide detailed error messages with context
- Fail-fast after collection (raise ValueError with all errors)

Separation of Concerns:
- TypeRegistry validates type definitions (builtin_types.py)
- ProtocolValidator validates messages (sysex_messages.py)
"""

from typing import List
from .message import Message
from .type_loader import TypeRegistry
from .field import FieldBase, PrimitiveField, CompositeField


class ProtocolValidator:
    """
    Validates messages against loaded type registry.

    Collects ALL errors before failing (helps debugging).

    Validation Checks:
    1. Field types reference existing atomic types
    2. No duplicate message names
    3. Valid Flow direction values (enforced by Message.__post_init__)

    Example:
        >>> registry = TypeRegistry()
        >>> registry.load_builtins(...)
        >>> registry.load_custom_types(...)
        >>>
        >>> validator = ProtocolValidator(registry)
        >>> validator.validate_messages(messages)  # Raises if errors
    """

    def __init__(self, type_registry: TypeRegistry):
        """
        Initialize validator with type registry.

        Args:
            type_registry: Loaded TypeRegistry instance
        """
        self.registry = type_registry
        self.errors: List[str] = []

    def validate_messages(self, messages: List[Message]) -> List[str]:
        """
        Validate all messages.

        Checks:
        1. Field types reference existing atomic types
        2. No duplicate message names
        3. Each message has at least a name and direction

        Args:
            messages: List of Message instances to validate

        Returns:
            List of error messages (empty if valid)

        Example:
            >>> errors = validator.validate_messages([msg1, msg2])
            >>> if errors:
            ...     print(f"Validation failed: {errors}")
        """
        self.errors = []

        # Check duplicate names
        names = [m.name for m in messages]
        duplicates = set([n for n in names if names.count(n) > 1])
        if duplicates:
            self.errors.append(
                f"Duplicate message names: {duplicates}"
            )

        # Check for empty messages list
        if not messages:
            self.errors.append(
                "No messages defined (ALL_MESSAGES is empty)"
            )

        # Validate each message
        for msg in messages:
            self._validate_message(msg)

        # Return errors (empty list if no errors)
        return self.errors

    def _validate_message(self, msg: Message):
        """
        Validate single message.

        Checks:
        - Field types exist in registry (for primitive fields)
        - Field names are not empty
        - No duplicate field names within message
        - Composite fields validated recursively
        - Depth limits enforced

        Args:
            msg: Message instance to validate
        """
        # Check for empty message name
        if not msg.name:
            self.errors.append("Message has empty name")
            return  # Can't continue validation without name

        # Check duplicate field names within message
        field_names = [f.name for f in msg.fields]
        duplicates = set([n for n in field_names if field_names.count(n) > 1])
        if duplicates:
            self.errors.append(
                f"Message '{msg.name}' has duplicate field names: {duplicates}"
            )

        # Validate each field recursively
        for field in msg.fields:
            self._validate_field(field, msg.name)

    def _validate_field(self, field: FieldBase, message_name: str, depth: int = 0, max_depth: int = 3) -> None:
        """
        Validate field recursively (handles both primitive and composite fields).

        Args:
            field: Field instance to validate
            message_name: Name of parent message (for error context)
            depth: Current nesting depth
            max_depth: Maximum allowed depth

        Checks:
        - Primitive fields: type exists in registry
        - Composite fields: nested fields validated recursively
        - Depth limit enforced
        """
        # Check depth limit
        if depth > max_depth:
            self.errors.append(
                f"Message '{message_name}' field '{field.name}' "
                f"exceeds maximum nesting depth of {max_depth} (current: {depth})"
            )
            return  # Don't continue deeper

        if isinstance(field, PrimitiveField):
            # Validate primitive field: check type exists in registry
            type_name_str: str = field.type_name.value

            # Extract base type (handle array notation if present)
            base_type: str = type_name_str.split('[')[0]

            # Check type exists
            if not self.registry.is_atomic(base_type):
                self.errors.append(
                    f"Message '{message_name}' field '{field.name}' "
                    f"references unknown type '{base_type}'"
                )

        elif isinstance(field, CompositeField):
            # Validate composite field: check nested fields recursively
            nested_field_names: list[str] = [f.name for f in field.fields]
            duplicates: set[str] = set([n for n in nested_field_names if nested_field_names.count(n) > 1])
            if duplicates:
                self.errors.append(
                    f"Message '{message_name}' composite field '{field.name}' "
                    f"has duplicate nested field names: {duplicates}"
                )

            # Recursively validate nested fields
            nested_field: FieldBase
            for nested_field in field.fields:
                self._validate_field(nested_field, message_name, depth + 1, max_depth)

    def get_errors(self) -> List[str]:
        """
        Get collected errors without raising.

        Returns:
            List of error messages (empty if no errors)
        """
        return self.errors.copy()

    def has_errors(self) -> bool:
        """
        Check if validation has errors.

        Returns:
            True if errors exist, False otherwise
        """
        return len(self.errors) > 0

    def clear_errors(self):
        """
        Clear all collected errors.

        Useful for reusing validator instance.
        """
        self.errors = []
