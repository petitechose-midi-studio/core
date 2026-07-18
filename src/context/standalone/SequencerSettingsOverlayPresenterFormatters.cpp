#include "context/standalone/SequencerSettingsOverlayPresenterFormatters.hpp"

#include <config/PlatformCompat.hpp>

#include "state/ViewSelectorItems.hpp"
#include "state/sequencer/SequencerScaleCatalog.hpp"

namespace core::context::standalone::sequencer_settings_presenter {

namespace {

namespace catalog = core::state::sequencer::scale_catalog;

constexpr const char* const ROW_KEYS[] PROGMEM = {"Root", "Scale", "Mode"};

}  // namespace

FLASHMEM OverlayRenderData buildOverlayRenderData(const Source& source) {
    OverlayRenderData data{};
    const auto projectSettings = source.trackBank.projectScaleSettings();

    data.rows = {{
        {.key = ROW_KEYS[0], .value = catalog::rootLabel(projectSettings.root)},
        {.key = ROW_KEYS[1], .value = catalog::scaleTypeLabel(projectSettings.type)},
        {.key = ROW_KEYS[2], .value = catalog::constraintModeLabel(projectSettings.mode)},
    }};
    data.selectedIndex = source.sequencerSettings.focusedRow.get();
    data.dataRevision = 1U |
        (static_cast<uint32_t>(source.trackBank.projectScaleRevisionSignal().get()) << 4);

    return data;
}

FLASHMEM SelectorRenderData buildSelectorRenderData(const Source& source) {
    SelectorRenderData data{};
    if (source.sequencerSettings.flowPhase.get() !=
            core::state::SequencerSettingsFlowPhase::VALUE_SELECTOR ||
        !source.sequencerSettings.selector.visible.get()) {
        return data;
    }

    const uint8_t row = source.sequencerSettings.selector.editingRow.get();
    data.visible = true;
    data.title = row < 3 ? ROW_KEYS[row] : "Value";
    data.meta = core::state::SETTINGS_SECTION_LABEL;

    switch (row) {
        case 0:
            data.items = catalog::ROOT_LABELS;
            data.itemCount = catalog::ROOT_COUNT;
            break;
        case 1:
            data.items = catalog::SCALE_TYPE_LABELS;
            data.itemCount = catalog::SCALE_TYPE_COUNT;
            break;
        case 2:
            data.items = catalog::CONSTRAINT_MODE_LABELS;
            data.itemCount = catalog::CONSTRAINT_MODE_COUNT;
            break;
        default:
            data.items = catalog::ROOT_LABELS;
            data.itemCount = catalog::ROOT_COUNT;
            break;
    }

    data.selectedIndex = source.sequencerSettings.selector.selectedIndex.get();
    data.dataRevision = 1U |
        (static_cast<uint32_t>(row) << 24);

    return data;
}

}  // namespace core::context::standalone::sequencer_settings_presenter
