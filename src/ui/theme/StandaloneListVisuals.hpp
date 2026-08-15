#pragma once

#include <ms/ui/widget/ListVisualTokens.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace standalone::theme {

inline constexpr ms::ui::ListVisualTokens CONTROLLER_LIST_VISUALS{
    color::FOCUS_EDIT,
    color::SURFACE_RAISED,
    color::TEXT_PRIMARY,
    color::TEXT_SECONDARY,
    color::TEXT_DISABLED,
    color::POSITIVE,
    color::WARNING,
    color::DESTRUCTIVE,
    LV_OPA_COVER,
    2,
};

}  // namespace standalone::theme
