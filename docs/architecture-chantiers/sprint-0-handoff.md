# Sprint 0 Handoff

Updated: 2026-04-29

Purpose: record the documentation cleanup facts that later sprints can trust.

Current status: complete for the tracked documentation entry path. Sprint 1 has
consumed this handoff and completed its scoped Gates 1-6 tranche. Sprints 2 and
3 completed their scoped handler and shared-structure tranches. Sprint 4 has
completed its software persistence compatibility/failure-semantics tranche,
including Teensy main-loop recovery wiring; hardware SD hot-swap validation
remains future work.

## Confirmed Current Entry Path

- `docs/README.md` is the only top-level documentation entry point.
- `docs/architecture-chantiers/README.md` lists the codebase-scale chantiers and
  their current ordering.
- `docs/architecture-chantiers/sprint-0-documentation-source-of-truth.md`
  captures the Sprint 0 cleanup method.
- `docs/architecture-chantiers/hpp-contract-compliance-map.md` maps the current
  `.hpp` contract-documentation debt and the recommended cleanup order.
- `docs/architecture-chantiers/hpp-contract-compliance-tracker.md` tracks
  review status while the `.hpp` contracts are patched.
- `docs/ARCHITECTURE_REVIEW_RULES.md` remains the review checklist for
  architecture-sensitive changes.
- `docs/CODE_STYLE.md` remains the style and tooling reference.

## Source-Level Contracts Preserved

- `src/context/StandaloneContext.hpp` states that the context owns assembly and
  not the sequencer runtime execution path.
- `src/context/standalone/StandaloneSequencerRuntimeGate.hpp` states the
  standalone runtime hook decision rule.
- `src/context/standalone/ActiveViewLifecyclePlan.hpp` states that view
  activation side effects belong to the lifecycle seam, not presentation views.
- `src/sequencer/SequencerRuntimeService.hpp` states the single-owner runtime
  contract and the narrow `StateRefs` access rule.

## Retired Top-Level Docs

The Sprint 0 cleanup removed historical audits, completed refactor plans,
illustrative guides, and stale specs from the top-level docs set. Their durable
runtime/lifecycle rationale is now carried by the source headers listed above or
by the current architecture-chantiers portfolio.

## Local Exploration Notes

- `docs/_codex-exploration/` is intentionally excluded from Git.
- Treat any local files under that directory as scratch navigation evidence,
  not as tracked source of truth.
- Before promoting a local exploration claim into implementation work, verify it
  against current source files and `ms test core`.

## Sprint 1 Input

- Treat the single standalone runtime owner as a source-backed contract.
- Still verify behavior with tests: standalone active play should tick the
  runtime and advance the playhead through the live hook path.
- Do not reintroduce historical plans or audits as standard reading material.

## Validation Snapshot

- Tracked Markdown links were checked after the 2026-04-29 doc refresh.
- `ms test core` passed with `45/45` tests after the Sprint 4 persistence
  compatibility, failure-semantics, and recovery-foundation pass.
- `ms build core --target teensy` passed after wiring the main-loop SD recovery
  manager.
