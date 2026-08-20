#include "context/standalone/SequencerOverlayPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"
#include "ui/interaction/SelectorPresentationPolicy.hpp"
#include "ui/interaction/TextKeyboardView.hpp"
#include "ui/sequencer/SequencerChordVoiceRail.hpp"
#include "ui/sequencer/SequencerPatternPresetPreview.hpp"
#include "ui/sequencer/SequencerStepEditOverlay.hpp"
#include "ui/font/StandaloneFonts.hpp"

namespace core::context::standalone {

FLASHMEM SequencerOverlayPresenter::~SequencerOverlayPresenter() {}

FLASHMEM SequencerOverlayPresenter::SequencerOverlayPresenter(
    StateRefs stateRefs,
    core::ui::SequencerStepEditOverlay& stepEditOverlay,
    core::ui::ContextActionStrip& stepEditActionStrip,
    ms::ui::VirtualListSelectorOverlay& presetLibraryOverlay,
    core::ui::ContextActionStrip& presetLibraryActionStrip,
    core::ui::SequencerChordVoiceRail& presetLibraryChordVoiceRail,
    core::ui::sequencer::SequencerPatternPresetPreview&
        presetLibraryPatternPreview,
    core::ui::interaction::TextKeyboardView& presetLibraryKeyboard
)
    : state_refs_(stateRefs)
    , step_edit_overlay_(stepEditOverlay)
    , step_edit_action_strip_(stepEditActionStrip)
    , preset_library_overlay_(presetLibraryOverlay)
    , preset_library_action_strip_(presetLibraryActionStrip)
    , preset_library_chord_voice_rail_(presetLibraryChordVoiceRail)
    , preset_library_pattern_preview_(presetLibraryPatternPreview)
    , preset_library_keyboard_(presetLibraryKeyboard)
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("SequencerOverlay"),
          &SequencerOverlayPresenter::drainRenderQueue,
          this
      ) {}

FLASHMEM bool SequencerOverlayPresenter::bind() {
    bool bound = render_scheduler_.valid();
    step_edit_watcher_.bind<&SequencerOverlayPresenter::requestStepEditRender>(
        *this, 0, "SequencerOverlay.stepEdit"
    );
    bound = step_edit_watcher_.watchAll(
        state_refs_.sequencer.stepEdit.visible,
        state_refs_.sequencer.stepEdit.stepIndex,
        state_refs_.sequencer.stepEdit.focusedRow,
        state_refs_.sequencer.stepEdit.localVariationEditActive,
        state_refs_.sequencer.stepEdit.chordEditor.active,
        state_refs_.sequencer.stepEdit.chordEditor.focusedField,
        state_refs_.sequencer.stepEdit.chordEditor.subEditor,
        state_refs_.sequencer.presetLibrary.visible,
        state_refs_.sequencer.pattern.enabledMask,
        state_refs_.sequencer.pattern.stepDataRevision,
        state_refs_.sequencer.pattern.patternScaleRevision,
        state_refs_.sequencer.pattern.graphRevision,
        state_refs_.sequencer.contentView.revision,
        state_refs_.tracks.projectScaleRevisionSignal()
    ) && bound;
    bound = step_edit_watcher_.watch(
        state_refs_.tracks.drumRevisionSignal()
    ) && bound;
    bound = step_edit_watcher_.watch(
        state_refs_.sequencer.drumSequencer.revision
    ) && bound;
    step_edit_action_watcher_.bind<&SequencerOverlayPresenter::requestStepEditActionsRender>(
        *this, 1, "SequencerOverlay.stepEditActions"
    );
    bound = step_edit_action_watcher_.watchAll(
        state_refs_.structureClipboard.revision,
        state_refs_.sequencer.stepEdit.contextHold.action,
        state_refs_.sequencer.stepEdit.contextHold.startedAtMs
    ) && bound;
    preset_library_watcher_.bind<&SequencerOverlayPresenter::requestPresetLibraryRender>(
        *this, 2, "SequencerOverlay.presetLibrary"
    );
    bound = preset_library_watcher_.watchAll(
        state_refs_.sequencer.presetLibrary.visible,
        state_refs_.sequencer.presetLibrary.libraryKind,
        state_refs_.sequencer.presetLibrary.mode,
        state_refs_.sequencer.presetLibrary.selectedIndex,
        state_refs_.sequencer.presetLibrary.entryCount,
        state_refs_.sequencer.presetLibrary.truncated,
        state_refs_.sequencer.presetLibrary.hasPreviousPage,
        state_refs_.sequencer.presetLibrary.hasNextPage,
        state_refs_.sequencer.presetLibrary.totalEntryCount,
        state_refs_.sequencer.presetLibrary.detailVisible,
        state_refs_.sequencer.presetLibrary.detailFocus,
        state_refs_.sequencer.presetLibrary.feedback,
        state_refs_.sequencer.presetLibrary.operationFeedback,
        state_refs_.sequencer.presetLibrary.actionGuard,
        state_refs_.sequencer.presetLibrary.revision
    ) && bound;
    preset_library_action_watcher_.bind<&SequencerOverlayPresenter::requestPresetLibraryActionsRender>(
        *this, 3, "SequencerOverlay.presetLibraryActions"
    );
    bound = preset_library_action_watcher_.watchAll(
        state_refs_.sequencer.presetLibrary.visible,
        state_refs_.sequencer.presetLibrary.mode,
        state_refs_.sequencer.presetLibrary.selectedIndex,
        state_refs_.sequencer.presetLibrary.entryCount,
        state_refs_.sequencer.presetLibrary.hasPreviousPage,
        state_refs_.sequencer.presetLibrary.revision,
        state_refs_.sequencer.presetLibrary.actionGuard,
        state_refs_.sequencer.presetLibrary.operationFeedback
    ) && bound;
    return bound;
}

FLASHMEM void SequencerOverlayPresenter::requestStepEditRender() {
    render_scheduler_.request(RENDER_STEP_EDIT | RENDER_STEP_EDIT_ACTIONS);
}

FLASHMEM void SequencerOverlayPresenter::requestStepEditActionsRender() {
    render_scheduler_.request(RENDER_STEP_EDIT_ACTIONS);
}

FLASHMEM void SequencerOverlayPresenter::requestPresetLibraryRender() {
    render_scheduler_.request(RENDER_PRESET_LIBRARY | RENDER_PRESET_LIBRARY_ACTIONS);
}

FLASHMEM void SequencerOverlayPresenter::requestPresetLibraryActionsRender() {
    render_scheduler_.request(RENDER_PRESET_LIBRARY_ACTIONS);
}

FLASHMEM void SequencerOverlayPresenter::drainRenderQueue(void* context, uint32_t flags) {
    auto* self = static_cast<SequencerOverlayPresenter*>(context);
    if (self) self->renderPending(flags);
}

FLASHMEM void SequencerOverlayPresenter::renderPending(uint32_t flags) {
    if ((flags & RENDER_STEP_EDIT) != 0) renderStepEdit();
    if ((flags & RENDER_STEP_EDIT_ACTIONS) != 0) renderStepEditActionStrip();
    if ((flags & RENDER_PRESET_LIBRARY) != 0) renderPresetLibrary();
    if ((flags & RENDER_PRESET_LIBRARY_ACTIONS) != 0) renderPresetLibraryActionStrip();
}

FLASHMEM void SequencerOverlayPresenter::renderStepEdit() {
    if (state_refs_.sequencer.drumSequencer.laneEditor.active) return;
    core::context::standalone::sequencer_overlay_presenter::StepEditRenderData data{};
    core::context::standalone::sequencer_overlay_presenter::buildStepEditRenderData(
        {
            state_refs_.sequencer,
            state_refs_.tracks,
        },
        data
    );
    if (!data.visible || state_refs_.sequencer.presetLibrary.visible.get()) {
        step_edit_overlay_.render({.visible = false});
    } else {
        step_edit_overlay_.render(data.overlayProps);
    }
}

FLASHMEM void SequencerOverlayPresenter::renderStepEditActionStrip() {
    if (state_refs_.sequencer.drumSequencer.laneEditor.active) return;
    if (!state_refs_.sequencer.stepEdit.visible.get()) {
        step_edit_action_strip_.render({.visible = false});
    } else {
        step_edit_action_strip_.render(
            core::context::standalone::sequencer_overlay_presenter::buildStepEditActionStripProps({
                state_refs_.sequencer,
                state_refs_.tracks,
                state_refs_.structureClipboard,
            })
        );
    }
}

FLASHMEM void SequencerOverlayPresenter::renderPresetLibrary() {
    preset_library_render_data_ =
        core::context::standalone::sequencer_overlay_presenter::
            buildPresetLibraryRenderData({
                state_refs_.sequencer,
                state_refs_.tracks,
            });
    const auto& data = preset_library_render_data_;
    if (!data.visible) {
        preset_library_overlay_.render({.visible = false});
        preset_library_chord_voice_rail_.render({.visible = false});
        preset_library_pattern_preview_.render({.visible = false});
        preset_library_keyboard_.setVisible(false);
        return;
    }

    const auto& picker = state_refs_.sequencer.presetLibrary;
    const bool textEditing =
        picker.libraryKind.get() ==
            core::state::sequencer::SequencerPresetLibraryKind::PATTERN &&
        picker.pattern().textEdit !=
            core::state::sequencer::SequencerPatternPresetTextEdit::NONE;
    if (textEditing) {
        const auto& pattern = picker.pattern();
        preset_library_overlay_.setContentVisible(false);
        preset_library_chord_voice_rail_.render({.visible = false});
        preset_library_pattern_preview_.render({.visible = false});
        preset_library_keyboard_.render({
            .visible = true,
            .title = pattern.textEdit ==
                    core::state::sequencer::
                        SequencerPatternPresetTextEdit::RENAME
                ? "Rename"
                : "New folder",
            .meta = pattern.location.root()
                ? "User"
                : pattern.location.relativeDirectory.data(),
            .name = pattern.textDraft.data(),
            .selectedKey = pattern.textKeyIndex,
            .shiftActive = pattern.textShiftActive,
        });
        return;
    }
    preset_library_keyboard_.setVisible(false);
    preset_library_overlay_.setContentVisible(true);

    auto props = core::ui::interaction::decisionSelectorProps(
        data.title.data(),
        data.meta.data(),
        data.items.data(),
        data.itemCount,
        data.selectedIndex,
        data.dataRevision
    );
    props.breadcrumb = data.breadcrumb.data();
    props.icons = data.itemIcons.data();
    props.values = data.itemValues.data();
    props.iconColors = data.itemIconColors.data();
    props.iconFont = standalone_fonts.icons_16;
    props.metaIcon = data.headerIcon;
    props.metaIconFont = standalone_fonts.icons_12;
    props.metaIconColor = data.headerIconColor;
    props.showIndexColumn = data.showIndexColumn;
    preset_library_overlay_.render(props);
    preset_library_chord_voice_rail_.render(data.chordVoiceRail);
    if (data.patternPreviewVisible) {
        const auto& pattern = state_refs_.sequencer.presetLibrary.pattern();
        preset_library_pattern_preview_.render({
            .descriptor = &pattern.descriptor,
            .revision = data.dataRevision,
            .visible = true,
        });
    } else {
        preset_library_pattern_preview_.render({.visible = false});
    }
}

FLASHMEM void SequencerOverlayPresenter::renderPresetLibraryActionStrip() {
    if (!state_refs_.sequencer.presetLibrary.visible.get()) {
        preset_library_action_strip_.render({.visible = false});
        return;
    }

    const auto& picker = state_refs_.sequencer.presetLibrary;
    if (picker.libraryKind.get() ==
            core::state::sequencer::SequencerPresetLibraryKind::PATTERN &&
        picker.pattern().textEdit !=
            core::state::sequencer::SequencerPatternPresetTextEdit::NONE) {
        preset_library_action_strip_.render(
            core::ui::interaction::TextKeyboardView::
                bottomActionStripProps(true, false)
        );
        return;
    }

    preset_library_action_strip_.render(
        core::context::standalone::sequencer_overlay_presenter::
            buildPresetLibraryActionStripProps({
                state_refs_.sequencer,
                state_refs_.tracks,
            })
    );
}

}  // namespace core::context::standalone
