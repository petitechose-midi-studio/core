# Core architecture map

This document is the current, source-adjacent map for MIDI Studio Core. It
describes where code belongs and how a change travels through the firmware. It
does not own roadmap or historical audit information; those live in
`petitechose-audio-docs`.

## Read a feature from input to output

Follow this direction when investigating behavior:

```text
physical input
    -> context composition and scoped binding
    -> domain handler or workflow
    -> domain state
    -> runtime snapshot/service and/or persistence transaction
    -> read-only UI projection
```

The UI may observe runtime and domain state, but it must not become a shortcut
for domain mutation. Persistence may reconstruct domain records, but it must
not call handlers or depend on LVGL.

## Layer ownership

| Folder | Owns | Must not own |
| --- | --- | --- |
| `src/api` | small product facades over OpenControl input APIs | domain workflow or mutable state |
| `src/app` | allocation helpers and application-level types | product workflows |
| `src/config` | platform-independent constants and input IDs | mutable domain state |
| `src/context` | feature assembly, scopes, overlays, presenters, service wiring | duplicated domain rules |
| `src/diagnostics` | compile-time removable measurement and reporting | product authority or release-build side effects |
| `src/handler` | physical-input interpretation and domain-specific interaction workflows | LVGL rendering, file codecs, global runtime engines |
| `src/state` | canonical product state, invariants, snapshots, and explicit domain workflows | hardware bindings, widgets, filesystem transport |
| `src/persistence` | file stores, containers, version inspection, bounded codecs, atomic transactions | UI state transitions and realtime playback |
| `src/sequencer` and `src/midi` | realtime playback, clock, MIDI evaluation, runtime publication | project menus, file lifecycle, LVGL |
| `src/ui` | view models, render caches, widgets, and read-only state projection | domain mutation and reusable non-visual rules |
| `src/protocol` | controller/host transport protocol | product menu or musical-state authority |
| `src/validation` | diagnostics and semantic validation surfaces | product-only behavior |

Dependencies should point toward the owning layer. A lower-level reusable rule
must not live in `ui/` merely because the first consumer was visual. A global
runtime service must not live under one domain handler merely because that
handler initially called it.

## Current authorities

- `ProjectTrackState` owns durable Track channel, mute, solo, and delay state.
- `ProjectControlDomainState` owns durable Macro Automation, Modulation, and
  Recorded Shape content.
- `MidiSyncState` owns the current controller MIDI-sync values, while
  `MidiNoteDisplayState` owns the device-wide note-octave naming convention.
  `persistence/DeviceSettingsStore` is their single durable byte-store owner;
  `DeviceSettingsState` owns only navigation state for the settings UI.
- `SequencerGraphAsset` owns reusable Step Graph Preset musical content and
  adaptation. `SequencerGraphCanonicalPolicy` owns pure Sequence/Step Node
  admission invariants. `SequencerGraphRecordCodec` owns fixed graph bytes and
  delegates admission to that policy, while `SequencerGraphAssetCodec` owns
  the preset file envelope.
- `sequencer/MidiCcGlobalFrameCoordinator` is the singular realtime Gate for
  persistent Macro/manual and Sequencer CC Lane authors. It owns temporal
  staging, destination arbitration and transactional queue commit; handlers
  are producers, not owners. Its implementation files are review boundaries,
  not separate authorities: the base `.cpp` owns temporal resolution,
  `*Publication.cpp` owns author-frame publication, and `*Lifecycle.cpp` owns
  queue/clock/trigger lifecycle.
- `ProjectModulationTriggerQueue`, `ProjectControlClockPublisher`, and
  `MidiCcResolutionTelemetryExchange` are bounded by-value collaborators of
  the Gate. Their independent tests define FIFO/overflow, clock publication,
  and held-reader triple-buffer invariants respectively.
- `OverlayManager` owns overlay visibility transitions.
- `StructureSelectionInteractionPolicy` owns the shared, non-mutating
  selection/placement gesture lifecycle. Macro and Sequencer workflows own
  their respective cursor bounds, preflights and mutations.
- `MacroSourceDetailPolicy` owns the ordered semantic Automation and
  Modulation detail rows. Handlers map those rows to actions; presenters map
  them to labels and icons.
- `state/modulation/*ParameterMapping.hpp` owns encoder/value conversions,
  musical rate grids and authored bounds. `ui/modulation/*UiModel` owns only
  labels, text formatting, preview geometry and runtime-marker projection.
- domain handlers own bindings; the collision-free physical-control contract
  is documented in [INPUT_BINDINGS.md](INPUT_BINDINGS.md).
- project/session and reusable preset persistence is file-backed. Do not add a
  second fixed-slot or in-memory persistence path.
- the product is pre-V1: current-format inspection remains explicit, but old
  model readers and speculative migration branches are not retained.

When an authority changes, record the rationale in the canonical ADR and place
the enforceable API contract beside the owning header.

## Where a new change belongs

| Change | Start here |
| --- | --- |
| musical invariant or canonical value | owning `src/state/<domain>` contract |
| button/encoder gesture | owning `src/handler/<domain>` policy and handler |
| cross-domain runtime timing or MIDI frame work | `src/sequencer` or `src/midi` |
| file format, preset, project, or session behavior | `src/persistence` |
| visual representation only | `src/ui` view model or component |
| object construction, scope, or dependency injection | `src/context` |
| cross-repo or durable product decision | ADR in `petitechose-audio-docs` |

Do not introduce a generic handler to make two domains look similar. Share a
small mechanical policy only when the gestures and invariants are genuinely
identical; keep each domain transition visible in its owning workflow.

`src/handler` must not include `src/ui`. If an interaction needs a reusable
non-visual conversion or ordered policy, place that contract beside the
canonical state it interprets. If it needs display text, keep that projection
at the presentation boundary instead of making the domain depend on wording.

## Mutation vocabulary

Domain operation names state what survives:

| Verb | Contract |
| --- | --- |
| `reset` | retain the address/entity and restore its complete payload, or an explicitly named part, to canonical defaults |
| `clear` | retain the parent and addressed slot while removing named child content or emptying a named collection |
| `delete` | make an entity or structural slot cease to exist or cease to be enabled |
| `remove` | detach a relationship or apply a neutral collection/span primitive |
| `erase` | invalidate raw storage, arena records, or low-level algorithmic data only |
| `discard` / `cancel` | abandon transient, uncommitted work |

Cross-context workflow methods name the physical gesture when one gesture has
different valid domain effects, for example
`applyCurrentStructureShortPress()` and
`applyCurrentStructureLongPress()`. User-facing `REMOVE` remains an interaction
intent and does not replace precise domain verbs. New compatibility aliases or
synonymous wrappers are not added during pre-V1 development.

## Embedded constraints

- avoid allocation and unbounded work in per-frame paths;
- use bounded fixed-capacity data structures for product state;
- use PSRAM-backed ownership for large snapshots or workspaces when required;
- publish heavy edits to realtime services through explicit snapshot/commit
  boundaries;
- invalidate the smallest practical UI region;
- keep diagnostics compile-time removable from the product image;
- treat files over 800 lines as review prompts, not automatic split targets.

A split is useful only when it isolates an authority, lifecycle, dependency
direction, reusable policy, or independently testable pipeline.

## Validation path

Native unit tests always use the workspace command:

```powershell
ms test core
```

Repository-wide architecture contracts are a separate, fast static gate:

```powershell
python script/dev/check-architecture-contracts.py
```

It enforces dependency directions, retired pre-V1 paths, mutation vocabulary,
input-routing ownership, diagnostics placement, memory gates, and retained-view
policies. Its >800-line inventory is advisory and is derived only from tracked
or nonignored source candidates.

Do not use PlatformIO's native test runner. PlatformIO remains the firmware
build and upload backend through the workspace commands:

```powershell
ms build core --target teensy --env dev
ms upload core --env dev
```

When exported headers move or change:

```powershell
pwsh ./script/dev/check-downstream-compat.ps1
```

Before calling a change release-ready:

```powershell
git diff --check
ms release dependencies --dry-run
```

Runtime, memory, or placement changes also require a product firmware build,
comparison against the configured FLASH/RAM/PSRAM gates, and focused hardware
evidence when timing or physical interaction is involved.

## New-developer path

1. Read this map and [ARCHITECTURE_REVIEW_RULES.md](ARCHITECTURE_REVIEW_RULES.md).
2. Read [INPUT_BINDINGS.md](INPUT_BINDINGS.md) before touching a physical
   control.
3. Locate the owning state contract, then its handler, composition module,
   runtime/persistence boundary, and UI projection.
4. Run `python script/dev/check-architecture-contracts.py` and `ms test core`
   before editing to establish the baseline.
5. Make one authority-level change, add a behavior-named test, and run the full
   gate again.
6. Update the source-local contract when implementation truth changes; update
   the canonical ADR/track when rationale, roadmap, or cross-repo impact
   changes.
