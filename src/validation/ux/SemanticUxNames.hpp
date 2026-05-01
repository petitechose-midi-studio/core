#pragma once

#include <oc/core/input/Binding.hpp>
#include <oc/type/Ids.hpp>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

namespace core::validation::ux {

const char* buttonName(oc::type::ButtonID id);
const char* encoderName(oc::type::EncoderID id);
const char* buttonGestureName(oc::core::input::ButtonBindingType type);
const char* encoderGestureName(oc::core::input::EncoderBindingType type);
const char* viewName(core::ui::ViewType view);
const char* overlayName(core::ui::OverlayType overlay);

}  // namespace core::validation::ux
