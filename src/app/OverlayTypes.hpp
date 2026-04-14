#pragma once

#include <cstdint>

namespace core::ui {

enum class OverlayType : uint8_t {
    NONE = 0,
    PAGE_SELECTOR,
    MACRO_EDIT,
    MACRO_EDIT_SELECTOR,
    MACRO_EDIT_MACRO_SELECTOR,
    VIEW_SELECTOR,
    SEQ_STEP_EDIT,
    GLOBAL_SETTINGS,
    GLOBAL_SETTINGS_SELECTOR,
    DATA_MANAGER,
    DATA_MANAGER_DIALOG,
    COUNT
};

}  // namespace core::ui
