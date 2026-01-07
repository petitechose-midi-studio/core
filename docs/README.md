# MIDI Studio Core Documentation

> **Last updated**: January 2026
> **Audience**: Developers contributing to MIDI Studio

---

## Quick Start

| I want to... | Read this |
|--------------|-----------|
| Understand the architecture | [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md) |
| Know the coding conventions | [CODE_STYLE.md](CODE_STYLE.md) |
| Add a new feature | [EXTENSION_CHECKLIST.md](EXTENSION_CHECKLIST.md) |
| Understand the rules | [INVARIANTS.md](INVARIANTS.md) |

---

## Documentation Structure

### Reference Documents

| Document | Description |
|----------|-------------|
| [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md) | System-level architecture, patterns, and recommendations |
| [CODE_STYLE.md](CODE_STYLE.md) | Naming conventions, formatting, tooling |
| [INVARIANTS.md](INVARIANTS.md) | Non-negotiable architectural rules |
| [EXTENSION_CHECKLIST.md](EXTENSION_CHECKLIST.md) | Checklist before adding major features |

### Step-by-Step Tutorials

Read in this order for best understanding:

| # | Tutorial | What You'll Learn |
|---|----------|-------------------|
| 1 | [STATE_MANAGEMENT.md](STATE_MANAGEMENT.md) | Signals, subscriptions, reactive state |
| 2 | [HOW_TO_ADD_WIDGET.md](HOW_TO_ADD_WIDGET.md) | Creating LVGL widgets |
| 3 | [HOW_TO_ADD_HANDLER.md](HOW_TO_ADD_HANDLER.md) | Input bindings, button/encoder handling |
| 4 | [HOW_TO_ADD_VIEW.md](HOW_TO_ADD_VIEW.md) | Full-screen views with lifecycle |
| 5 | [HOW_TO_ADD_OVERLAY.md](HOW_TO_ADD_OVERLAY.md) | Modal overlays with two-level scoping |

---

## Key Patterns

### Data Flow

```
User Input → Handler → State (Signals) → View (LVGL)
```

### Component Relationships

```
Context
├── State (CoreState)
├── Handlers (InputHandler)
├── Views (IView)
│   └── Widgets (IWidget)
└── Overlays (via OverlayController)
```

### Golden Rules

1. **Handlers never touch LVGL** - Only update state
2. **Views never send protocol** - Only observe state
3. **State is the source of truth** - UI is a projection
4. **Use scoped bindings** - For input authority
5. **Clean up in destructors** - RAII everywhere

---

## File Organization

```
midi-studio/core/
├── docs/                    # This documentation
├── src/
│   ├── config/              # Configuration constants
│   ├── context/             # Application contexts
│   ├── handler/input/       # Input handlers
│   ├── state/               # Reactive state (Signals)
│   └── ui/
│       ├── component/       # Reusable components
│       ├── macro/           # Macro-specific UI
│       ├── topbar/          # Top bar components
│       ├── transportbar/    # Transport bar
│       ├── view/            # Full-screen views
│       └── widget/          # Widgets
└── platformio.ini           # Build configuration
```

---

## Contributing

1. Read [INVARIANTS.md](INVARIANTS.md) before writing code
2. Follow [CODE_STYLE.md](CODE_STYLE.md) conventions
3. Use the appropriate tutorial for your task
4. Complete [EXTENSION_CHECKLIST.md](EXTENSION_CHECKLIST.md) before merge

---

## Serena Memories

Related memories in `.serena/memories/`:

| Memory | Content |
|--------|---------|
| `code-style` | Unified style guide (Core + Bitwig) |
| `project-paths` | File paths and structure |
| `changelog` | Version history |
| `alignment-plan-core-bitwig` | Refactoring plan |
