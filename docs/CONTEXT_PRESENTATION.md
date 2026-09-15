# Context presentation

Start with the owner of an interaction, then follow its presenter to the view.
The screen describes an action; it does not decide whether that action is legal
or execute it. This keeps the displayed gesture and the handler on the same
domain policy.

## Where to change a behavior

| Change | Entry point |
|---|---|
| Which tap/hold is available, its target or admission | The feature's policy in `src/state`, used by its handler |
| Action words, glyphs and operation feedback | `src/ui/strip/ContextActionVisualProjection.hpp` |
| Shared outcome words and precise refusal causes | `src/ui/strip/ContextFeedbackPresentation.hpp` |
| Sequencer action vocabulary adapter | `src/ui/sequencer/SequencerActionStripVisuals.hpp` |
| Context-specific hint or action projection | The feature's presenter or view-model builder |
| Footer geometry, colors and text style | `src/ui/theme/StandaloneTheme.hpp` and `src/ui/strip/ContextActionStrip.cpp` |
| Transport indicators and their subscriptions | `src/ui/transportbar/TransportBar.cpp` |
| Ownership, overlay priority or visibility | `src/context/standalone/StandaloneUiAssembly.cpp`, `StandaloneOverlayAssembly.cpp` and the existing presentation registry |
| Drawing a bounded label or rectangle | `src/ui/common/ContextSurfaceDraw.hpp` |
| Compact parameter cards in Macro and Modulator | `src/ui/common/ParameterCard.hpp` |
| Title and status in Macro, Modulator and Track Editor | `src/ui/common/ContextHeader.hpp` |

For example, Macro's bottom-right release copies when copying is possible.
Holding can paste. `MacroViewModelBuilder` asks `MacroInteractionPolicy` for
both actions, displays the release action at rest and projects the active hold.
Clipboard presence alone must not change the displayed release action to Paste.

## Composition at 320 by 240

`MainViewFrame` remains the root-view frame. The standalone main zone occupies
the display height. Its horizontal action strip reserves the last 40 pixels:

- y=200..219: context information or a useful gesture hint;
- y=220..239: the local bottom-left and bottom-right commands;
- x=102..217, y=220..239: the single global transport surface, above the local
  strip in display order.

The middle action-strip slot is information, such as a selection count. When
present, it takes precedence over the two hints. A running hold countdown takes
precedence over both. These choices only affect presentation.

Overlays call `alignToFooter()` on their strip. Their owner continues to decide
which overlay is visible. Do not add another navigation stack or transport bar
to a context. The view selector owns its Undo/Redo hint strip and uses the same
global transport as the other contexts.

Vertical strips retain the three physical left-hand controls. They prefer
glyphs to labels, while horizontal commands prefer words. `holdOnly` means
the command has no tap action and adds a Hold prefix at rest; it must come from
the actual gesture policy.

## State and rendering

`ContextActionStrip` and `TransportBar` each use one LVGL drawing object. Text is
copied into fixed-capacity buffers, without retaining caller-owned text
pointers. Byte truncation uses the existing UTF-8-aware `TextOverflow` helper;
drawing is clipped to each field's bounds. Glyph pointers must be static.

Identical properties do not invalidate the strip. The transport caches its
flags and formatted tempo and repaints only on change. Its opaque background
lets LVGL skip the local commands underneath when transport indicators change.
The strip's timer is paused outside a visible hold and only paints progress;
the handler remains responsible for committing or cancelling the action.

Keep specialized retained renderers for grids, curves and editors. Share the
bounded drawing primitives where their geometry really matches; do not rebuild
these surfaces into per-cell widget trees to obtain a common appearance.

`ParameterCard` owns one drawing object and bounded icon/label/value text. Its
caller owns the number, position and visibility of cards. The caller projects
focus, availability and activity from its domain policy into `ParameterCardVisual`;
the card cannot change a parameter. Macro's three domains and Modulator's changing
parameter layout use this same renderer. Their curve providers remain independent.

`ContextHeader` similarly retains a title, status and optional icons. The same
`drawContextHeader` function can draw into an existing surface, as Track Editor
does, without adding a widget. Both components copy temporary presenter text and
avoid invalidation for identical properties. Destroy these owners before deleting
their parent LVGL tree. Their text is bounded and clipped inside each field.

Draft state is also local to its owner. In Track Editor, Type can be pending
while Channel and Delay remain direct edits. A global Draft flag would erase
that distinction. Feedback text cannot close a transaction or alter encoder
ownership when it expires.

Operation results have two levels: the command keeps a short outcome, while
`describeContextFeedback` uses the full explanation row for its recorded cause.
The owner still controls visibility and expiry. A queued operation remains
visible until resolved; drawing it cannot acknowledge a refusal or cancel a
draft. Press/hold progress keeps precedence. Recovery gestures and details such
as loading into an editor or waiting for a loop boundary stay with the domain
that can establish those facts. Do not guess a cause from a generic failure.

## Verification

Run `ms test core` for action-policy and view-model contracts, then the real
LVGL suite in `test/lvgl_step_editor`. Its footer tests exercise caller-owned
text, hold timing and clock wrap, hidden views, incremental repaint, long
labels and transport isolation. `ms ux run core --all` covers the existing
interaction workflows and cross-context routing.

For performance, compare cold runs of the RAM-only hardware fixture on the
same device and build profile. Record dynamic LVGL memory separately from
static RAM, application PSRAM allocations, musical timing and display time.
Native object sizes are not measurements of Teensy heap consumption.

The shared footer is the implemented presentation base. A dedicated help
panel and further body changes remain separate work: a help entry must
first be checked against the existing physical gesture grammar.
