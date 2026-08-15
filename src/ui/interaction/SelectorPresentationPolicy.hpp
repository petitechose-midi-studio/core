#pragma once

#include <cstdint>

#include <lvgl.h>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "ui/theme/StandaloneListVisuals.hpp"

namespace core::ui::interaction {

/**
 * Canonical presentation for a modal choice list.
 *
 * Choice lists replace the surface below them: every option remains readable,
 * the selected row carries focus, and the opaque backdrop prevents unrelated
 * editor content from competing with the decision.
 */
constexpr ms::ui::VirtualListSelectorOverlayProps decisionSelectorProps(
    const char* title,
    const char* meta,
    const char* const* items,
    int itemCount,
    int selectedIndex,
    uint32_t dataRevision
) {
    return {
        .title = title,
        .meta = meta,
        .items = items,
        .itemCount = itemCount,
        .selectedIndex = selectedIndex,
        .showIndexColumn = false,
        .dimUnselected = false,
        .backdropOpacity = LV_OPA_COVER,
        .visible = true,
        .dataRevision = dataRevision,
        .visualTokens = &standalone::theme::CONTROLLER_LIST_VISUALS,
    };
}

}  // namespace core::ui::interaction
