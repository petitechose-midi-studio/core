# Core Quality Audit And Roadmap - 2026-07-08

Purpose: define the next cleanup roadmap after the June/early-July feature
wave. The goal is not cosmetic refactoring. The goal is to keep Core easy to
reason about, deterministic, memory-conscious, and free of duplicate or legacy
paths while preserving the new product features.

## Scope

- Repository: `midi-studio/core`.
- Baseline inspected: current working tree on 2026-07-08, with the recent
  sequencer left-button grammar edits still uncommitted.
- Time window reviewed: changes since commit `25cda21` from 2026-06-07.
- Main feature areas reviewed:
  - project file persistence, container, migration, and file tool;
  - nested sequencer step content, resolved display projection, clipboard,
    presets, and structure actions;
  - chord step editor and overlay readouts;
  - macro automation recording, playback, persistence, and UX;
  - SDL UX workflow expansion and native CMake test coverage.

## Evidence Snapshot

Observed churn since 2026-06-07:

- `466 files changed`
- `52260 insertions`
- `8747 deletions`

Largest current production files:

| File | Lines | Concern |
|---|---:|---|
| `src/handler/sequencer/SequencerStepEditHandler.cpp` | ~701 | Reduced, but still owns input binding and top-level step edit routing. |
| `src/handler/sequencer/SequencerStructureEditWorkflow.cpp` | ~596 | Reduced, but still mixes track current actions, track selection mute/remove, and history boundaries. |
| `src/ui/sequencer/SequencerViewModelBuilder.cpp` | ~26 | Public facade; concrete projections live in dedicated builders. |
| `src/ui/sequencer/StepGrid.cpp` | ~974 | Custom draw path is feature-rich and performance-sensitive. |
| `src/ui/sequencer/SequencerStepEditOverlay.cpp` | ~971 | Layout/render state is large and tightly coupled to step-editor semantics. |
| `src/context/standalone/ux/StandaloneSequencerUxSurfaces.cpp` | ~821 | UX semantic surface rules are becoming dense. |
| `src/context/standalone/SequencerOverlayPresenterFormatters.cpp` | ~783 | Formatter accumulation risks becoming a second UI model layer. |
| `src/persistence/SequencerPersistenceEnvelope.cpp` | ~767 | Current codec is explicit, but migrations must stay outside hot decode paths. |

Current Teensy build observation from `pio run` on 2026-07-08:

- FLASH code/data: `656852` / `168016`
- RAM1 variables/code/padding: `65376` / `313272` / `14408`
- RAM1 free for local variables: `131232`
- RAM2 variables/free malloc: `247968` / `276320`
- EXTRAM variables: `4282368`

This is acceptable today. The risk is not immediate memory exhaustion. The risk
is future hidden cost from broad copies, compaction, full-surface UI refreshes,
and large feature hubs.

## Review Rules For The Cleanup

- One behavior path per user action. Do not keep compatibility aliases,
  duplicate binding paths, or hidden fallbacks unless they are part of an
  explicit migration policy.
- Policy decides intent; handlers execute; domain/state modules mutate;
  presenters format; views render. Do not let view builders or formatters own
  business rules.
- Keep current file formats current-only in the hot path. Older data should be
  handled by explicit project migration and load reports.
- Large mutable scratch or cold feature state should stay out of RAM1 unless it
  is timing-critical.
- UI render work should be bounded by local invalidation and cached structural
  layout. Runtime value changes should not recompute full layout.
- Tests should name the behavior contract, not implementation details.

## Deep Context Pass - 2026-07-08

This pass reads the current code as a system map before deciding where to
refactor. The goal is to avoid broad cleanup that looks clean locally but moves
product rules into the wrong layer.

### Current Source-Of-Truth Map

| Concern | Current authority | Health | Cleanup direction |
|---|---|---|---|
| Sequencer button grammar | `src/state/sequencer/SequencerInteractionPolicy.cpp` | Good | Keep this as the policy source. Handlers and view models should only adapt it. |
| Sequencer resolved display | `src/state/sequencer/SequencerResolvedDisplayProjectionOps.cpp` and `StepGridFrameLogic.cpp` | Good | Protect it. UI must continue to display what the engine will play, including child graph and random output. |
| Sequencer step visual language | `src/ui/sequencer/StepSemanticVisuals.hpp` and `StepPropertyVisuals.*` | Good | No local ad hoc colors/icons in feature files. Extend here first. |
| Sequencer step editor workflow | `src/handler/sequencer/SequencerStepEditHandler.cpp` | Risky | Split by row actions, context actions, preset picker, and chord detail routing. |
| Sequencer structure actions | `src/handler/sequencer/SequencerStructureEditWorkflow.cpp` plus paste plans | Medium risk | Preserve paste-plan objects; split current vs selection actions without changing behavior. |
| Sequencer view model | `src/ui/sequencer/SequencerViewModelBuilder.cpp` | Medium risk | Split builders after policy is stable so strips/header/grid consume resolved policy. |
| Step grid rendering | `src/ui/sequencer/StepGrid.cpp` and `StepGridRenderPlanner.*` | Mostly healthy | Keep cache and local invalidation model; avoid adding business rules here. |
| Macro grammar | `src/state/macro/MacroInteractionPolicy.cpp` | Needs alignment | Policy exists, but UI strip currently diverges for macro-slot focus. |
| Macro automation lifecycle | `src/state/macro/MacroAutomationDomain.*`, `MacroAutomationState.*`, handlers/services | Medium risk | Durable model is good; centralize timebase and edit semantics before modulators. |
| Project persistence | `ProjectSnapshot*`, `ProjectFileContainer*`, `ProjectMigration*`, `ms-core-file-tool` | Good | Keep one current codec path and explicit migration/report paths. |

### Positive Findings To Preserve

- Large project, sequencer, and macro automation buffers are generally allocated
  through `makeExtmemUnique` or stored in EXTMEM-backed state. The current
  memory risk is not obvious RAM1 exhaustion.
- `StepGrid` already has a tile render plan and per-tile cache. It returns early
  when neither geometry nor tile data changed, and invalidates individual step
  buttons for overlay changes.
- Sequencer clipboard and paste semantics use explicit graph/plan objects. This
  is the correct way to support root and child contexts without duplicated
  behavior.
- Project snapshot capture/apply restores sequencer history snapshots, macro
  tracks, shared track state, and macro automation through one persistence path.
- `ms-core-file-tool` calls Core project migration/codecs instead of defining a
  separate PC-side file semantics. This is the right long-term direction.
- Macro automation persistence has dense roundtrip tests that verify packed
  point payload size and restoration of multiple automation slots.

### Concrete Risks Found

1. Macro policy/view-model drift:
   `MacroInteractionPolicy::actionStrip()` marks bottom actions active in
   `STEP` focus, while `buildMacroBottomActionStripProps()` hides all bottom
   slots for `STEP` focus. That creates an invisible functional path. This is a
   P0 consistency issue before adding modulation features.
2. Macro automation timebase duplication:
   `MacroPerformanceDomainServices.cpp` and
   `MacroAutomationPlaybackService.cpp` both define local `elapsedBeats`
   helpers. Overlay formatting also owns tick/beat formatting and curve summary
   logic. The result is not broken today, but it makes future tempo, loop,
   offset, and quantization changes easy to desynchronize.
3. Macro automation detail grammar exception:
   `MacroAutomationHandler` uses `LEFT_CENTER` as a coarse edit modifier. This
   may be valid, but it must be explicitly reconciled with the sequencer grammar
   where `LEFT_CENTER` opens or applies the active context/property selector.
4. Step editor feature hub:
   `SequencerStepEditHandler` still owns input routing, row editing, local
   random, chord detail, child context actions, copy/paste, presets, back
   navigation, and encoder configuration. This is the highest-risk file for
   regressions during future step graph work.
5. View-model business logic accumulation:
   `SequencerViewModelBuilder` computes action-strip affordances, clipboard
   compatibility, paste preview context, labels, and grid frame input. It should
   consume policy/projection outputs, not become a second interaction model.
6. Persistence compatibility branches must remain intentional:
   The project codec currently handles minor-version differences and reports
   unsupported chunks. That is acceptable when explicit. Future format changes
   must not become silent fallback behavior in hot decoders.

### Progress Log

- `1e34a4c sequencer: align step property selector controls`
  completed Phase 0 for the current sequencer left-button grammar slice.
- `ec7bc8d macro: align slot action strip with policy` fixed the immediate
  Macro slot-focus hidden-action drift.
- `49a2abe macro: share interaction context projection` made Macro handler and
  Macro view-model consume one shared interaction context projection and added
  `test_MacroInteractionContextBuilder`.
- `46505d7 macro: remove dead performance mode predicates` removed residual
  Macro performance workflow predicates that were no longer called after the
  context projection cleanup.
- `3383809 macro: centralize automation elapsed beat math` moved recording and
  playback elapsed-beat conversion into `MacroAutomationDomain` and added a
  domain regression for tempo fallback and reversed-time safety.
- `8ee53af macro: project automation curve window state` moved automation curve
  window/source/offset/wrap projection from the macro overlay formatter into
  `MacroAutomationDomain`, with a domain regression for persisted window
  semantics.
- `355c2f4 macro: extract automation editor model` moved the macro automation
  OPT position/range math for length and offset out of `MacroAutomationHandler`
  into a tested pure editor model.
- `4bffa8f sequencer: centralize step property reset` moved root/child step
  property reset semantics into `SequencerContentViewOps`, made the step edit
  handler call one domain operation, and zeroed inactive offset payload values
  when their flags are cleared.
- `838b9b8 sequencer: centralize child context opening` moved micro-sequence
  and cycle-state open/create semantics into `SequencerContentViewOps`, leaving
  `SequencerStepEditHandler` responsible for history capture and overlay
  lifecycle only.
- `3b412ed sequencer: centralize child content clipboard actions` moved
  micro-sequence/cycle-state clear, copy, paste, clipboard compatibility, and
  view refresh semantics into `SequencerContentViewOps`, leaving the step edit
  handler responsible for button routing and history only.
- `25065b5 sequencer: extract step preset picker workflow` moved the step graph
  preset picker open/close/navigation/save/load/feedback behavior into
  `SequencerStepPresetPickerWorkflow`, leaving `SequencerStepEditHandler` with
  the history boundary around successful preset loads.
- `9c62c30 sequencer: extract step chord editor workflow` moved chord detail
  field navigation, value application, encoder configuration, and focused-field
  reset into `SequencerStepChordEditorWorkflow`, leaving the step edit handler
  with routing and OPT resynchronization only.
- `da92d22 sequencer: extract step value row workflow` moved step activated,
  musical property, quick chord, local random range, value-row encoder
  configuration, and value-row default reset behavior into
  `SequencerStepValueRowWorkflow`, with direct native coverage for the extracted
  workflow.
- `d687ead sequencer: extract step context row workflow` moved focused
  micro-sequence/cycle-state row kind resolution, create/open, copy, paste,
  clear, and paste compatibility checks into `SequencerStepContextRowWorkflow`,
  leaving history snapshots and overlay lifecycle in the step edit handler.
- `5747368 sequencer: extract step edit session workflow` moved step edit
  session open, close, commit-history, child-content back navigation,
  edited-step bounds, and open-release latch checks into
  `SequencerStepEditSessionWorkflow`, keeping encoder resync and top-level
  routing in the handler.
- `2bf076e sequencer: extract bottom action strip builder` moved the sequencer
  bottom action strip projection, selection counts, hold visuals, clipboard
  affordance checks, and local variation status label formatting into
  `SequencerBottomActionStripViewModelBuilder`, leaving
  `SequencerViewModelBuilder` with the public facade.
- `fd105a6 sequencer: extract header view model builder` moved the sequencer
  header projection, clipboard badge text, page source/destination preview
  masks, add-slot preview, and Track/Pattern/Step context label resolution into
  `SequencerHeaderViewModelBuilder`.
- `0dc10a0 sequencer: extract property overlay builder` moved step property
  selector, local random overlay value formatting, pattern quick-control overlay
  formatting, and shared quick-control icon/color mapping out of
  `SequencerViewModelBuilder`.
- `ae8eef4 sequencer: extract left action strip builder` moved the sequencer
  left action strip projection and action-to-icon visibility mapping into
  `SequencerLeftActionStripViewModelBuilder`, consuming the existing sequencer
  interaction policy and shared quick-control visuals.
- `0280914 sequencer: extract step grid view model builder` moved grid frame
  projection and step paste-preview footprint application into
  `SequencerStepGridViewModelBuilder`, reducing `SequencerViewModelBuilder` to
  public facade wrappers.
- `db92820 sequencer: extract structure step ops` moved selected-step range
  detection, root/child step reset, effective scale lookup for copied steps,
  step clipboard entry capture, and root/child step clipboard write helpers into
  `SequencerStructureStepOps`.
- `86c0ea7 sequencer: extract structure page clipboard ops` moved page
  clipboard capture, page paste, and copied page graph payload transfer into
  `SequencerStructurePageClipboardOps`.
- `7b887c6 sequencer: keep structure ops implementations in flash` moved the
  extracted structure step/page clipboard implementations out of large inline
  headers and into `FLASHMEM` `.cpp` units, preserving the cleanup without
  increasing RAM1 pressure.
- `ff476f7 sequencer: extract structure step paste workflow` moved step paste
  preview setup, paste-plan construction, content resizing, and root/child
  clipboard write dispatch into `SequencerStructureStepPasteWorkflow`, leaving
  `SequencerStructureEditWorkflow` responsible for history and focus updates.
- `83335e1 sequencer: extract structure track selection ops` moved track
  selection clipboard capture, track selection paste target projection, and
  selected-track snapshot/graph application into
  `SequencerStructureTrackSelectionOps`.
- `a177ad2 sequencer: extract structure page selection ops` moved page
  selection clipboard capture, page selection paste-plan construction, and
  selected-page paste application into `SequencerStructurePageSelectionOps`,
  leaving history, focus, preview, and selection cancellation in
  `SequencerStructureEditWorkflow`.
- `4737e8c sequencer: extract structure page selection mutations` moved
  selected-page active-mask filtering, selected-page clear, and selected-page
  remove mutations into `SequencerStructurePageSelectionOps`, keeping only
  history/cancel boundaries in `SequencerStructureEditWorkflow`.
- `8769569 sequencer: extract structure page current ops` moved page add-target
  resolution, page preview sync, focused-page clear, and focused-page remove
  into `SequencerStructurePageOps.cpp`, keeping the header declarative and
  `SequencerStructureEditWorkflow` focused on history routing.

Validated after the Macro context projection slice:

- `ms test core` -> `78/78`;
- `ms ux run core --select macro/slot-automation-local-actions.ux --report --no-interactive`
  -> OK.

Validated after the Macro timebase slice:

- `ms test core` -> `78/78`.

Validated after the Macro curve window projection slice:

- `ms test core` -> `78/78`;
- `ms ux run core --select macro/automation-curve-window-offset.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select macro/automation-curve-sparkline.ux --report --no-interactive`
  -> OK.

Validated after the Macro automation editor model slice:

- `ms test core` -> `79/79`;
- `ms ux run core --select macro/automation-coarse-length-offset.ux --report --no-interactive`
  -> OK.

Validated after the Sequencer step property reset slice:

- `ms test core` -> `80/80`;
- `ms ux run core --select sequencer/editing/step-edit-basic.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/editing/step-edit-local-random.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/step-editor-bottom-actions.ux --report --no-interactive`
  -> OK.

Validated after the Sequencer child context opening slice:

- `ms test core` -> `80/80`;
- `ms ux run core --select sequencer/step-content/child-step-edit-back-to-parent.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/step-content/closeout-authoring-runtime.ux --report --no-interactive`
  -> OK.

Validated after the Sequencer child content clipboard actions slice:

- `ms test core` -> `80/80`;
- `ms ux run core --select sequencer/step-content/child-step-edit-back-to-parent.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/step-content/closeout-authoring-runtime.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/step-editor-bottom-actions.ux --report --no-interactive`
  -> OK.

Validated after the Sequencer step preset picker workflow slice:

- `ms test core` -> `80/80`;
- `cmake --build C:\Users\miu-lab\ms-dev-env\.build\core\native --clean-first --target midi_studio_core -j 16`
  -> OK; required locally after the `SequencerStepEditHandler.hpp` layout change
  to avoid a stale SDL object allocating the old handler size;
- `ms ux run core --select sequencer/editing/step-preset-picker.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/editing/step-edit-basic.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer step chord editor workflow slice:

- `ms test core` -> `80/80`;
- `ms ux run core --select sequencer/editing/step-edit-chord.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/editing/step-edit-chord-strum.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer step value row workflow slice:

- `ms test core` -> `81/81`;
- `ms ux run core --select sequencer/editing/step-edit-basic.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/editing/step-edit-local-random.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/editing/step-edit-chord.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/structure/step-editor-bottom-actions.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer step context row workflow slice:

- `ms test core` -> `82/82`;
- `ms ux run core --select sequencer/step-content/child-step-edit-back-to-parent.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/step-content/closeout-authoring-runtime.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/structure/step-editor-bottom-actions.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer step edit session workflow slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/editing/step-edit-basic.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/step-content/child-step-edit-back-to-parent.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/editing/step-preset-picker.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/structure/step-editor-bottom-actions.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer bottom action strip builder slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/structure/property-strip-contexts.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/step-editor-bottom-actions.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/step-content/child-step-edit-back-to-parent.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/editing/step-edit-local-random.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer header view model builder slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/structure/canonical-scope-cycle.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/page-copy-paste-preview.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer property overlay builder slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/editing/quick-controls-basic.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/editing/step-edit-local-random.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/structure/property-strip-contexts.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer left action strip builder slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/structure/property-strip-contexts.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/editing/quick-controls-basic.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer step grid view model builder slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/structure/step-selection.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/step-focus-copy-paste.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer structure step ops slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/structure/step-focus-copy-paste.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/step-selection.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/structure/child-step-selection-bottom-actions.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer structure page clipboard ops slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/structure/page-copy-paste-preview.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/page-chord-copy-paste.ux --report --no-interactive --skip-build`
  -> OK.

Validated after moving extracted structure ops into `FLASHMEM` implementation
units:

- `ms test core` -> `83/83`.

Validated after the Sequencer structure step paste workflow slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/structure/step-focus-copy-paste.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/step-selection.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/structure/child-step-selection-bottom-actions.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer structure track selection ops slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/structure/track-bottom-actions.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/track-chord-copy-paste.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer structure page selection ops slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/structure/page-copy-paste-preview.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/pattern-selection-bottom-actions.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/structure/page-chord-copy-paste.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer structure page selection mutations slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/structure/pattern-selection-bottom-actions.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/page-chord-copy-paste.ux --report --no-interactive --skip-build`
  -> OK;
- `ms ux run core --select sequencer/structure/page-copy-paste-preview.ux --report --no-interactive --skip-build`
  -> OK.

Validated after the Sequencer structure page current ops slice:

- `ms test core` -> `83/83`;
- `ms ux run core --select sequencer/structure/pattern-selection-bottom-actions.ux --report --no-interactive`
  -> OK;
- `ms ux run core --select sequencer/structure/page-copy-paste-preview.ux --report --no-interactive --skip-build`
  -> OK.

### Current Uncommitted Sequencer Grammar Slice

The initial working tree contained a small, coherent sequencer grammar slice:

- In Step focus, `LEFT_CENTER` opens the musical property selector.
- In Step focus, `LEFT_BOTTOM` is hidden on the main surface.
- While the musical property selector is open in Step focus, `LEFT_CENTER`
  applies/closes the selector and `LEFT_BOTTOM` edits the local random layer for
  the selected property.
- In Pattern focus, musical property selection still uses `LEFT_BOTTOM`; pattern
  dimensions use `LEFT_CENTER`.
- Track focus has neither pattern nor step-property selector access.

This slice was validated and committed as `1e34a4c`; it defines the reference
model the macro surface should learn from.

### First Code-Quality Decisions

- Do not introduce a generic global "button grammar engine" yet. Sequencer and
  macro policies are similar but not identical; premature generic code would
  hide product decisions. Extract only shared mappers where behavior is already
  proven identical, such as action-strip visual mapping and hold progress
  widgets.
- Treat policy tests as the canonical interaction matrix. UX workflows prove
  the rendered result; unit tests must prove the grammar contract.
- Keep macro automation absolute recording production-ready before adding LFO,
  ADSR, or modulation-preset features. A modulator layer should build on the
  same packed curve/timebase primitives, not add a second automation format.
- Do not refactor StepGrid first. It is large, but already has the right cache
  shape. Refactor upstream policy/projection builders first so StepGrid remains
  a renderer.
- Keep file migration shared by controller and PC tools. New file families
  should follow the same pattern: current codec, explicit load report,
  migration entrypoint, native fixture tests, file-tool command.

## Bootstrap Codebase Convergence Plan

This is the minimal path toward a clean bootstrap codebase: a codebase that can
serve as the stable base for the next product features without carrying
transitional workflow debt.

### Phase 0 - Freeze The Contract Before Refactoring

Goal: make the current behavior explicit before moving code.

Work:

1. Finish the current sequencer left-button grammar slice.
2. Add or keep policy tests for every affected context.
3. Keep UX workflows aligned with the visible controller behavior.
4. Run `ms test core`.
5. Commit this as a small behavior slice before structural refactors.

Exit criteria:

- Sequencer Step/Pattern/Track selector access is unambiguous.
- Tests describe the physical-control grammar.
- No unrelated cleanup is bundled into this commit.

### Phase 1 - Remove Double Truths

Goal: every visible interaction has exactly one policy source and one UI
projection path.

Work:

1. Align Macro policy and Macro view-model strips.
2. Add Macro view-model strip tests where policy tests are not enough.
3. Audit sequencer and macro handlers for direct focus checks that duplicate
   policy decisions.
4. Extract shared visual mapping only for policy action to strip icon/tone,
   where the behavior is already identical.

Exit criteria:

- No action can be functional while hidden, unless explicitly named as a hidden
  shortcut in policy and tests.
- Policy tests and view-model tests agree.
- User-facing button grammar can be explained from one source per domain.

### Phase 2 - Split High-Risk Hubs Without Changing Behavior

Goal: reduce file responsibility before adding features.

Work order:

1. Split `SequencerStepEditHandler` into row actions, context actions, preset
   picker workflow, and a thin router.
2. Split `SequencerViewModelBuilder` into header, action strip, property
   overlay, and grid builders.
3. Split `SequencerStructureEditWorkflow` into current actions, selection
   actions, step clipboard workflow, and paste preview workflow.

Exit criteria:

- Public behavior and UX captures are unchanged.
- Extracted modules have direct tests.
- New developers can find mutation logic without scanning a thousand-line file.

### Phase 3 - Stabilize Macro Automation As A Product Primitive

Goal: make automation reliable enough to become the base for modulation.

Work:

1. Centralize tempo-to-beat, tick formatting, duration quantization, crop,
   extend, and offset wrapping helpers.
2. Keep one durable packed curve representation.
3. Keep temporary recording buffers separate from persisted state.
4. Validate dense persistence, copy/paste, clear/remove, restore, override, and
   reload.
5. Decide and document the exact `LEFT_CENTER` and `LEFT_BOTTOM` macro grammar
   before adding LFO/ADSR/modulation presets.

Exit criteria:

- Recording, playback, editor overlay, and persistence share the same curve
  semantics.
- Automation lifecycle is production-ready before modulators are introduced.

### Phase 4 - Bound UI Performance And Memory

Goal: keep the controller responsive on Teensy while preserving rich feedback.

Work:

1. Keep `StepGrid` as a renderer and avoid adding business decisions there.
2. Add targeted cache tests or perf counters around track switch, random
   preview, selection paste preview, and automation playback UI.
3. Move warm non-realtime large state to EXTMEM when it is not timing-critical.
4. Record `pio run` memory output when a change touches storage, UI caches, or
   automation capacity.

Exit criteria:

- Runtime value changes invalidate only local UI regions.
- RAM1 usage remains intentional and documented.
- No large feature adds an unmeasured memory path.

### Phase 5 - Persistence Discipline

Goal: keep PC and controller file behavior identical.

Work:

1. Keep current codecs single-layout.
2. Put older layouts behind explicit migration and load reports.
3. Add fixtures for every durable schema change.
4. Extend `ms-core-file-tool` command coverage as new file families appear.

Exit criteria:

- Controller and PC tooling use the same Core codecs/migrators.
- Old data never silently mutates into corrupt UI state.
- Every supported migration is fixture-backed.

## Findings And Roadmap

### P0 - Protect The Current Interaction Grammar

Concern:

- Sequencer and Macro now share a product goal: common interaction grammar.
  The sequencer policy work is good, but it is still possible for handlers,
  view-model strips, UX semantic surfaces, and tests to drift independently.

Evidence:

- `src/state/sequencer/SequencerInteractionPolicy.cpp` is the best current
  source of truth for sequencer behavior.
- `src/handler/sequencer/SequencerInteractionPolicyAdapter.hpp` exposes handler
  predicates.
- `src/ui/sequencer/SequencerViewModelBuilder.cpp` still derives visible affordance
  state and clipboard compatibility locally.
- `src/state/macro/MacroInteractionPolicy.cpp` and
  `src/ui/view/MacroViewModelBuilder.cpp` currently disagree for macro-slot
  focus bottom-strip visibility.

Work:

1. Treat interaction policy structs as the only authority for button/encoder
   meaning on each surface.
2. Add a small table-style test for every context:
   `Track`, `Pattern`, `Step`, each selection mode, each transient selector,
   and `StepEditor`.
3. Move action-strip icon choice to a shared visual mapper per policy action.
4. Audit every handler predicate for direct focus/overlay checks that duplicate
   policy decisions.
5. Keep UX workflows as visual proof, but make unit tests carry the grammar
   contract.
6. Add a Macro view-model strip test or move macro strip building through the
   macro policy so hidden functional paths cannot reappear.

Acceptance:

- A new developer can answer "what does this physical control do in this
  context?" by opening one policy test plus one policy source file.
- No handler directly reimplements the context matrix when a policy predicate
  exists.
- `ms test core` and the targeted sequencer UX workflows pass.
- Macro slot focus cannot expose behavior without a matching visible affordance,
  unless the product intentionally defines a hidden shortcut and tests name it.

### P1 - Split `SequencerStepEditHandler`

Concern:

- `SequencerStepEditHandler.cpp` is the largest handler and currently owns too
  many roles: open/close, row navigation, value editing, local random layer,
  chord detail editing, context child mutation, copy/paste, preset picker,
  history, and encoder synchronization.

Evidence:

- The file contains large clusters around setup bindings, focused row editing,
  chord editor operations, context child actions, and step preset actions.
- Chord edit mechanics have already started moving to `SequencerChordEditOps.*`,
  which is the right direction.

Work:

1. Extract `SequencerStepEditorRowActions`:
   - reset value row;
   - edit value row;
   - configure normalized encoder for value rows;
   - local random range edits.
2. Extract `SequencerStepEditorContextActions`:
   - create/remove/copy/paste child context;
   - context mutation history;
   - parent/back behavior.
3. Extract `SequencerStepPresetPickerWorkflow`:
   - list refresh;
   - mode toggle;
   - selected preset lookup;
   - load/save execution;
   - feedback formatting.
4. Keep `SequencerStepEditHandler` as an input router and overlay lifecycle
   owner only.
5. Preserve existing tests, then add tests at the extracted module level before
   removing duplicate handler assertions.

Acceptance:

- `SequencerStepEditHandler.cpp` drops below roughly 600 lines.
- Each extracted module has a clear header contract and direct tests.
- No behavior change without a matching UX workflow update.
- `ms test core`; step editor UX workflows; `pio run`.

### P1 - Split `SequencerStructureEditWorkflow`

Concern:

- This workflow is powerful but broad. It mixes page/track/step operations,
  selection actions, clipboard capture/apply, paste previews, and history.
  It is readable today but expensive to modify safely.

Work:

1. Keep the public workflow facade, but extract internal modules:
   - `SequencerStructureCurrentActions`;
   - `SequencerStructureSelectionActions`;
   - `SequencerStepClipboardWorkflow`;
   - `SequencerStructurePastePreviewWorkflow`.
2. Move capture/apply helpers next to their clipboard/paste-plan models.
3. Make copy/paste behavior identical across root and child contexts through
   shared plan objects, not ad hoc branching.
4. Add a small operation matrix test covering:
   - clear/remove/copy/paste current;
   - clear/remove/copy/paste selection;
   - track, pattern, root step, child step.

Acceptance:

- Root and child step behavior are tested through the same matrix.
- Current-action and selection-action code paths share explicit plans where
  behavior is intended to match.
- No stale "track legacy" or transitional naming remains.

### P1 - Separate Sequencer View-Model Decisions

Concern:

- `SequencerViewModelBuilder.cpp` is becoming a policy adapter, a clipboard
  compatibility checker, an action-strip builder, a label formatter, a paste
  preview projector, and a grid-frame builder.

Work:

1. Split into narrow builders:
   - `SequencerHeaderViewModelBuilder`;
   - `SequencerActionStripViewModelBuilder`;
   - `SequencerPropertyOverlayViewModelBuilder`;
   - `SequencerStepGridViewModelBuilder`.
2. Keep low-level text formatting close to the target component only when it is
   presentation-specific.
3. Move context/clipboard compatibility assembly into a state-level projection
   helper if multiple UI surfaces need it.
4. Ensure each builder consumes already-resolved policy/projection data rather
   than recomputing business decisions.

Acceptance:

- Each builder file has one visible output type.
- Interaction decisions remain traceable to `SequencerInteractionPolicy`.
- BuildStepGrid does not need to know unrelated action-strip details.

### P2 - Bound StepGrid Render Cost

Concern:

- `StepGrid.cpp` now renders semantic badges, selection overlays, paste
  previews, resolved variation ranges, runtime deltas, labels, bars, and shapes.
  That is product-correct, but it makes the grid the highest-risk UI performance
  surface.

Work:

1. Record per-tile revision keys for:
   - structural frame geometry;
   - semantic badges;
   - value labels;
   - runtime variation preview;
   - selection/paste overlay.
2. Ensure LVGL object property writes happen only when cached values changed.
3. Move custom draw helpers into focused render modules:
   - `StepGridSelectionRenderer`;
   - `StepGridVariationRenderer`;
   - `StepGridBadgeRenderer`.
4. Add a native render/cache test that proves unchanged frame props do not
   rewrite every tile cache.
5. Add a UX stress workflow: playback running, random preview visible,
   selection paste preview, and page/track switch.

Acceptance:

- No full-grid layout recomputation for value-only edits.
- Hardware or SDL perf counter evidence for track switch and local random edit.
- Existing captures remain visually equivalent.

### P2 - Stabilize Macro Automation Memory And Lifecycle

Concern:

- The macro automation design is directionally good: durable storage uses
  curve refs plus a packed point pool. However, max capacities are high enough
  that compaction/copy operations and persistence payload size must be watched.
- Macro automation currently has several time/curve presentation helpers in
  handler, playback, domain, and overlay formatter code. They should converge
  before modulation features are added.

Evidence:

- `MACRO_AUTOMATION_RECORDING_MAX_POINTS = 2048`.
- `MACRO_AUTOMATION_POINT_POOL_CAPACITY = 32768`.
- `MacroAutomationLane` and `MacroModulationShape` are temporary full float
  buffers; durable data is packed.

Work:

1. Document exact worst-case RAM/PSRAM and file payload size for:
   - one lane;
   - full point pool;
   - copied automation clipboard;
   - project snapshot with all automation points.
2. Verify all large automation buffers are PSRAM-backed or short-lived stack
   safe. Move any warm non-realtime large state to EXTRAM if needed.
3. Add tests for:
   - full pool refusal behavior;
   - compaction preserving references;
   - copy/paste across macro slots/pages/tracks;
   - persistence reload of sparse and dense automation banks.
4. Keep live recording deterministic:
   - quantization rule;
   - point simplification tolerance;
   - crop/extend/window offset semantics.
5. Centralize tempo-to-beat conversion, duration quantization, tick formatting,
   and window summary helpers under the macro automation domain/projection
   boundary.
6. Decide whether `LEFT_CENTER` coarse edit is a durable macro automation grammar
   rule or a temporary implementation detail. Update tests and overlays
   accordingly.
7. Do not add modulation features until automation lifecycle is production
   ready.

Acceptance:

- Worst-case memory is written down and verified with `pio run`.
- Automation lifecycle can be tested end-to-end from UI workflow, domain test,
  persistence test, and playback test.
- No hidden second representation becomes durable.
- Playback, recording, editing, and overlay formatting agree through shared
  timebase helpers.

### P2 - Persistence And Migration Contract Cleanup

Concern:

- The current chunked project file direction is strong, but persistence can
  become fragile if current codecs silently carry older layouts.

Work:

1. Keep hot/current codecs single-layout.
2. Route older data through `ProjectChunkMigration` and `ProjectMigration`.
3. Add fixtures whenever a persisted schema changes.
4. Verify `ms-core-file-tool` can inspect/migrate every durable product file
   family, not only projects:
   - projects;
   - step presets;
   - future modulation presets;
   - future chord/pattern presets.
5. Keep load reports user-actionable: current, migrated, partial, failed,
   unsupported version.

Acceptance:

- No current decoder has hidden multi-version compatibility branches.
- Fixture coverage exists for every supported migration.
- Old unsupported data fails or partially loads through explicit reports, not
  random UI corruption.

### P3 - Test And UX Harness Hygiene

Concern:

- Test volume is good, but several test files are now very large. They can
  become as hard to maintain as the production code they protect.

Work:

1. Extract reusable harnesses from large test files:
   - sequencer step edit harness;
   - structure operation harness;
   - project persistence harness;
   - macro automation harness.
2. Keep behavior tests named by user-visible contract.
3. Avoid encoding accidental implementation details unless they are part of the
   product contract.
4. Ensure UX workflows have stable names and comments that match the current
   grammar.

Acceptance:

- Large tests shrink by moving setup mechanics into `test/support`.
- Each test still reads as a product behavior statement.
- UX workflow comments do not reference obsolete controls or legacy modes.

## Recommended Execution Order

1. Finish and commit the current sequencer left-button grammar work.
2. Run the P0 policy/handler audit before adding new macro grammar features.
3. Split `SequencerStepEditHandler` first. It is the highest-risk feature hub.
4. Split `SequencerViewModelBuilder` and action-strip builders next, because
   they directly affect UI consistency.
5. Split `SequencerStructureEditWorkflow` after the policy and builder seams are
   stable, to avoid moving interaction semantics twice.
6. Run StepGrid performance work after structural splits, so visual behavior
   can be compared against stable workflows.
7. Stabilize macro automation memory/lifecycle before introducing modulators.
8. Re-run persistence fixture/migration review after macro automation and step
   preset formats settle.

## Required Validation Commands

For every cleanup slice:

```powershell
git diff --check
ms test core
```

When UI behavior changes:

```powershell
ms ux run core --select sequencer/structure/property-strip-contexts.ux
ms ux run core --select sequencer/editing/macro-local-random.ux
```

When step editor changes:

```powershell
ms ux run core --select sequencer/editing/step-edit-basic.ux
ms ux run core --select sequencer/editing/step-edit-local-random.ux
ms ux run core --select sequencer/editing/step-edit-chord.ux
```

When persistence changes:

```powershell
ms test core
python script/dev/validate-project-fixtures.py
python script/dev/validate-step-graph-preset-fixtures.py
```

When memory or runtime-sensitive code changes:

```powershell
pio run
```

Record the Teensy size output in the commit or audit note when the change is
expected to affect RAM1, RAM2, EXTRAM, FLASH, or runtime allocation.

## Exit Criteria For This Roadmap

- The main sequencer feature hubs are split into modules with one clear role.
- Interaction grammar is tested from the policy level and visually confirmed
  through UX workflows.
- UI render hot paths have cached local invalidation and measured performance
  evidence.
- Macro automation has documented worst-case memory and persistence behavior.
- Persistence keeps one current path plus explicit migration paths.
- No cleanup leaves compatibility aliases, unused feature branches, or duplicate
  user-action paths behind.
