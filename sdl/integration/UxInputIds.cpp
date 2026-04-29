#include "integration/UxInputIds.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include <config/InputIDs.hpp>

namespace sdl::integration {

namespace {

std::string upper(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

}  // namespace

bool uxButtonIdFromName(std::string_view name, oc::type::ButtonID& out) {
    const std::string id = upper(name);
    using Config::ButtonID;
    if (id == "LEFT_TOP") out = static_cast<oc::type::ButtonID>(ButtonID::LEFT_TOP);
    else if (id == "LEFT_CENTER") out = static_cast<oc::type::ButtonID>(ButtonID::LEFT_CENTER);
    else if (id == "LEFT_BOTTOM") out = static_cast<oc::type::ButtonID>(ButtonID::LEFT_BOTTOM);
    else if (id == "BOTTOM_LEFT") out = static_cast<oc::type::ButtonID>(ButtonID::BOTTOM_LEFT);
    else if (id == "BOTTOM_CENTER") out = static_cast<oc::type::ButtonID>(ButtonID::BOTTOM_CENTER);
    else if (id == "BOTTOM_RIGHT") out = static_cast<oc::type::ButtonID>(ButtonID::BOTTOM_RIGHT);
    else if (id == "MACRO_1") out = static_cast<oc::type::ButtonID>(ButtonID::MACRO_1);
    else if (id == "MACRO_2") out = static_cast<oc::type::ButtonID>(ButtonID::MACRO_2);
    else if (id == "MACRO_3") out = static_cast<oc::type::ButtonID>(ButtonID::MACRO_3);
    else if (id == "MACRO_4") out = static_cast<oc::type::ButtonID>(ButtonID::MACRO_4);
    else if (id == "MACRO_5") out = static_cast<oc::type::ButtonID>(ButtonID::MACRO_5);
    else if (id == "MACRO_6") out = static_cast<oc::type::ButtonID>(ButtonID::MACRO_6);
    else if (id == "MACRO_7") out = static_cast<oc::type::ButtonID>(ButtonID::MACRO_7);
    else if (id == "MACRO_8") out = static_cast<oc::type::ButtonID>(ButtonID::MACRO_8);
    else if (id == "NAV") out = static_cast<oc::type::ButtonID>(ButtonID::NAV);
    else return false;
    return true;
}

bool uxEncoderIdFromName(std::string_view name, oc::type::EncoderID& out) {
    const std::string id = upper(name);
    using Config::EncoderID;
    if (id == "MACRO_1") out = static_cast<oc::type::EncoderID>(EncoderID::MACRO_1);
    else if (id == "MACRO_2") out = static_cast<oc::type::EncoderID>(EncoderID::MACRO_2);
    else if (id == "MACRO_3") out = static_cast<oc::type::EncoderID>(EncoderID::MACRO_3);
    else if (id == "MACRO_4") out = static_cast<oc::type::EncoderID>(EncoderID::MACRO_4);
    else if (id == "MACRO_5") out = static_cast<oc::type::EncoderID>(EncoderID::MACRO_5);
    else if (id == "MACRO_6") out = static_cast<oc::type::EncoderID>(EncoderID::MACRO_6);
    else if (id == "MACRO_7") out = static_cast<oc::type::EncoderID>(EncoderID::MACRO_7);
    else if (id == "MACRO_8") out = static_cast<oc::type::EncoderID>(EncoderID::MACRO_8);
    else if (id == "NAV") out = static_cast<oc::type::EncoderID>(EncoderID::NAV);
    else if (id == "OPT") out = static_cast<oc::type::EncoderID>(EncoderID::OPT);
    else return false;
    return true;
}

}  // namespace sdl::integration
