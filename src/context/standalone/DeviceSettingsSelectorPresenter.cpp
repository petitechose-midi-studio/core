#include "context/standalone/DeviceSettingsSelectorPresenter.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "state/settings/DeviceSettingsMenuModel.hpp"
#include "ui/interaction/SelectorPresentationPolicy.hpp"

namespace core::context::standalone {
namespace {

constexpr const char* const MODE_ITEMS[] PROGMEM = {"Master", "Slave", "Auto"};
constexpr const char* const FOLLOW_ITEMS[] PROGMEM = {"Off", "On"};
constexpr const char* const FALLBACK_ITEMS[] PROGMEM = {
    "150 ms",
    "250 ms",
    "500 ms",
    "750 ms",
    "1000 ms",
    "1500 ms",
    "2000 ms"
};
constexpr const char* const LOCK_ITEMS[] PROGMEM = {"1", "2", "3", "4", "6", "8", "12", "24"};
constexpr const char* const NOTE_OCTAVE_ITEMS[] PROGMEM = {"C3", "C4", "C5"};

struct SelectorRenderData {
    const char* title = "";
    const char* const* items = nullptr;
    int itemCount = 0;
    int selectedIndex = 0;
    uint32_t dataRevision = 0;
    bool visible = false;
};

FLASHMEM SelectorRenderData buildSelectorRenderData(
    const core::state::DeviceSettingsState& settings,
    const core::state::MidiSyncState& midiSync,
    const core::state::MidiNoteDisplayState& midiNoteDisplay
) {
    SelectorRenderData data{};
    if (settings.flowPhase.get() != core::state::DeviceSettingsFlowPhase::VALUE_SELECTOR ||
        !settings.selector.visible.get()) {
        return data;
    }

    const uint8_t row = settings.selector.editingRow.get();
    data.visible = true;
    data.title = core::state::settings::deviceSettingsRowLabel(row);

    switch (row) {
        case 0:
            data.items = MODE_ITEMS;
            data.itemCount = 3;
            break;
        case 1:
            data.items = FOLLOW_ITEMS;
            data.itemCount = 2;
            break;
        case 2:
            data.items = FALLBACK_ITEMS;
            data.itemCount = 7;
            break;
        case 3:
            data.items = LOCK_ITEMS;
            data.itemCount = 8;
            break;
        case 4:
            data.items = NOTE_OCTAVE_ITEMS;
            data.itemCount = 3;
            break;
        default:
            data.items = MODE_ITEMS;
            data.itemCount = 3;
            break;
    }

    data.selectedIndex = settings.selector.selectedIndex.get();
    data.dataRevision = static_cast<uint32_t>(row);
    data.dataRevision = data.dataRevision * 31U +
        static_cast<uint32_t>(midiNoteDisplay.octaveConvention.get());
    data.dataRevision = data.dataRevision * 31U +
        static_cast<uint32_t>(midiSync.mode.get());
    data.dataRevision = data.dataRevision * 31U +
        static_cast<uint32_t>(midiSync.followTransport.get());
    data.dataRevision = data.dataRevision * 31U +
        static_cast<uint32_t>(midiSync.autoFallbackMs.get());
    data.dataRevision = data.dataRevision * 31U +
        static_cast<uint32_t>(midiSync.autoLockClockCount.get());

    return data;
}

}  // namespace

FLASHMEM DeviceSettingsSelectorPresenter::DeviceSettingsSelectorPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListSelectorOverlay& selectorOverlay
)
    : state_refs_(stateRefs)
    , selector_overlay_(selectorOverlay)
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("DeviceSettingsSelector"),
          &DeviceSettingsSelectorPresenter::drainRenderQueue,
          this
      ) {}

FLASHMEM DeviceSettingsSelectorPresenter::~DeviceSettingsSelectorPresenter() = default;

FLASHMEM bool DeviceSettingsSelectorPresenter::bind() {
    selector_watcher_.bind<&DeviceSettingsSelectorPresenter::requestSelectorRender>(
        *this, 0, "DeviceSettings.selector"
    );
    return render_scheduler_.valid() && selector_watcher_.watchAll(
        state_refs_.settings.flowPhase,
        state_refs_.settings.selector.selectedIndex,
        state_refs_.settings.selector.editingRow,
        state_refs_.midiSync.mode,
        state_refs_.midiSync.followTransport,
        state_refs_.midiSync.autoFallbackMs,
        state_refs_.midiSync.autoLockClockCount,
        state_refs_.midiNoteDisplay.octaveConvention
    );
}

FLASHMEM void DeviceSettingsSelectorPresenter::requestSelectorRender() {
    render_scheduler_.request(RENDER_SELECTOR);
}

FLASHMEM void DeviceSettingsSelectorPresenter::drainRenderQueue(void* context, uint32_t flags) {
    if ((flags & RENDER_SELECTOR) == 0) return;
    auto* self = static_cast<DeviceSettingsSelectorPresenter*>(context);
    if (self) self->renderSelector();
}

FLASHMEM void DeviceSettingsSelectorPresenter::renderSelector() {
    const auto data = buildSelectorRenderData(
        state_refs_.settings,
        state_refs_.midiSync,
        state_refs_.midiNoteDisplay
    );
    if (!data.visible) {
        selector_overlay_.render({.visible = false});
        return;
    }

    selector_overlay_.render(
        core::ui::interaction::decisionSelectorProps(
            data.title,
            "Device settings",
            data.items,
            data.itemCount,
            data.selectedIndex,
            data.dataRevision
        )
    );
}

}  // namespace core::context::standalone
