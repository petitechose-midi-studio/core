#pragma once

#include <cstdint>

#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "app/OverlayTypes.hpp"
#include "state/CoreState.hpp"

namespace core::handler {

/** Clip-launcher navigation and actions, routed by the shared Sequencer bindings. */
class SequencerClipLauncherWorkflow {
public:
    struct StateRefs {
        core::state::CoreState& core;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>&
            navigationFocus;
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
    };

    explicit SequencerClipLauncherWorkflow(StateRefs state);

    [[nodiscard]] bool launcherAvailable() const;
    [[nodiscard]] bool patternBackAvailable() const;
    [[nodiscard]] bool operationBackAvailable() const;
    [[nodiscard]] bool focusedClipAvailable() const;
    void move(float delta);
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

    core::state::CoreState& core_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>&
        navigation_focus_;
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
};

}  // namespace core::handler
