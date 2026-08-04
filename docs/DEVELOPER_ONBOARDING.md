# Developer onboarding

This is the shortest reliable path from a fresh MIDI Studio workspace to a
reviewable Core change. It describes the current pre-V1 architecture; durable
history and roadmap decisions remain in `petitechose-audio-docs`.

## 1. Prepare the workspace

Core is developed inside `ms-dev-env`, which owns toolchains, dependency
checkouts and the `ms` command. Follow the workspace root README for initial
setup. With the user-level launcher installed, the commands below work from
any directory; otherwise prefix them with `uv run` from the workspace root.

Establish a baseline before editing:

```powershell
ms check
python midi-studio/core/script/dev/check-architecture-contracts.py
ms test core
```

The static gate is intentionally separate from native tests. It checks layer
directions, retired pre-V1 paths, mutation vocabulary, input ownership,
diagnostics placement, memory gates and retained-view policies. It also prints
an advisory count of files over 800 physical lines.

Useful execution paths:

```powershell
# Native simulator
ms build core --target native
ms run core

# Product firmware
ms build core --target teensy --env dev
ms upload core --env dev
ms monitor core --env dev

# Instrumented firmware
ms build core --target teensy --env dev_diagnostics
```

Native unit tests always use `ms test core`. Do not use PlatformIO's native
test runner.

## 2. Use the ownership model

Read a feature in this order:

```text
physical input
    -> context composition and scoped binding
    -> domain handler/workflow
    -> canonical state and invariant
    -> realtime service and/or persistence transaction
    -> read-only UI projection
```

Start from the owner, not from the largest caller:

| Change | First owner to inspect | Typical proof |
| --- | --- | --- |
| musical value or invariant | `src/state/<domain>` | pure domain test |
| button, encoder or hold grammar | `src/handler/<domain>` and `docs/INPUT_BINDINGS.md` | handler test and focused `.ux` workflow |
| overlay lifecycle or feature wiring | `src/context` | presenter/module test and semantic UX |
| realtime MIDI, clock or playback | `src/sequencer` or `src/midi` | bounded runtime test, firmware build, hardware measurement if timing changes |
| project, session, preset or file bytes | `src/persistence` | exact-byte/round-trip/rejection tests |
| visual projection only | `src/ui` | view-model/render test and capture when visual output changes |
| controller/host wire contract | `src/protocol` | protocol fixture and downstream check |

`CoreState` is an application-lifetime composition aggregate as well as the
root of canonical state. Its explicit `DeviceSettingsStore` member is a
documented exception; it does not authorize general State-to-Persistence
dependencies.

## 3. Know the repository

```text
src/
  api/          small product facades over OpenControl input APIs
  app/          application allocation and shared app-level types
  config/       constants, timing and physical input IDs
  context/      composition, scopes, overlays, presenters and wiring
  diagnostics/  compile-time removable performance/memory reporting
  handler/      input interpretation and domain workflows
  midi/         MIDI transport/runtime helpers
  persistence/  product files, stores, codecs and atomic transactions
  protocol/     controller/host protocol
  sequencer/    realtime playback, clocks and MIDI frame coordination
  state/        canonical state, invariants, snapshots and domain policies
  ui/           view models, retained views, widgets and render projections
  validation/   semantic validation and smoke surfaces

test/           native behavior tests run by `ms test core`
sdl/            native/WASM simulator and semantic UX workflows
benchmark/      explicit host microbenchmarks, never firmware authority
script/dev/     local architecture and downstream gates
script/pio/     linker placement and post-link memory gates
cmake/          native source graph and test registration
tools/          host-side Core file tooling
asset/          source and allowlisted generated product assets
```

The detailed layer map is [CORE_ARCHITECTURE.md](CORE_ARCHITECTURE.md).
Physical-control ownership is [INPUT_BINDINGS.md](INPUT_BINDINGS.md).

## 4. Preserve the pre-V1 policy

- Keep one canonical current model and one execution path.
- Do not add forwarding headers, old namespaces, deprecated aliases or
  speculative migrations.
- Reject unsupported durable formats explicitly.
- Preserve the building blocks required for a future migration layer:
  version inspection, load reports, bounded codecs and atomic transactions.
- Add a compatibility reader only after V1 creates a published format that
  users actually need to migrate.

Mutation names state what survives:

- `reset`: retain the address/entity and restore its payload;
- `clear`: retain the parent/slot and remove named child content;
- `delete`: destroy or disable the entity/structural slot;
- `remove`: detach a relation or use a neutral collection/span primitive;
- `erase`: raw storage or arena invalidation only;
- `discard` / `cancel`: abandon uncommitted work.

## 5. Treat performance as evidence

Embedded constraints are part of behavior. Avoid unbounded work and allocation
in frame/realtime paths, keep large ownership explicit, and use fixed-capacity
structures where the product envelope is bounded.

A large file or buffer is not a defect by itself. Refactor only when a review
identifies mixed authority, independent lifecycles, duplicated policy,
dependency leakage, unclear memory ownership or a measured hot path. A
performance or capacity change needs a reproducible scenario and before/after
Flash, RAM1, RAM2 and PSRAM evidence.

Use the diagnostics build for timing and high-water evidence; do not infer
Teensy behavior from host timings alone.

## 6. Close a change with the right gates

Every product change:

```powershell
python script/dev/check-architecture-contracts.py
ms test core
git diff --check
```

Add gates according to scope:

| Scope | Additional gate |
| --- | --- |
| input, navigation, overlay or visible UX | focused `ms ux run core --select <workflow>` |
| firmware, realtime, placement or memory | `ms build core --target teensy --env dev` |
| diagnostics or footprint | build both `dev` and `dev_diagnostics`, compare exact ELF output |
| exported Core headers | `pwsh ./script/dev/check-downstream-compat.ps1` |
| release readiness | `ms release dependencies --dry-run` after intended repositories are clean |

PlatformIO-generated directories, CMake output, captures, binaries, maps and
local logs are ignored and must not enter a commit. The committed icon font
and generated C++ font data are deliberate product assets and must move with
their source SVG changes.

## 7. Keep implementation and documentation atomic

A committable lot has one responsibility, its tests and its documentation.
Update:

- the owning `.hpp` when an API invariant changes;
- repository docs when the developer workflow or source map changes;
- the canonical ADR/track in `petitechose-audio-docs` when rationale,
  cross-repository policy, roadmap or measured closure evidence changes.

Before handoff, explain the owner changed, the invariant preserved, the gates
run and any hardware validation still required. Do not hide unrelated dirty
worktree changes inside the lot.
