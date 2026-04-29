#pragma once

#include <string_view>

#include <oc/type/Ids.hpp>

namespace sdl::integration {

bool uxButtonIdFromName(std::string_view name, oc::type::ButtonID& out);
bool uxEncoderIdFromName(std::string_view name, oc::type::EncoderID& out);

}  // namespace sdl::integration
