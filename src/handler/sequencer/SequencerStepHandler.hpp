#pragma once

/**
 * @file SequencerStepHandler.hpp
 * @brief Standalone sequencer step editing bindings
 */

#include <cstdint>
#include <vector>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/Signal.hpp>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

/**
 * v0 bindings (sequencer view scope):
 * - MACRO_1..MACRO_8 release: toggle step in current page
 * - NAV turn: page switch (wrap)
 * - NAV hold + turn: switch track
 * - NAV long press: enter page/track selection mode
 *
 * Bottom action buttons are handled separately by SequencerRangeActionHandler.
 */
class SequencerStepHandler {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        core::state::StructureClipboardState& structureClipboard;
        core::state::CoreState& coreState;
    };

    SequencerStepHandler(StateRefs state,
                        oc::api::EncoderAPI& encoders,
                        oc::api::ButtonAPI& buttons,
                        oc::type::ScopeID scopeId);

    ~SequencerStepHandler() = default;

    SequencerStepHandler(const SequencerStepHandler&) = delete;
    SequencerStepHandler& operator=(const SequencerStepHandler&) = delete;
    SequencerStepHandler(SequencerStepHandler&&) = delete;
    SequencerStepHandler& operator=(SequencerStepHandler&&) = delete;

private:
    void bindStateSync();
    void setupBindings();

    void toggleStep(uint8_t indexInPage);
    void cycleNavigationFocus();
    void movePage(float delta);
    void moveTrack(float delta);
    void eraseCurrentStructure();
    void removeCurrentStructure();
    void copyCurrentStructure();
    void pasteCurrentStructure();
    void createPreviewedStructure();
    bool canRemoveCurrentStructure() const;
    bool canPasteCurrentStructure() const;
    void beginHoldAction(core::state::StructureHoldAction action);
    void clearHoldAction();
    void enterSelectionMode(core::state::StructureSelectionScope scope);
    void cancelSelectionMode();
    void toggleSelectionAtCursor();
    void navigateSelection(float delta);
    void deleteSelection();
    void duplicateSelection();
    bool createPage();
    bool createTrack();
    void setPagePreview(uint8_t pageIndex, bool addSlot);
    void setTrackPreview(uint8_t trackIndex, bool addSlot);
    uint8_t cursorForFocus(core::state::StructureNavigationFocus focus) const;
    uint8_t cursorForSelectionScope(core::state::StructureSelectionScope scope) const;
    void syncPreviewToFocus(core::state::StructureNavigationFocus focus);
    uint16_t currentTrackEnabledMask() const;
    uint8_t currentActiveTrack() const;
    bool applyTrackState(uint16_t enabledMask, uint8_t activeTrack);
    void prevPage();
    void nextPage();

    core::state::CoreState& core_state_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::StructureClipboardState& structure_clipboard_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    std::vector<oc::state::Subscription> subscriptions_;
    oc::type::ScopeID scope_id_ = 0;
    bool nav_long_press_used_ = false;
    bool ignore_next_bottom_left_release_ = false;
    bool ignore_next_bottom_right_release_ = false;
};

}  // namespace core::handler
