# Sprint 0: Documentation Source Of Truth

Purpose: make the documentation entry path accurate enough that future
architecture and implementation work can trust it.

This sprint is intentionally documentation-first. It should not refactor runtime,
state, input, UI, persistence, or HAL code except for tiny evidence checks needed
to classify documentation claims.

Important rule: do not preserve visible legacy documentation as a normal
developer entry point. Durable architecture rationale should live in standardized
current docs and, when it belongs to a code contract, in the relevant `.hpp`.
`.cpp` comments should stay minimal and explain only local non-obvious
implementation details.

## Scope

Included:

- Validate the architecture-chantier portfolio.
- Audit active docs for broken links and stale references.
- Separate current-contract documents from legacy material, then retire or
  remove legacy material from the standard entry path.
- Align the docs index with the current source-backed contracts.
- Produce a short handoff summary of confirmed doc truth and remaining doc
  uncertainty.
- Identify any historical rationale that still deserves preservation and move it
  into the appropriate `.hpp` contract comment or a small current-contract doc.

Excluded:

- Adding runtime integration tests.
- Refactoring `CoreState` access.
- Creating input/overlay behavioral tests.
- Running hardware validation.
- Changing persistence formats.
- Rewriting historical docs into implementation plans.
- Keeping historical docs visible simply because they once contained useful
  context.

## Sprint Inputs

Primary docs and source contracts to inspect:

- [`../README.md`](../README.md)
- [`../ARCHITECTURE_REVIEW_RULES.md`](../ARCHITECTURE_REVIEW_RULES.md)
- [`../../src/context/StandaloneContext.hpp`](../../src/context/StandaloneContext.hpp)
- [`../../src/context/standalone/StandaloneSequencerRuntimeGate.hpp`](../../src/context/standalone/StandaloneSequencerRuntimeGate.hpp)
- [`../../src/context/standalone/ActiveViewLifecyclePlan.hpp`](../../src/context/standalone/ActiveViewLifecyclePlan.hpp)
- [`../../src/sequencer/SequencerRuntimeService.hpp`](../../src/sequencer/SequencerRuntimeService.hpp)

Exploration evidence to use as navigation, not unquestioned truth:

- [`../_codex-exploration/codebase-map.md`](../_codex-exploration/codebase-map.md)
- [`../_codex-exploration/major-discoveries.md`](../_codex-exploration/major-discoveries.md)
- [`../_codex-exploration/analysis-readiness.md`](../_codex-exploration/analysis-readiness.md)
- [`../_codex-exploration/remaining-dark-zones.md`](../_codex-exploration/remaining-dark-zones.md)
- Domain maps under `../_codex-exploration/domain-*.md`

Source seams to verify when docs disagree:

- [`../../main.cpp`](../../main.cpp)
- [`../../src/context/StandaloneContext.cpp`](../../src/context/StandaloneContext.cpp)
- [`../../src/context/StandaloneContext.hpp`](../../src/context/StandaloneContext.hpp)
- [`../../src/state/CoreState.hpp`](../../src/state/CoreState.hpp)
- [`../../src/sequencer/SequencerRuntimeService.hpp`](../../src/sequencer/SequencerRuntimeService.hpp)
- [`../../src/sequencer/SequencerRuntimeService.cpp`](../../src/sequencer/SequencerRuntimeService.cpp)

## Work Plan

### 1. Link And File Existence Audit

Goal: find links that point to missing files and references that claim removed
files/classes are active.

Commands:

```powershell
rg -n "\]\([^)]+\.md\)" docs -g "*.md"
```

Expected output:

- A small table of broken links and obsolete-seam references.
- A decision for each item: fix target, mark historical, remove from active
  index, or leave with explanation.

Known pre-cleanup candidate:

- `docs/README.md` referenced a missing action-strip spec before Sprint 0.

### 2. Document Classification

Goal: make every top-level doc's authority level obvious, then remove legacy
material from the visible entry path.

Temporary classification labels used during the cleanup:

- `current-contract`: source-backed and intended to guide current changes.
- `working-plan`: active or recently completed planning that may include
  implementation history.
- `historical-audit`: temporary classification during the audit only; not a
  desired final state in the standard docs entry path.
- `illustrative-guide`: tutorial material that must not be copied without
  checking live code.
- `retired-reference`: removed from the standard docs entry path; keep only if
  explicitly needed outside the normal developer path.

Expected output:

- A classification table in the handoff summary.
- Active top-level docs are current, small, and intentionally listed. Legacy
  docs are not presented as standard reading material.
- Useful legacy rationale is moved into current contracts or `.hpp` comments
  before the legacy doc is retired.

### 3. Current Runtime Story Check

Goal: resolve the docs conflict around standalone runtime ownership.

Facts to verify:

- `main.cpp` owns the live `SequencerRuntimeService`.
- `OpenControlApp::registerPreContextUpdateHook(...)` is the live update entry.
- `StandaloneContext::update()` does not tick the runtime.
- Removed runtime-registry references are retired unless the symbols still exist.

Commands:

```powershell
rg -n "SequencerRuntimeService|registerPreContext" main.cpp src docs -g "*.cpp" -g "*.hpp" -g "*.md"
```

Expected output:

- Current-contract docs point to the hook-based runtime owner.
- Historical docs either stop claiming old seams are current or are clearly
  labelled as historical.

### 4. Current Architecture Entry Path

Goal: make `docs/README.md` route readers in the right order.

Target shape:

1. Architecture chantiers / current portfolio.
2. Sprint 0 source-of-truth plan.
3. Review rules.
4. Code style.
5. Source headers for API/lifecycle contracts.

Expected output:

- `docs/README.md` no longer presents stale or missing docs as normative current
  architecture references.
- The "Start Here" section explains that durable architecture rationale belongs
  in `.hpp` contracts where possible.

### 5. Handoff Summary

Goal: leave Sprint 1 with a clean input and no visible legacy-doc ambiguity.

The summary should include:

- Confirmed current-contract docs.
- Retired legacy docs or links removed from the standard entry path.
- Retired or removed links.
- Open documentation uncertainties.
- Runtime-contract claims that Sprint 1 must verify with tests rather than docs.

Suggested artifact:

- `docs/architecture-chantiers/sprint-0-handoff.md`

## Risks

- Historical docs may contain useful rationale.
  Mitigation: extract the minimal durable "why" into the relevant `.hpp` or a
  current-contract doc, then retire the legacy document from the standard docs
  path.
- The docs may be updated to match an unverified code interpretation.
  Mitigation: every current-contract claim must cite a live source file, symbol,
  command result, or focused exploration map.
- Sprint 0 may expand into refactoring.
  Mitigation: any non-doc change becomes a follow-up ticket for Sprint 1+.

## Completion Criteria

Sprint 0 is done when:

- All top-level docs are classified by authority level.
- Broken Markdown links in active docs are fixed, removed, or explicitly marked
  retired.
- Legacy docs are removed from the visible standard entry path; none are kept as
  normal reading material.
- The standalone runtime story in current-contract docs matches the current
  source-backed owner/update path.
- `docs/README.md` gives a low-friction entry path for a new contributor.
- A handoff note lists the exact doc facts Sprint 1 can trust and the runtime
  facts Sprint 1 still needs to prove with tests.

## Recommended Validation

Minimum:

```powershell
rg -n "\]\([^)]+\.md\)" docs -g "*.md"
rg -n "SequencerRuntimeService|registerPreContext" main.cpp src docs -g "*.md" -g "*.hpp" -g "*.cpp"
```

Manual checks:

- Every remaining match for old runtime seams is explicitly historical.
- Every active link in `docs/README.md` points to an existing file.
- No practical guide is presented as the source of truth for current constructor
  signatures, filenames, or composition wiring.
- Any preserved "why" from retired docs has a current home in `.hpp` contract
  comments or current-contract docs.
