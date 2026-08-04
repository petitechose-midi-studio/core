#pragma once

#include <oc/state/StaticSignalWatcher.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/StructureClipboardState.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}

namespace core::ui {
class SequencerChordVoiceRail;
class SequencerStepEditOverlay;
}

namespace core::context::standalone {

/**
 * Projects sequencer step-edit state into the step edit overlay.
 *
 * The presenter formats current step values and watches step-edit signals; it
 * does not apply edits or manage input bindings.
 */
class SequencerOverlayPresenter {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        core::state::StructureClipboardState& structureClipboard;
    };

    SequencerOverlayPresenter(StateRefs stateRefs,
                              core::ui::SequencerStepEditOverlay& stepEditOverlay,
                              core::ui::ContextActionStrip& stepEditActionStrip,
                              ms::ui::VirtualListSelectorOverlay& presetLibraryOverlay,
                              core::ui::ContextActionStrip& presetLibraryActionStrip,
                              core::ui::SequencerChordVoiceRail&
                                  presetLibraryChordVoiceRail);
    ~SequencerOverlayPresenter();

    [[nodiscard]] bool bind();

private:
    static constexpr uint32_t RENDER_STEP_EDIT = 1U << 0;
    static constexpr uint32_t RENDER_STEP_EDIT_ACTIONS = 1U << 1;
    static constexpr uint32_t RENDER_PRESET_LIBRARY = 1U << 2;
    static constexpr uint32_t RENDER_PRESET_LIBRARY_ACTIONS = 1U << 3;

    static void drainRenderQueue(void* context, uint32_t flags);
    void requestStepEditRender();
    void requestStepEditActionsRender();
    void requestPresetLibraryRender();
    void requestPresetLibraryActionsRender();
    void renderPending(uint32_t flags);
    void renderStepEdit();
    void renderStepEditActionStrip();
    void renderPresetLibrary();
    void renderPresetLibraryActionStrip();

    StateRefs state_refs_;
    core::ui::SequencerStepEditOverlay& step_edit_overlay_;
    core::ui::ContextActionStrip& step_edit_action_strip_;
    ms::ui::VirtualListSelectorOverlay& preset_library_overlay_;
    core::ui::ContextActionStrip& preset_library_action_strip_;
    core::ui::SequencerChordVoiceRail&
        preset_library_chord_voice_rail_;
    core::ui::CoalescedLvglRenderScheduler render_scheduler_;
    oc::state::StaticWatchGroup<14> step_edit_watcher_;
    oc::state::StaticWatchGroup<3> step_edit_action_watcher_;
    oc::state::StaticWatchGroup<16> preset_library_watcher_;
    oc::state::StaticWatchGroup<8> preset_library_action_watcher_;
};

}  // namespace core::context::standalone
