#pragma once

#include <ms/device_support/v1/Timing.hpp>

#include <oc/core/input/InputConfig.hpp>

namespace ms::device_support::v1::input {

inline constexpr oc::core::input::InputConfig CONFIG{
    timing::LONG_PRESS_MS,
    timing::DOUBLE_TAP_MS,
    timing::LATCH_THRESHOLD_MS,
    timing::DEBOUNCE_MS,
    oc::core::input::ReleaseRoutingPolicy::OwnerOnly,
    oc::core::input::GestureRoutingPolicy::PressScoped,
    oc::core::input::BindingAmbiguityPolicy::FailClosed,
    oc::core::input::GlobalRoutingPolicy::ExplicitPassThroughOnly,
};

}  // namespace ms::device_support::v1::input
