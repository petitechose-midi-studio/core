# Controller interaction grammar

This is the effective controller-wide interaction contract. Historical product
rationale remains in the ADRs; `INPUT_BINDINGS.md` owns physical routing and
scope authority. This document owns the stable relationship between a musical
intention and the gesture used to express it.

The goal is transfer, not literal uniformity:

- the same intention uses the same gesture family across surfaces;
- one gesture does not silently change intention family;
- a justified exception is scoped, visible and explicitly tested;
- domain actions keep their precise musical names and map to one stable
  controller intention for cross-surface validation.

## Canonical gesture families

| Gesture | Canonical intention |
|---|---|
| `NAV` turn | Move the visible focus on the primary axis |
| short `NAV` | Activate the focused item or enter its detail |
| held `NAV` + turn on a hierarchical surface | Change Track/Pattern/Step or equivalent scope |
| held `NAV` without turn on a selectable hierarchy | Enter selection at the focused scope |
| `OPT` turn | Edit or preview exactly the focused value |
| `LEFT_TOP` | Back or cancel one level; at a safe root, open the View Selector |
| `LEFT_CENTER` + `NAV` | Navigate a visible structural, timing or target axis |
| `LEFT_BOTTOM` | Open or manipulate the deeper musical/content dimension |
| `BOTTOM_LEFT` tap / hold | Soft or reset action / guarded structural deletion |
| `BOTTOM_CENTER` | Transport everywhere |
| `BOTTOM_RIGHT` tap / hold | Safe primary or Copy action / guarded Apply, overwrite or Paste |

The action strip must expose every contextual bottom action. An unavailable or
hidden action has no invisible fallback.

## Surface archetypes

### Hierarchical surface

Track, Pattern and Step overview surfaces use `NAV` to move, short `NAV` to
activate, held-and-turned `NAV` to change scope and an immobile hold to enter
selection. A feature-specific editor must not steal the immobile hold.

### Retained editor

Track, Pattern, Step, Lane and Macro editors retain their target while the
musician works:

- `NAV` moves between fields;
- `OPT` edits the focused field;
- short `NAV` activates an explicit child or row action;
- `LEFT_CENTER + NAV` navigates the visible secondary axis when one exists;
- `LEFT_TOP` returns or cancels one level;
- bottom actions remain visible and domain-specific.

Pattern windows are a timing axis, Track/Step/Lane changes are target axes.
They share the secondary-axis intention without pretending to be the same
domain action.

### Momentary selector

A held contextual button opens the selector, `NAV` browses, `OPT` previews an
editable choice, release applies and `LEFT_TOP` cancels. Opening never mutates
the authored state by itself.

### Transactional editor

Edits operate on a visible draft. `BOTTOM_RIGHT` applies, `LEFT_TOP` cancels,
and a destructive action uses a guarded `BOTTOM_LEFT` hold. A dirty draft must
not silently retarget another object.

The Drum Lane Editor is the reference retained transactional editor. `NAV`
moves between its fields, `OPT` edits the draft, short `NAV` enters text editing
on the Name field, and `LEFT_CENTER + NAV` retargets another lane only while the
draft is clean. Apply and Delete each publish one domain-history transaction;
Cancel publishes nothing.

### Performance surface

Direct Macro and Step controls prioritize immediate musical authoring. Their
special gestures remain visible and must not leak into retained editors or
hierarchical navigation.

### Browser and text entry

Browsers use `NAV` for browse/enter and `LEFT_TOP` for hierarchical Back. Text
entry keeps normal navigation but may use explicit editing controls described
below.

## Transaction modes

| Mode | User-observable contract |
|---|---|
| Live | The focused value is authored immediately and coalesced per gesture; no Apply action is shown |
| Momentary | A temporary preview is applied on release and cancelled by Back |
| Draft | The stable state is unchanged until visible Apply; Back cancels |
| Guarded | Hold progress and preflight identify the exact mutation before commit |

A surface may contain a clearly delimited child mode with another transaction
mode. For example Pattern fields are live while Randomize is a draft. The mode
change must be visible and must not change the meaning of Back or Apply.

The Track Editor follows the same rule: Channel and Delay are live/coalesced,
while Type is an explicit draft. A dirty Type draft retains its opening Track
until Apply or Cancel; target navigation cannot silently discard it.

## Accepted scoped exceptions

| Surface | Exception | Reason |
|---|---|---|
| View Selector | `LEFT_CENTER` Undo, `LEFT_BOTTOM` Redo | One explicit global history surface; action names remain visible |
| Macro Performance | `LEFT_CENTER` capture, `LEFT_BOTTOM` Edit prompt | Direct performance gestures with visible temporary overlays |
| Project name keyboard | held `LEFT_CENTER` Shift, `LEFT_BOTTOM` Clear | Isolated text-entry archetype |
| Preset Library | `LEFT_CENTER` consumed no-op | Strict routing prevents leakage to the parent editor |

Adding an exception requires updating this table and the focused interaction
contract test. Absence of an available binding is not an exception.

## Executable guardrail

`state/interaction/ControllerInteractionContract.hpp` contains only fixed-size
enums and `constexpr` mechanical validation. Domain policies expose a mapping
from their precise actions to `ControllerIntent`; handlers keep executing the
domain action exactly as before.

`test_ControllerInteractionContract` exercises representative hierarchical,
retained-editor, momentary-selector, performance and selection policies. It is
the fast per-change gate. Focused handler and UX tests remain responsible for
domain effects and presentation; broad UX replay and firmware/hardware proof
belong at lot closure rather than every micro-edit.

New surfaces must either reuse an existing policy/archetype or add one focused
contract case. They must not introduce a parallel generic interaction engine.
