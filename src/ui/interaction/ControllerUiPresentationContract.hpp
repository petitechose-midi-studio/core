#pragma once

#include <cstdint>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "state/interaction/ControllerInteractionContract.hpp"

namespace core::ui::interaction {

enum class SurfaceChrome : uint8_t {
    ROOT = 0,
    EDITOR,
    MODAL,
};

struct SurfacePresentationContract {
    core::state::interaction::SurfaceArchetype archetype =
        core::state::interaction::SurfaceArchetype::HIERARCHICAL;
    core::state::interaction::TransactionMode transaction =
        core::state::interaction::TransactionMode::LIVE;
    SurfaceChrome chrome = SurfaceChrome::ROOT;
    bool valid = false;
};

constexpr SurfacePresentationContract presentationFor(core::ui::ViewType view) {
    using Archetype = core::state::interaction::SurfaceArchetype;
    using Transaction = core::state::interaction::TransactionMode;

    switch (view) {
        case core::ui::ViewType::MACRO:
            return {Archetype::PERFORMANCE, Transaction::LIVE, SurfaceChrome::ROOT, true};
        case core::ui::ViewType::SEQUENCER:
            return {Archetype::HIERARCHICAL, Transaction::LIVE, SurfaceChrome::ROOT, true};
        case core::ui::ViewType::PROJECT:
        case core::ui::ViewType::DEVICE_SETTINGS:
            return {Archetype::BROWSER, Transaction::LIVE, SurfaceChrome::ROOT, true};
        case core::ui::ViewType::MODULATORS:
            return {Archetype::RETAINED_EDITOR, Transaction::LIVE, SurfaceChrome::ROOT, true};
        case core::ui::ViewType::COUNT:
            return {};
    }
    return {};
}

constexpr SurfacePresentationContract presentationFor(core::ui::OverlayType overlay) {
    using Archetype = core::state::interaction::SurfaceArchetype;
    using Transaction = core::state::interaction::TransactionMode;

    switch (overlay) {
        case core::ui::OverlayType::MACRO_EDIT:
        case core::ui::OverlayType::MACRO_AUTOMATION:
        case core::ui::OverlayType::SEQ_STEP_EDIT:
        case core::ui::OverlayType::PATTERN_PITCH_SETTINGS:
            return {Archetype::RETAINED_EDITOR, Transaction::LIVE, SurfaceChrome::EDITOR, true};
        case core::ui::OverlayType::MACRO_EDIT_SELECTOR:
        case core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR:
        case core::ui::OverlayType::PATTERN_PITCH_SETTINGS_SELECTOR:
            return {Archetype::MOMENTARY_SELECTOR, Transaction::MOMENTARY, SurfaceChrome::MODAL, true};
        case core::ui::OverlayType::VIEW_SELECTOR:
        case core::ui::OverlayType::PRESET_LIBRARY:
            return {Archetype::BROWSER, Transaction::MOMENTARY, SurfaceChrome::MODAL, true};
        case core::ui::OverlayType::SEQ_CC_LANE:
        case core::ui::OverlayType::SEQ_PATTERN_EDIT:
        case core::ui::OverlayType::SEQ_TRACK_EDIT:
        case core::ui::OverlayType::SEQ_DRUM_LANE_EDIT:
            return {Archetype::TRANSACTIONAL_EDITOR, Transaction::DRAFT, SurfaceChrome::EDITOR, true};
        case core::ui::OverlayType::NONE:
        case core::ui::OverlayType::COUNT:
            return {};
    }
    return {};
}

constexpr bool replacesRootChrome(core::ui::OverlayType overlay) {
    const auto contract = presentationFor(overlay);
    return contract.valid && contract.chrome != SurfaceChrome::ROOT;
}

static_assert(sizeof(SurfaceChrome) == 1U);
static_assert(sizeof(SurfacePresentationContract) == 4U);

}  // namespace core::ui::interaction
