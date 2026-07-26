#pragma once

#include <cstdint>

namespace core::ui {

enum class ViewType : uint8_t {
    MACRO = 0,
    SEQUENCER,
    PROJECT,
    DEVICE_SETTINGS,
    /** First-rank musical view backed by the shared Project workspace. */
    MODULATORS,
    COUNT
};

[[nodiscard]] constexpr bool isProjectWorkspaceView(ViewType view) {
    return view == ViewType::PROJECT || view == ViewType::MODULATORS;
}

}  // namespace core::ui
