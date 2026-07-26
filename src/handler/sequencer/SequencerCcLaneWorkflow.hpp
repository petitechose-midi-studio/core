#pragma once

#include <cstdint>

#include "handler/sequencer/SequencerCcLaneDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/StatusBarState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::handler {

class MidiCcGlobalFrameCoordinator;

class SequencerCcLaneWorkflow {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& editor;
        core::state::sequencer::SequencerTrackBankState& tracks;
        const core::state::project::ProjectNavigationState& projectNavigation;
        SequencerHistoryDomainServices history;
        core::state::StatusBarState& statusBar;
        const MidiCcGlobalFrameCoordinator* midiCcCoordinator = nullptr;
    };

    SequencerCcLaneWorkflow(StateRefs state, SequencerCcLaneDomainServices services);

    void openLaneSelector();
    bool openLane(uint8_t lane);
    bool createDefaultLane(uint32_t nowMs);
    void suspendGridForPropertySelector(uint32_t nowMs);
    void closeOneLevel(uint32_t nowMs);
    void moveSelector(float delta);
    bool activateSelector(uint32_t nowMs = 0);
    bool openSettings();
    void moveDraftField(float delta);
    bool activateDraftField();
    void editDraft(float delta);
    void moveFocusedStep(float delta, uint32_t nowMs);
    bool focusStep(uint8_t step, uint32_t nowMs);
    bool toggleFocusedEvent(uint32_t nowMs);
    bool editFocusedEvent(float delta, uint32_t nowMs);
    bool editVisibleEvent(uint8_t indexInWindow, float normalized, uint32_t nowMs);
    bool toggleVisibleEvent(uint8_t indexInWindow, uint32_t nowMs);
    bool openTransitionPicker(uint8_t indexInWindow, uint32_t nowMs);
    bool openFocusedTransitionPicker(uint32_t nowMs);
    void moveTransition(float delta);
    bool selectTransitionNormalized(float normalized);
    bool applyTransition(uint32_t nowMs);
    void cancelTransition();

    bool executeTap(
        core::state::sequencer::SequencerCcLaneActionSlot slot,
        uint32_t nowMs
    );
    bool beginGuard(
        core::state::sequencer::SequencerCcLaneActionSlot slot,
        uint32_t nowMs
    );
    bool releaseGuard(
        core::state::sequencer::SequencerCcLaneActionSlot slot,
        uint32_t nowMs
    );
    void update(uint32_t nowMs);
    bool commitEventEdit(uint32_t nowMs);

    [[nodiscard]] uint8_t selectorItemCount() const;
    [[nodiscard]] bool selectorFocusesAdd() const;
    [[nodiscard]] int8_t selectorLane() const;
    void refreshProjection();

private:
    using PatternChangePtr =
        core::state::sequencer::SequencerHistoryPatternChangePtr;
    using LaneBankPtr = core::state::sequencer::SequencerCcLaneBankPtr;

    PatternChangePtr prepareChange_(
        core::state::sequencer::SequencerHistoryActionKind kind,
        uint8_t lane,
        uint8_t step = core::state::sequencer::SequencerHistoryDescriptor::INVALID_INDEX
    );
    bool captureAfterFromBank_(
        core::state::sequencer::SequencerHistoryPatternChange& change,
        const core::state::sequencer::SequencerCcLaneBank* bank
    );
    bool installPreparedChange_(PatternChangePtr change, LaneBankPtr bank);
    bool stageCurrentBank_(LaneBankPtr& out, bool materializeEmpty) const;
    bool applySettings_(bool macroConflictAuthorized, uint32_t nowMs);
    bool clearFocusedEvent_(uint32_t nowMs);
    bool setFocusedEventValue_(uint8_t value, uint32_t nowMs);
    bool focusVisibleStep_(uint8_t indexInWindow, uint32_t nowMs);
    bool openTransitionPickerForFocused_(bool compact, uint32_t nowMs);
    bool removeCurrentLane_(uint32_t nowMs);
    void openGrid_(uint8_t lane);
    void loadSettingsDraft_();
    void refreshActions_(const SequencerCcLanePreflight& preflight);
    void refreshValueProjection_();
    void publishFeedback_(
        core::state::contextual::ContextActionId action,
        core::state::contextual::OperationFeedbackStatus status,
        core::state::contextual::ContextActionReason reason,
        core::state::contextual::OperationFeedbackExpiryPolicy expiry,
        uint32_t nowMs,
        uint32_t durationMs = 0
    );
    void block_(
        core::state::contextual::ContextActionId action,
        core::state::contextual::ContextActionReason reason,
        uint32_t nowMs
    );
    static int direction_(float delta);

    core::state::sequencer::SequencerState& editor_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    const core::state::project::ProjectNavigationState& project_navigation_;
    SequencerHistoryDomainServices history_;
    core::state::StatusBarState& status_bar_;
    const MidiCcGlobalFrameCoordinator* midi_cc_coordinator_ = nullptr;
    SequencerCcLaneDomainServices services_;
    bool last_transport_playing_ = false;
    core::state::sequencer::SequencerCcLaneActionSlot guard_slot_ =
        core::state::sequencer::SequencerCcLaneActionSlot::BOTTOM_RIGHT;
};

}  // namespace core::handler
