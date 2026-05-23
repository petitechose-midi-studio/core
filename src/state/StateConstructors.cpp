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

FLASHMEM DataManagerState::DataManagerState() {
    feedback.set("");
}

FLASHMEM StatusBarState::StatusBarState() {
    pageName.set("Page 1");
}

FLASHMEM UiSystemState::UiSystemState()
    : overlays{},
      activeView{core::ui::ViewType::MACRO},
      sharedTracks{},
      trackNavigation{},
      viewSelector{},
      statusBar{},
      midiSync{},
      globalSettings{},
      sequencerSettings{},
      dataManager{},
      macroEdit{} {}

}  // namespace core::state
