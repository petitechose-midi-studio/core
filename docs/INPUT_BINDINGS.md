# Input binding contract

This is the current code-local contract for physical controls. Product
rationale lives in ADR-0055 in the documentation vault; framework mechanics
live in `open-control/framework/INPUT_ROUTING_POLICY.md`.

## Routing boundary

Core enables the strict framework combination:

```cpp
ReleaseRoutingPolicy::OwnerOnly
GestureRoutingPolicy::PressScoped
BindingAmbiguityPolicy::FailClosed
GlobalRoutingPolicy::ExplicitPassThroughOnly
```

Physical button authority is captured before Press dispatch. The synchronous
Press callback may establish an inline mode inside that same authority; its
remaining release, long-press, double-tap, and combo bindings are then frozen.
A later view, overlay, mode, or predicate transition cannot retarget them.
Missing or ambiguous contextual bindings are consumed. Overlay authority
changes quarantine held gestures unless a handler deliberately calls
`handoffPress()`. The framework observes the visibility transition itself, so
this remains true for the few compatibility paths that still mutate the
underlying overlay stack directly.

Encoder turns remain instantaneous and resolve against current authority on
each event. Do not apply button pass-through assumptions to `NAV` or `OPT`
turns.

## Reserved controls

| Control | Scope | Contract |
|---|---|---|
| `BOTTOM_CENTER` release | global pass-through | Play/Stop Transport everywhere, including overlays |
| `LEFT_TOP` at a safe view root | current view, then explicit handoff | short persistent or held momentary View Selector |
| `LEFT_TOP` below a root | current scope | Back, Cancel, Close, or clear the current selection |
| `NAV` turn | current scope | move focus |
| `OPT` turn | current scope | edit or preview the focused value |

`BOTTOM_CENTER` must never receive another contextual binding. If a surface
needs a third action, assign another visible control instead of shadowing
Transport.

Transport Lock may block starting while stopped. It must not block Stop while
already playing.

## Shared view grammar

| Gesture | Meaning |
|---|---|
| short `LEFT_TOP` at a safe root | open and latch the View Selector; a second `LEFT_TOP` or `NAV` applies/closes |
| held `LEFT_TOP` at a safe root | keep the View Selector visible while held; `NAV` turns preview and release applies |
| `LEFT_TOP` below the root | cancel/back exactly one local level |
| first `LEFT_TOP` during selection or paste placement | clear the local selection/placement; it does not open the View Selector |
| hold `NAV` and turn before the timeout | preview Track/Page/Macro or Track/Pattern/Step context; release applies |
| hold `NAV` without turning through the timeout | enter selection for the currently focused context |
| short `NAV` | activate the focused item according to the visible surface |

Track selection is project-wide: copying or pasting one Track includes its
Sequencer, Macro, and Modulator content regardless of whether the operation was
started from Macro or Sequencer. Macro pages remain compact; sparse Track slots
remain valid.

## Collision-free contextual bindings

| Surface | Bindings |
|---|---|
| View Selector | `NAV` select/confirm, `LEFT_CENTER` Undo, `LEFT_BOTTOM` Redo, `LEFT_TOP` apply/close |
| Track Editor | `BOTTOM_LEFT` Mute, `BOTTOM_RIGHT` Solo |
| Track Paste preflight | `LEFT_CENTER` Summary/Details, `BOTTOM_RIGHT` Copy/Paste and guard |
| Step Editor | short `NAV` focused-row action; long `NAV` Step Preset |
| Step Preset | `NAV` asset/detail, `OPT` preview state, `BOTTOM_LEFT` Load/Save, `BOTTOM_RIGHT` action/guard, `LEFT_TOP` or `LEFT_CENTER` close |
| Project name keyboard | `NAV` key/insert including `SPC`, `OPT` row, held `LEFT_CENTER` Shift, `BOTTOM_LEFT` Backspace, `LEFT_BOTTOM` Clear, `LEFT_TOP` Cancel, `BOTTOM_RIGHT` Validate |
| CC Lane | contextual settings use `BOTTOM_RIGHT`; Transport remains `BOTTOM_CENTER` |

`BOTTOM_LEFT` and `BOTTOM_RIGHT` are contextual action slots. Their icon, tone,
availability, and hold guard must be projected by the visible action strip.
There is no hidden fallback for an action absent from that strip.

## Handoff rule

Use `handoffPress(button, targetScope)` only when the same physical gesture
must intentionally continue in the scope it opens. The View Selector's held
`LEFT_TOP` gesture is the canonical case.

Opening an overlay on release normally needs no handoff: the release belongs to
the old scope, the authority transition then quarantines any remaining held
buttons.

New code must mutate overlays through `OverlayManager`. Direct access to the
underlying visibility stack is observation/compatibility only; it is covered by
the quarantine invariant but must not become a second overlay API.

## Required checks

- framework `test_inputbinding` and `test_overlaymanager`;
- Core ViewSwitcher, Transport, Project keyboard, Track Editor/Paste, Step
  Editor/Preset, and CC Lane tests;
- SDL workflows for overlay exclusivity, view-selector roundtrip, Project name
  keyboard, Step Preset, Track Editor, Track Paste, and Transport pass-through
  while an overlay remains active;
- `pio run -e dev` to preserve the firmware memory gates.
