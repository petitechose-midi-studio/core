#include "integration/InputBindingTraceWriter.hpp"

#include <SDL2/SDL.h>

namespace sdl::integration {

namespace {

const char* stageName(oc::core::input::InputBindingTraceStage stage) {
    using Stage = oc::core::input::InputBindingTraceStage;
    switch (stage) {
        case Stage::Event: return "event";
        case Stage::Candidate: return "candidate";
        case Stage::Dispatch: return "dispatch";
        case Stage::NoDispatch: return "no_dispatch";
        case Stage::RouteCapture: return "route_capture";
        case Stage::RouteHandoff: return "route_handoff";
        case Stage::Fallback: return "fallback";
        case Stage::Ambiguous: return "ambiguous";
        case Stage::Consumed: return "consumed";
    }
    return "unknown";
}

const char* domainName(oc::core::input::InputBindingTraceDomain domain) {
    return domain == oc::core::input::InputBindingTraceDomain::Encoder ? "encoder" : "button";
}

const char* buttonTypeName(oc::core::input::ButtonBindingType type) {
    using Type = oc::core::input::ButtonBindingType;
    switch (type) {
        case Type::PRESS: return "press";
        case Type::RELEASE: return "release";
        case Type::LONG_PRESS: return "long_press";
        case Type::DOUBLE_TAP: return "double_tap";
        case Type::COMBO: return "combo";
    }
    return "unknown";
}

const char* encoderTypeName(oc::core::input::EncoderBindingType type) {
    using Type = oc::core::input::EncoderBindingType;
    switch (type) {
        case Type::TURN: return "turn";
        case Type::TURN_WHILE_PRESSED: return "turn_while_pressed";
    }
    return "unknown";
}

int boolInt(bool value) {
    return value ? 1 : 0;
}

}  // namespace

bool InputBindingTraceWriter::open(const char* path) {
    error_.clear();
    stream_.open(path, std::ios::out | std::ios::trunc);
    if (!stream_) {
        error_ = "unable to open binding trace file";
        return false;
    }
    stream_ << "{\"event\":\"binding_trace_start\"}\n";
    return true;
}

void InputBindingTraceWriter::write(const oc::core::input::InputBindingTraceEvent& event) {
    if (!stream_) return;

    stream_ << "{\"event\":\"binding_trace\""
            << ",\"ms\":" << SDL_GetTicks()
            << ",\"stage\":\"" << stageName(event.stage) << "\""
            << ",\"domain\":\"" << domainName(event.domain) << "\""
            << ",\"button_id\":" << static_cast<unsigned>(event.buttonId)
            << ",\"encoder_id\":" << static_cast<unsigned>(event.encoderId)
            << ",\"button_type\":\"" << buttonTypeName(event.buttonType) << "\""
            << ",\"encoder_type\":\"" << encoderTypeName(event.encoderType) << "\""
            << ",\"binding_id\":" << static_cast<unsigned>(event.bindingId)
            << ",\"scope_id\":" << static_cast<unsigned>(event.scopeId)
            << ",\"authority_scope\":" << static_cast<unsigned>(event.authorityScope)
            << ",\"scoped\":" << boolInt(event.scoped)
            << ",\"active\":" << boolInt(event.active)
            << ",\"authority\":" << boolInt(event.authority)
            << ",\"required_button\":" << boolInt(event.requiredButton)
            << ",\"dispatched\":" << boolInt(event.dispatched)
            << ",\"candidate_count\":"
            << static_cast<unsigned>(event.candidateCount)
            << ",\"encoder_value\":" << event.encoderValue
            << "}\n";
}

}  // namespace sdl::integration
