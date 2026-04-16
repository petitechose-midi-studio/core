# Architecture Review Rules

These rules exist to keep `core` readable and maintainable as the codebase grows.

## Review Priorities

- preserve explicit domain handlers
- keep `CoreState` as data plus simple invariants
- keep UI as a projection of state
- factorize mechanics only when behavior is genuinely identical
- protect downstream consumers of exported headers

## Reject These Changes

- a generic handler that replaces multiple domain handlers
- a helper that writes across unrelated `CoreState` branches
- a utility that hides real overlay or business-state transitions
- UI code that mutates state for convenience
- silent breaking changes to exported `ms-core` headers

## Accept These Changes

- navigation helpers such as wrapped index and turn-step helpers
- small modal helpers when they only handle mechanical selection flow
- runtime services for timeouts, pulses, or transient feedback
- view-model builders that prepare render state outside the view
- RAII feature modules that shrink the composition root

## Review Questions

- does this abstraction remove duplication without hiding domain meaning?
- can a reader still see where the business transition really happens?
- does the change reduce a hotspot, or just move complexity behind a generic API?
- does the UI remain read-only with respect to state transitions?
- does this break a downstream repo that includes `ms-core` headers?

## Required Checks

- run `pio run -e dev`
- run `pwsh ./script/dev/check-downstream-compat.ps1` when exported headers move or change
- update [CORE_ARCHITECTURE_AUDIT_2026_04.md](CORE_ARCHITECTURE_AUDIT_2026_04.md) when architecture work changes status
