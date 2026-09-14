# Project history: admission and ownership

Start with `CoreStateProjectHistory.cpp` when changing global Undo/Redo.
The View Selector and any future command route must use
`CoreState::undoProjectHistory()` / `redoProjectHistory()`.

## One command boundary

1. `projectHistoryBlockReason()` reads current owners. It allocates nothing,
   publishes nothing and never seals an edit. Hidden parents remain protected.
2. Execution repeats admission before committing coalesced live edits. A Step
   Content draft also receives its existing blocked-transition feedback.
3. The Project coordinator supplies the next chronological entry. The domain
   must still have that exact payload identity before applying the entry.
4. The domain reconciles musical state, runtime and editor targets. An owner
   removed by history closes through that existing reconciliation.

`ProjectHistoryCoordinator` owns chronology and retention admission. Macro,
Sequencer, Settings and Track histories own their payloads. They do not decide
whether a global user command may interrupt a local interaction.

## Admission contract

| Owner | Global command |
|---|---|
| Step Content, detached Quick Controls, CC settings/transition, Drum Lane, Track name/type draft | Finish or cancel the draft locally first. |
| Modulator audition, Macro take or Recorded Shape capture | Finish the audition/capture first, including an armed take. |
| Selection/placement, contextual selector, action hold, Track paste details | Exit the selection or release the owning gesture first. |
| Pending Pattern preset preview | Apply or cancel the preview through its owner. |
| Macro editor, Pattern editor, preset browser, value-selector child, Project keyboard or creation flow | Finish the local editor first. These owners retain a buffer, preview or private publication boundary. |
| New/Load Project confirmation | Finish the Project action first. |
| Live CC grid, clean Track editor, live Step/Pitch editor, root performance edits | Seal coalesced edits, then traverse exactly one chronological entry. |
| Published Track activation waiting for a musical boundary | Preserve the existing exact queued Undo/Redo transition. |

The last two cases differ from an uncommitted preview. Autosave requests and
performance launch queues are not blanket blockers: their existing session,
revision and activation contracts remain authoritative.

An overlay alone is not a transaction classification. Admission first checks
semantic owners, including parents hidden by children. The final overlay list
fails closed for an unqualified new surface. Pattern Randomize, for example,
has a private session in its feature module, so global history requires leaving
the Pattern editor. A broader live-editor shortcut needs qualification of that
owner's publication boundary before changing this contract.

## Input and presentation

`ViewSwitcherHandler` owns root routing and physical button checks. An
already-held button prevents opening the selector, and Back below a root keeps
its local meaning. At a root, held `LEFT_TOP`, then `LEFT_CENTER` / `LEFT_BOTTOM`
provides momentary Undo/Redo. Releasing `LEFT_TOP` returns to the same view if
NAV has not selected another one. The opposite press order keeps the first
local action's ownership.

In Clips, an armed quick property owns the first Back. That exits its live edit
mode without reverting the value; the next Back reaches the View Selector.

Labels use `CoreState::formatProjectHistoryLabel()` and the same admission as
execution. `StandaloneContext` checks for an admission change only while the
selector is visible. Its small presentation cache avoids a subscriber for each
transient state and unchanged redraws; it is not a replicated transaction.

Availability is advisory. Execution rechecks admission, the checked coalescing
outcome and exact history identity. A refusal consumes that attempt and never
retries automatically when a draft or gesture ends.

## Tests to extend

- `test_ProjectHistoryCoordinator`: pure admission, pending coalescing, exact
  cross-domain identities, eviction and memory limits; owners under all five views.
- `test_ViewSwitcherHandler`: held root Undo/Redo and admission changes between
  press and release, routing, view changes and empty history.
- CC Lane, Track Editor, Sequencer Step Handler and Macro Performance Handler:
  real draft/capture entry, rejected history, local exit, then exact Undo/Redo.

Domain isolation tests may deliberately create unrelated synthetic selectors.
Keep their isolation assertions, then settle that owner before testing a global
command. Product callers must not bypass admission.
