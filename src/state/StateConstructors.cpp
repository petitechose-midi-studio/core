#include "state/CoreState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM MacroState::MacroState() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        char buf[16];
        size_t pos = oc::type::text::appendString(buf, sizeof(buf), 0, "Macro ");
        pos = oc::type::text::appendUnsigned(buf, sizeof(buf), pos, i + 1);
        oc::type::text::terminate(buf, sizeof(buf), pos);
        slots[i].label.set(buf);
    }
}

FLASHMEM MacroState::~MacroState() = default;

FLASHMEM StatusBarState::StatusBarState() = default;

FLASHMEM StatusBarState::~StatusBarState() = default;

FLASHMEM MacroEditState::~MacroEditState() = default;

FLASHMEM UiSystemState::UiSystemState()
    : overlays{},
      activeView{core::ui::ViewType::MACRO},
      sharedTracks{},
      trackNavigation{},
      viewSelector{},
      statusBar{},
      midiSync{},
      deviceSettings{},
      patternPitchSettings{},
      macroEdit{},
      macroUi{},
      projectNavigation{} {}

FLASHMEM UiSystemState::~UiSystemState() = default;

}  // namespace core::state
