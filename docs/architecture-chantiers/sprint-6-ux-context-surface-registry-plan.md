# Sprint 6: Native UX Context Surface Registry Plan

## Status

Tracking document.

This document is the cold reference for the Sprint 6 refactor. If the execution
context is lost, resume from this file before touching code.

Last updated: 2026-05-01

Current execution state:

| Step | Status | Notes |
| --- | --- | --- |
| Phase 0: tracking reference | done | This file is now the execution and resume reference. |
| Phase 1: registry primitive | done | Added the fixed-capacity surface registry and focused tests. |
| Phase 2: extract current logic | done | Current semantic extraction moved out of `StandaloneContext` into standalone UX surfaces. |
| Phase 3: feature-owned surfaces | done | Existing settings, sequencer, and transport surfaces are owned by their modules/assembly. |
| Phase 4: missing semantic surfaces | done | View selector, macro value, sequencer structure navigation, quick controls, macro edit, macro selectors, and data manager are wired through existing core state/presenter sources. |
| Phase 5: manager transport cleanup | done | Current semantic fields, shared track state, coalesced metrics, outcome/reason, target masks, and compact presentation labels are transported without legacy event-schema compatibility paths. |
| Phase 6: structure action truthfulness | done | Structure releases consumed after long-press are now traced as `ignored`, add-slot copy/erase and unavailable remove arms are `noop`, and page/track target masks are emitted by core for reconstruction. |

Resume protocol:

1. Read this file.
2. Check the execution state table.
3. Run `git status --short` and preserve unrelated user changes.
4. Continue with the first `pending` phase.
5. Update this table after each completed phase and record validation commands.

Hard constraints:

- no legacy compatibility path;
- no duplicated business labels for logging;
- no feature-specific UX branches in `StandaloneContext`;
- no semantic inference in `ms-manager`;
- all firmware UX tracing remains explicitly gated by `MS_UX_RECORDER`.

## Problem Statement

The current UX recorder pipeline works, but the semantic context extraction is
not yet shaped correctly for long-term maintenance.

Current issue:

- `SemanticUxRecorder` is correctly positioned at the `InputBinding` dispatch
  trace layer.
- `ms-manager` correctly archives and presents the structured fields emitted by
  core.
- `StandaloneContext` currently contains feature-specific UX semantic extraction.
  This makes it a central route for unrelated feature knowledge.

This is not the intended ownership model. The active feature or surface should
provide its own UX context from data it already owns or projects for rendering.
The recorder should only sample and serialize that context.

## Objective

Build a native, opt-in UX context extraction architecture where:

- semantic UX fields are provided by core;
- business labels and values come from existing state, presenters, formatters, or
  view-model builders;
- adding a feature does not require editing `SemanticUxRecorder`;
- adding a feature should not require adding feature-specific branches to
  `StandaloneContext`;
- `ms-manager` remains a transport and presentation layer, not a semantic
  inference layer;
- production firmware remains unaffected unless `MS_UX_RECORDER` is explicitly
  enabled.

## Non-Goals

- Do not build a generic runtime reflection system.
- Do not trace every raw physical input tick.
- Do not infer business meaning in `ms-manager`.
- Do not duplicate UI label tables for tracing.
- Do not keep compatibility shims for previous draft NDJSON schemas.
- Do not turn feature handlers into logging objects.

## Current Evidence

Existing usable sources of UX context:

- Global settings overlay:
  - `context/standalone/GlobalSettingsOverlayPresenterFormatters.hpp`
  - `buildOverlayRenderData`
  - `buildSelectorRenderData`
- Macro edit and macro selectors:
  - `context/standalone/MacroOverlayPresenterFormatters.hpp`
  - `buildEditRenderData`
  - `buildEditSelectorRenderData`
  - `buildPageSelectorRenderData`
  - `buildTargetSelectorRenderData`
- Data Manager:
  - `context/standalone/DataManagerPresenterFormatters.hpp`
  - `buildOverlayRenderData`
  - `buildDialogRenderData`
- Macro live view:
  - `ui/view/MacroViewModelBuilder.hpp`
  - `buildMacroViewFrameState`
  - `buildMacroPropertyStripProps`
  - `buildMacroBottomControlsProps`
- Sequencer live view:
  - `ui/sequencer/SequencerViewModelBuilder.hpp`
  - `buildStepGridProps`
  - `buildStepPropertyStripProps`
  - `buildBottomControlsProps`
  - `buildHeaderBarProps`
- Step value formatting:
  - `state/sequencer/StepPropertyDisplay.hpp`
  - `stepPropertyName`
  - `formatStepPropertyValue`
- Stable input mapping:
  - `config/InputIDs.hpp`
  - `MACRO_ENCODERS`
  - `MACRO_BUTTONS`
  - `macroEncoderIndex`
  - `macroButtonIndex`

Current problematic location:

- `context/StandaloneContext.cpp`
  - `captureSemanticUxContext` contains feature-specific branches for global
    settings, transport, sequencer property selection, and step grid editing.

## Target Architecture

The target architecture is a small registry of UX surfaces.

```text
InputBindingTraceEvent
        |
        v
SemanticUxRecorder
        |
        v
SemanticUxContextProvider
        |
        v
SemanticUxSurfaceRegistry
        |
        +-- ViewSelectorUxSurface
        +-- TransportUxSurface
        +-- MacroUxSurface
        +-- MacroOverlayUxSurface
        +-- SequencerUxSurface
        +-- SequencerOverlayUxSurface
        +-- SettingsUxSurface
        +-- DataManagerUxSurface
```

The recorder does not know which feature is active. It asks the current provider
for the pre-dispatch and post-dispatch semantic context.

The registry chooses the first surface that can describe the current context for
the event.

Priority order:

1. Active modal or overlay surfaces.
2. Active top-level view surfaces.
3. Global surfaces such as transport.

## Proposed Core Interfaces

### `SemanticUxContext`

Current fields are acceptable as the first stable contract:

```cpp
struct SemanticUxContext {
    const char* mode = nullptr;
    const char* effect = nullptr;
    const char* outcome = nullptr;
    const char* reason = nullptr;
    const char* target = nullptr;
    int16_t targetIndex = -1;
    int16_t targetStep = -1;
    int32_t targetMask = -1;
    const char* property = nullptr;
    char valueLabel[16] = {};
    bool hasStepOn = false;
    bool stepOn = false;
};
```

`outcome` and `reason` are intentionally sparse. They are emitted only when the
binding dispatch alone would be misleading, for example `ignored` after a
long-press consumed the matching release or `noop` for an add-slot action that
cannot mutate state.

### `SemanticUxSurface`

```cpp
class SemanticUxSurface {
public:
    virtual ~SemanticUxSurface() = default;

    virtual bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        SemanticUxContext& out
    ) const = 0;
};
```

### `SemanticUxSurfaceRegistry`

Fixed-capacity registry, no dynamic allocation required:

```cpp
class SemanticUxSurfaceRegistry : public SemanticUxContextProvider {
public:
    bool add(const SemanticUxSurface& surface);
    void clear();

    void captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        SemanticUxContext& out
    ) const override;

private:
    static constexpr uint8_t CAPACITY = 16;
    const SemanticUxSurface* surfaces_[CAPACITY] = {};
    uint8_t count_ = 0;
};
```

Registry behavior:

- Iterate in registration order.
- Stop on first surface returning `true`.
- Leave `out` empty when no surface matches.
- No ownership of surfaces.

## Ownership Model

`StandaloneContext` owns the registry under `MS_UX_RECORDER`.

Feature modules own their own surfaces under `MS_UX_RECORDER`.

Example:

```text
StandaloneContext
  SemanticUxSurfaceRegistry
  StandaloneGlobalHandlerAssembly
    ViewSelectorUxSurface
    TransportUxSurface
  StandaloneFeatureAssembly
    MacroFeatureModule
      MacroUxSurface
      MacroOverlayUxSurface
    SequencerFeatureModule
      SequencerUxSurface
      SequencerOverlayUxSurface
    SettingsFeatureModule
      GlobalSettingsUxSurface
      DataManagerUxSurface
```

This avoids lifetime ambiguity:

- registry is created before feature modules;
- feature modules register surfaces during construction;
- registry is cleared during context cleanup before feature modules are reset.

## Implementation Roadmap

### Phase 0: Tracking Reference

Files:

- update `docs/architecture-chantiers/sprint-6-ux-context-surface-registry-plan.md`

Actions:

- Keep this document untracked until the refactor shape is validated.
- Maintain the execution state table above.
- Append validation evidence after each phase.

Exit condition:

- This file is sufficient to resume the refactor without chat context.

### Phase 1: Introduce Registry Without Behavior Change

Files:

- add `src/validation/ux/SemanticUxSurface.hpp`
- add `src/validation/ux/SemanticUxSurface.cpp`
- update `src/validation/ux/SemanticUxContext.hpp` only if needed
- update tests under `test/test_SemanticUxRecorder` or add
  `test/test_SemanticUxSurfaceRegistry`

Actions:

- Implement fixed-capacity registry.
- Add unit test for:
  - first matching surface wins;
  - no match leaves context empty;
  - capacity handling is deterministic.
- Do not move feature logic yet.

Validation:

```powershell
ms test core
```

### Phase 2: Move Current `StandaloneContext` Logic Into Surfaces

Files:

- add `src/context/standalone/ux/StandaloneUxSurfaces.hpp`
- add `src/context/standalone/ux/StandaloneUxSurfaces.cpp`
- update `src/context/StandaloneContext.hpp`
- update `src/context/StandaloneContext.cpp`

Initial surfaces:

- `TransportUxSurface`
- `GlobalSettingsUxSurface`
- `SequencerStepGridUxSurface`
- `SequencerPropertySelectorUxSurface`

Actions:

- Move existing logic out of `StandaloneContext::captureSemanticUxContext`.
- Keep behavior equivalent.
- `StandaloneContext` becomes owner of `SemanticUxSurfaceRegistry`.
- `StandaloneContext` registers only surfaces it owns directly, or passes the
  registry to child assemblies for feature-owned surfaces.

Exit condition:

- `StandaloneContext.cpp` no longer contains feature-specific UX semantic
  branches.
- `StandaloneContext` only registers or clears the provider.

Validation:

```powershell
ms test core
ms build core --target teensy --env dev_ux_recorder
```

### Phase 3: Register Feature-Owned Surfaces In Feature Modules

Files:

- update `src/context/standalone/StandaloneFeatureAssembly.hpp`
- update `src/context/standalone/StandaloneFeatureAssembly.cpp`
- update `src/context/standalone/MacroFeatureModule.hpp`
- update `src/context/standalone/MacroFeatureModule.cpp`
- update `src/context/standalone/SequencerFeatureModule.hpp`
- update `src/context/standalone/SequencerFeatureModule.cpp`
- update `src/context/standalone/SettingsFeatureModule.hpp`
- update `src/context/standalone/SettingsFeatureModule.cpp`

Actions:

- Pass an optional `SemanticUxSurfaceRegistry*` under `MS_UX_RECORDER`.
- Each feature module registers its own surfaces.
- Keep all declarations behind `#if defined(MS_UX_RECORDER)`.
- Avoid including UX headers in production-only paths when possible.

Initial ownership:

- `SettingsFeatureModule`
  - owns `GlobalSettingsUxSurface`
  - owns `DataManagerUxSurface`
- `SequencerFeatureModule`
  - owns `SequencerStepGridUxSurface`
  - owns `SequencerPropertySelectorUxSurface`
  - later owns quick-control/page-navigation surfaces
- `MacroFeatureModule`
  - owns `MacroValueUxSurface`
  - owns macro edit/page/target selector surfaces
- `StandaloneGlobalHandlerAssembly`
  - owns `ViewSelectorUxSurface`
  - owns `TransportUxSurface`

Validation:

```powershell
ms test core
ms build core --target teensy --env dev_ux_recorder
```

### Phase 4: Add Missing UX Surfaces From Existing Data Sources

Priority order:

1. `ViewSelectorUxSurface`
   - source: `ViewSelectorState`
   - fields:
     - `mode=view_selector`
     - `target=view`
     - `property=<selected view>`
     - `effect=select_view` or `apply_view`
2. `MacroValueUxSurface`
   - source: `MacroViewModelBuilder` or macro state refs
   - fields:
     - `mode=macro`
     - `target=macro`
     - `target_index=<macro index>`
     - `property=Value`
     - `value_label=<normalized value>`
     - `effect=edit_macro_value`
3. `SequencerPageNavigationUxSurface`
   - source: `SequencerViewModelBuilder` / `SequencerState`
   - fields:
     - `mode=sequencer.page`
     - `target=page`
     - `value_label=<page index>`
     - `effect=select_page`
4. `SequencerQuickControlsUxSurface`
   - source: `SequencerBottomControlsProps`
   - fields:
     - `mode=sequencer.quick_controls`
     - `target=quick_control`
     - `property=<Offset|Division|Length>`
     - `value_label=<formatted current value>`
5. `MacroOverlayUxSurface`
   - source: `MacroOverlayPresenterFormatters`
   - fields:
     - edit row/value selector/page selector/target selector
6. `DataManagerUxSurface`
   - source: `DataManagerPresenterFormatters`
   - fields:
     - command, dialog item, slot, confirmation choice

Validation with real traces:

- Flash `dev_ux_recorder`.
- Record a hardware session.
- Confirm new fields appear for:
  - view switching;
  - macro knob edits;
  - sequencer page navigation;
  - global settings;
  - data manager if exercised.

### Phase 5: Simplify `ms-manager` Field Transport

Current manager copies a fixed allowlist of payload fields.

Problem:

- Adding one more core semantic field requires editing
  `copy_payload_fields`.

Preferred minimal change:

- Keep known operational fields explicit.
- Copy all primitive semantic fields under a controlled prefix or allowlist
  family.

Option A:

```json
"ux": {
  "mode": "...",
  "effect": "...",
  "target": "...",
  "property": "...",
  "value_label": "..."
}
```

Option B:

Keep flat fields but copy all known primitive keys listed in one shared local
constant.

Recommendation:

- Do not change NDJSON shape immediately.
- First stabilize core registry.
- Then decide whether schema 6 should move semantic fields under `ux`.

Validation:

```powershell
cargo test ux_recorder --no-run
npm run check
```

Known Windows limitation:

- `cargo test ux_recorder` currently fails at runtime with
  `STATUS_ENTRYPOINT_NOT_FOUND`.
- `--no-run` is the reliable local validation for Rust test compilation until
  that environment issue is resolved.

## Cleanup Requirements

Before considering the refactor complete:

- remove `StandaloneContext::captureSemanticUxContext`;
- remove feature-specific UX helper functions from `StandaloneContext.cpp`;
- ensure all new files live under either:
  - `src/validation/ux/`
  - `src/context/standalone/ux/`
  - the owning feature module directory;
- keep all UX registry wiring behind `MS_UX_RECORDER`;
- keep production builds free of UX tracing feature knowledge;
- keep `ms-manager` as archive/presentation only;
- update sprint 5 documentation to point to this plan or its final result;
- ensure no generated capture/report artifacts are accidentally committed unless
  explicitly requested.

## Expected Change Size

Core:

- 3 new validation registry files.
- 1 to 3 new UX surface files.
- small constructor signature changes in standalone assemblies/modules.
- net removal of feature-specific UX logic from `StandaloneContext.cpp`.
- expected net code increase is acceptable only if `StandaloneContext` shrinks.

Manager:

- likely no immediate change for Phase 1-4.
- optional later simplification of semantic field transport.

Tests:

- add registry unit test.
- keep recorder unit tests.
- add focused surface tests only where a surface has non-trivial selection logic.

## Risks

### Surface Lifetime

Risk:

- registry stores non-owning pointers.

Mitigation:

- registry owned by `StandaloneContext`;
- feature modules own surfaces;
- registry cleared before feature modules are destroyed;
- no dynamic allocation inside registry.

### Include Pollution

Risk:

- UX headers leak into production compile paths.

Mitigation:

- wrap surface members and constructor parameters in `#if defined(MS_UX_RECORDER)`;
- keep forward declarations where possible;
- verify normal builds after refactor.

### Duplicate Label Logic

Risk:

- surfaces recreate labels that presenters already know.

Mitigation:

- surfaces must call existing builders/formatters where they exist;
- any new formatter needed for UX should live with the existing presenter/model
  code, not in the recorder.

### Manager Semantic Drift

Risk:

- `ms-manager` starts mapping core effect names into feature meaning.

Mitigation:

- manager only formats strings for display;
- core remains owner of semantic field names and values;
- any new business meaning must be emitted by core.

## Exit Criteria

Sprint 6 is complete when:

- `StandaloneContext` no longer contains feature-specific UX context extraction;
- semantic context is supplied through a registry of feature-owned surfaces;
- existing sequencer/global settings/transport UX logs remain at least as
  informative as today;
- view selector, macro value, and sequencer page navigation have native UX
  context fields;
- `ms test core` passes;
- `ms build core --target teensy --env dev_ux_recorder` passes;
- `cargo test ux_recorder --no-run` passes;
- `npm run check` passes;
- a fresh hardware NDJSON confirms reduced inference needs for `.ux`
  reconstruction.

## Validation Log

- 2026-05-01: `ms test core` -> OK, 47/47.
- 2026-05-01: `ms build core --target teensy --env dev_ux_recorder` -> OK.
- 2026-05-01: `ms test core` after feature-owned surface wiring -> OK, 47/47.
- 2026-05-01: `ms build core --target teensy --env dev_ux_recorder` after feature-owned surface wiring -> OK.
- 2026-05-01: `ms test core` after phase 4 first tranche -> OK, 47/47.
- 2026-05-01: `ms build core --target teensy --env dev_ux_recorder` after phase 4 first tranche -> OK.
- 2026-05-01: `cargo test ux_recorder --no-run` after `target_index` manager passthrough -> OK.
- 2026-05-01: `rustfmt --check src-tauri/src/services/ux_recorder/session_store.rs src-tauri/src/services/ux_recorder/uxr_parser.rs` -> OK.
- 2026-05-01: `npm run check` in `ms-manager` -> OK.
- 2026-05-01: global `cargo fmt --check` in `ms-manager/src-tauri` still reports pre-existing formatting drift outside touched UX files.
- 2026-05-01: split standalone UX surfaces by domain to avoid a catch-all implementation file.
- 2026-05-01: `ms test core` after UX surface split -> OK, 47/47.
- 2026-05-01: `ms build core --target teensy --env dev_ux_recorder` after UX surface split -> OK.
- 2026-05-01: added macro edit, macro selector, and data manager UX surfaces using existing presenter formatter sources.
- 2026-05-01: `ms test core` after macro edit/data manager UX surfaces -> OK, 47/47.
- 2026-05-01: `ms build core --target teensy --env dev_ux_recorder` after macro edit/data manager UX surfaces -> OK.
- 2026-05-01: completed `ms-manager` presentation labels for macro edit and data manager semantic fields.
- 2026-05-01: `rustfmt --check src-tauri/src/services/ux_recorder/session_store.rs src-tauri/src/services/ux_recorder/uxr_parser.rs` -> OK.
- 2026-05-01: `cargo test ux_recorder --no-run` in `ms-manager/src-tauri` -> OK.
- 2026-05-01: `npm run check` in `ms-manager` -> OK.
- 2026-05-01: fixed manager Activity presentation for user-facing indices: step labels and page/shared deltas are displayed 1-based like the UI, while raw NDJSON indices remain unchanged for deterministic replay/reconstruction.
- 2026-05-01: `cargo test ux_recorder --no-run` in `ms-manager/src-tauri` after user-facing index presentation fix -> OK.
- 2026-05-01: extended user-facing index presentation to coalesced page/track scans, e.g. `track new 2->16 preview`, while preserving raw `first_target_index`/`last_target_index` values in NDJSON.
- 2026-05-01: `rustfmt --check src/services/ux_recorder/session_store.rs src/services/ux_recorder/uxr_parser.rs` in `ms-manager/src-tauri` after coalesced range presentation -> OK.
- 2026-05-01: `cargo test ux_recorder --no-run` in `ms-manager/src-tauri` after coalesced range presentation -> OK.
- 2026-05-01: `npm run check` in `ms-manager` after coalesced range presentation -> OK.
- 2026-05-01: hardware NDJSON `20260501-151703.264Z` exposed a core-side pre/post merge drift: `preview_structure` emitted post-dispatch `value_label`/page but pre-dispatch `target_index`, producing page/track cursor labels offset by one event.
- 2026-05-01: fixed `SemanticUxRecorder` merge priority so same-surface `target_index` and `target_step` use post-dispatch values, matching the cursor state already used by `property`/`value_label`.
- 2026-05-01: `ms test core` after same-surface target merge fix -> OK, 47/47.
- 2026-05-01: `ms build core --target teensy --env dev_ux_recorder` after same-surface target merge fix -> OK.
- 2026-05-01: added sparse `outcome`/`reason` and `target_mask` UX fields for structure truthfulness: ignored releases after long press and add-slot, single-slot, or empty-clipboard no-ops are explicit in core output.
- 2026-05-01: added opt-in `StructureUxTraceState` shared by structure handlers and UX surfaces only under `MS_UX_RECORDER`; normal firmware state remains unchanged.
- 2026-05-01: `ms test core` after structure outcome fields -> OK, 47/47.
- 2026-05-01: `ms build core --target teensy --env dev_ux_recorder` after structure outcome fields -> OK.
- 2026-05-01: `ms build core --target teensy --env dev` after structure outcome fields -> OK; confirms the extra trace state is not carried by the normal dev firmware.
- 2026-05-01: `rustfmt --check src-tauri/src/services/ux_recorder/session_store.rs src-tauri/src/services/ux_recorder/uxr_parser.rs` after outcome labels -> OK.
- 2026-05-01: `cargo test ux_recorder --no-run` in `ms-manager/src-tauri` after outcome labels -> OK.
- 2026-05-01: `npm run check` in `ms-manager` after outcome labels -> OK.
- 2026-05-01: hardware NDJSON `20260501-145908.006Z` confirmed `ignored`/`noop` outcomes and target masks are emitted; remaining naked events were macro quick controls and focused sequencer `OPT`.
- 2026-05-01: added macro quick-controls UX context and focused-step `OPT` semantics in sequencer step grid.
- 2026-05-01: manager coalescing now groups `preview_structure` target motion across target-index changes while preserving first/last target index for compact track/page scans.
- 2026-05-01: `ms test core` after quick-controls/focused OPT semantics -> OK, 47/47.
- 2026-05-01: `ms build core --target teensy --env dev_ux_recorder` after quick-controls/focused OPT semantics -> OK.
- 2026-05-01: `rustfmt --check src-tauri/src/services/ux_recorder/session_store.rs src-tauri/src/services/ux_recorder/uxr_parser.rs` after preview coalescing -> OK.
- 2026-05-01: `cargo test ux_recorder --no-run` in `ms-manager/src-tauri` after preview coalescing -> OK.
- 2026-05-01: `npm run check` in `ms-manager` after preview coalescing -> OK.
- 2026-05-01: extracted sequencer step edit overlay render data into `SequencerOverlayPresenterFormatters` so presenter and UX tracing share the same labels/values.
- 2026-05-01: added sequencer step edit UX context for open, focus row, edit value, apply, and cancel.
- 2026-05-01: `ms test core` after sequencer step edit UX surface -> OK, 47/47.
- 2026-05-01: `ms build core --target teensy --env dev_ux_recorder` after sequencer step edit UX surface -> OK.
- 2026-05-01: `rustfmt --check src-tauri/src/services/ux_recorder/session_store.rs src-tauri/src/services/ux_recorder/uxr_parser.rs` after step edit labels -> OK.
- 2026-05-01: `cargo test ux_recorder --no-run` in `ms-manager/src-tauri` after step edit labels -> OK.
- 2026-05-01: `npm run check` in `ms-manager` after step edit labels -> OK.
- 2026-05-01: `ms build core --target teensy --env dev` -> OK; confirms UX recorder code remains opt-in and production dev firmware does not carry `MS_UX_RECORDER`.
- 2026-05-01: real hardware NDJSON exposed post-dispatch context contamination on view selector apply; fixed structure surface capture and pre/post merge guard.
- 2026-05-01: `ms test core` after context contamination fix -> OK, 47/47.
- 2026-05-01: `ms build core --target teensy --env dev_ux_recorder` after context contamination fix -> OK.
- 2026-05-01: added macro structure UX context for page/track preview, focus switching, create, erase, remove, copy, paste, selection delete/duplicate.
- 2026-05-01: added macro performance clutch UX context for property selection and clutch apply, plus macro edit apply on macro-button release.
- 2026-05-01: added pre/post shared track and shared mask fields so global track mutations are archived by `ms-manager`.
- 2026-05-01: completed manager labels for structure actions, macro clutch, macro property, add-slot targets, and selection targets.
- 2026-05-01: `ms test core` after macro structure/global shared UX fields -> OK, 47/47.
- 2026-05-01: `ms build core --target teensy --env dev_ux_recorder` after macro structure/global shared UX fields -> OK.
- 2026-05-01: `rustfmt --check src-tauri/src/services/ux_recorder/session_store.rs src-tauri/src/services/ux_recorder/uxr_parser.rs` -> OK.
- 2026-05-01: `cargo test ux_recorder --no-run` in `ms-manager/src-tauri` -> OK.
- 2026-05-01: `npm run check` in `ms-manager` -> OK.
