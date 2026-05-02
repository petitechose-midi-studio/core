#pragma once

#if defined(MS_UX_RECORDER)

#include <cstdint>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/core/input/InputBindingTrace.hpp>

#include "state/StructureSelectionState.hpp"
#include "validation/ux/SemanticUxContext.hpp"

namespace core::context::standalone::ux::detail {

FLASHMEM bool isButton(const oc::core::input::InputBindingTraceEvent& event,
                       Config::ButtonID button,
                       oc::core::input::ButtonBindingType type);
FLASHMEM bool isEncoder(const oc::core::input::InputBindingTraceEvent& event,
                        Config::EncoderID encoder);
FLASHMEM bool isMacroButtonLongPress(const oc::core::input::InputBindingTraceEvent& event,
                                     uint8_t& index);
FLASHMEM bool isMacroButtonRelease(const oc::core::input::InputBindingTraceEvent& event,
                                   uint8_t& index);
FLASHMEM bool isMacroEncoderTurn(const oc::core::input::InputBindingTraceEvent& event,
                                 uint8_t& index);

FLASHMEM void copyValueLabel(char (&out)[16], const char* value);
FLASHMEM void copyIndexLabel(char (&out)[16], unsigned value);

FLASHMEM const char* structureTarget(core::state::StructureNavigationFocus focus);
FLASHMEM const char* structureTarget(core::state::StructureSelectionScope scope);

FLASHMEM bool isAddSlot(const core::validation::ux::SemanticUxContext& out);
FLASHMEM void markNoop(core::validation::ux::SemanticUxContext& out, const char* reason);
FLASHMEM void markIgnored(core::validation::ux::SemanticUxContext& out, const char* reason);

}  // namespace core::context::standalone::ux::detail

#endif
