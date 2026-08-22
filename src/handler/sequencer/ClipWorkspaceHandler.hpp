#pragma once

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "app/OverlayTypes.hpp"
#include "state/CoreState.hpp"

namespace core::handler {

class SequencerPatternEditorHandler;
class ProjectTrackEditorHandler;
class SequencerStructureNavigationWorkflow;

/** Owns the first-rank Clips matrix bindings and structural actions. */
class ClipWorkspaceHandler {
public:
    struct StateRefs {
        core::state::CoreState& core;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>&
            navigationFocus;
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
    };

    ClipWorkspaceHandler(
        StateRefs state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID scopeId
    );

    void attachPatternEditorHandler(SequencerPatternEditorHandler& handler);
    void attachTrackEditorHandler(ProjectTrackEditorHandler& handler);
    void attachTrackNavigationWorkflow(
        SequencerStructureNavigationWorkflow& navigation
    );
    void update();

    [[nodiscard]] bool matrixAvailable() const;
    [[nodiscard]] bool operationBackAvailable() const;
    [[nodiscard]] bool focusedClipAvailable() const;
    void move(float delta);
    void moveViewport(int direction);
    void selectFocused();
    void openFocused();
    void launchVisible(uint8_t macroIndex);
    void beginMove();
    void applyOrBeginDuplicate();
    void beginRemove(uint32_t nowMs);
    void applyRemove();
    void endRemove();
    [[nodiscard]] bool prepareFocusedEditor();
    void back();

private:
    void setupBindings();
    [[nodiscard]] core::state::sequencer::SequencerClipAddress
    visibleAddress(uint8_t macroIndex) const;
    [[nodiscard]] core::state::sequencer::SequencerClipAddress
    sourceAddress() const;
    [[nodiscard]] uint8_t firstEmptySlotAfter(
        core::state::sequencer::SequencerClipAddress source
    ) const;
    bool enterClip(core::state::sequencer::SequencerClipAddress address);
    bool selectClipForEditing(
        core::state::sequencer::SequencerClipAddress address
    );
    bool selectTrack(uint8_t track);
    [[nodiscard]] bool trackSelectionActive() const;
    [[nodiscard]] bool trackHeaderAvailable() const;
    void beginTrackSelection();
    void syncNavigationFocus();

    core::state::CoreState& core_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>&
        navigation_focus_;
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    SequencerPatternEditorHandler* pattern_editor_handler_ = nullptr;
    ProjectTrackEditorHandler* track_editor_handler_ = nullptr;
    SequencerStructureNavigationWorkflow* navigation_workflow_ = nullptr;
};

}  // namespace core::handler
