#pragma once

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "handler/common/ButtonReleaseLatch.hpp"
#include "handler/common/PressHoldTurnReleaseGesture.hpp"
#include "state/StructureNavigationState.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/sequencer/SequencerClipGridState.hpp"
#include "state/sequencer/SequencerClipLaunchQueue.hpp"
#include "state/sequencer/SequencerUiState.hpp"

namespace core::state {
struct StatusBarState;
struct TrackNavigationState;
}

namespace core::state::project {
struct ProjectTrackState;
}

namespace core::state::sequencer {
struct ClipWorkspaceUiState;
struct DrumSequencerState;
struct SequencerState;
struct SequencerTrackPasteUiState;
}

namespace core::handler {

class ProjectTrackEditorHandler;
class SequencerStructureNavigationWorkflow;

/**
 * Owns the first-rank Clips matrix bindings and structural actions.
 *
 * The handler reads targeted state slices and routes every state-changing Clip
 * operation through composition-supplied thunks, so it never sees the full
 * CoreState aggregate.
 */
class ClipWorkspaceHandler {
public:
    struct Refs {
        struct Ops {
            void* context = nullptr;
            bool (*createClip)(void*, core::state::sequencer::SequencerClipAddress) = nullptr;
            bool (*switchClipForEditing)(void*, core::state::sequencer::SequencerClipAddress) = nullptr;
            bool (*deleteClip)(void*, core::state::sequencer::SequencerClipAddress) = nullptr;
            bool (*duplicateClip)(void*,
                                  core::state::sequencer::SequencerClipAddress,
                                  core::state::sequencer::SequencerClipAddress) = nullptr;
            bool (*moveClips)(void*,
                              const core::state::sequencer::SequencerClipSelectionMask&,
                              int8_t,
                              int8_t) = nullptr;
            bool (*setStopSlot)(void*,
                                core::state::sequencer::SequencerClipAddress,
                                bool) = nullptr;
            bool (*setClipBehavior)(void*,
                                    core::state::sequencer::SequencerClipAddress,
                                    core::state::sequencer::SequencerLauncherBehavior) = nullptr;
            bool (*setSceneBehavior)(void*,
                                     uint8_t,
                                     core::state::sequencer::SequencerLauncherBehavior) = nullptr;
            bool (*requestClipLaunch)(void*,
                                      core::state::sequencer::SequencerClipAddress,
                                      core::state::sequencer::SequencerClipLaunchQuantization) = nullptr;
            bool (*requestTrackStop)(void*,
                                     uint8_t,
                                     core::state::sequencer::SequencerClipLaunchQuantization) = nullptr;
            bool (*requestSceneLaunch)(void*,
                                       uint8_t,
                                       core::state::sequencer::SequencerClipLaunchQuantization) = nullptr;
            bool (*setSharedTrackState)(void*, uint16_t, uint8_t) = nullptr;
        };

        core::state::sequencer::ClipWorkspaceUiState& clipWorkspace;
        core::state::sequencer::SequencerTrackPasteUiState& trackPaste;
        core::state::sequencer::DrumSequencerState& drumSequencer;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerClipGridState& sequencerClips;
        core::state::sequencer::SequencerTrackBankState& sequencerTracks;
        core::state::sequencer::SequencerClipLaunchQueue& sequencerClipLaunches;
        core::state::TrackNavigationState& trackNavigation;
        core::state::StatusBarState& statusBar;
        oc::state::Signal<uint8_t, 8>& sharedTrackActive;
        oc::state::Signal<uint16_t, 16>& sharedTrackEnabledMask;
        core::state::project::ProjectTrackState& projectTracks;
        core::state::project::ProjectTrackDomainServices projectTrackDomain;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>&
            navigationFocus;
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        Ops ops;
    };

    ClipWorkspaceHandler(
        Refs refs,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID scopeId
    );

    void attachTrackEditorHandler(ProjectTrackEditorHandler& handler);
    void attachTrackNavigationWorkflow(
        SequencerStructureNavigationWorkflow& navigation
    );
    void update();

    [[nodiscard]] bool matrixAvailable() const;
    [[nodiscard]] bool editorAvailable() const;
    [[nodiscard]] bool horizontalNavigationAvailable() const;
    [[nodiscard]] bool quickSelectorAvailable() const;
    [[nodiscard]] bool directPatternAvailable() const;
    [[nodiscard]] bool operationBackAvailable() const;
    [[nodiscard]] bool focusedClipAvailable() const;
    void move(float delta);
    void edit(float delta);
    void moveViewport(int direction);
    void selectFocused();
    void openFocused();
    void openFocusedEditor();
    void launchVisible(uint8_t macroIndex);
    void beginMove();
    void applyOrBeginDuplicate();
    void beginRemove(uint32_t nowMs);
    void applyRemove();
    void endRemove();
    void back();

private:
    void setupBindings();
    void beginHorizontalNavigation();
    void moveHorizontal(float delta);
    void releaseHorizontalNavigation();
    void beginQuickSelector();
    void moveQuickSelector(float delta);
    void releaseQuickSelector();
    void openFocusedPattern();
    void editQuickProperty(float delta);
    void editEditorValue(float normalized);
    void confirmSlotAction();
    [[nodiscard]] core::state::sequencer::SequencerClipAddress
    sourceAddress() const;
    [[nodiscard]] uint8_t lastNavigableScene() const;
    void launchScene(uint8_t slot);
    void stopTrack(uint8_t track, bool immediate);
    void stopFocusedTrack();
    void beginStopLayer();
    void endStopLayer();
    void toggleTrackMute();
    void toggleTrackSolo();
    void showFeedback(
        core::state::sequencer::ClipWorkspaceFeedback feedback
    );
    void finishPendingRemove();
    bool enterClip(core::state::sequencer::SequencerClipAddress address);
    bool selectClipForEditing(
        core::state::sequencer::SequencerClipAddress address
    );
    bool selectTrack(uint8_t track);
    [[nodiscard]] bool trackSelectionActive() const;
    [[nodiscard]] bool trackHeaderAvailable() const;
    void beginTrackSelection();
    void syncNavigationFocus();

    Refs refs_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    ProjectTrackEditorHandler* track_editor_handler_ = nullptr;
    SequencerStructureNavigationWorkflow* navigation_workflow_ = nullptr;
    PressHoldTurnReleaseGesture horizontal_navigation_gesture_{};
    PressHoldTurnReleaseGesture quick_selector_gesture_{};
    ButtonReleaseLatch<2> release_latch_;
};

}  // namespace core::handler
