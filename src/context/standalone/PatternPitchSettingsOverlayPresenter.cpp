#include "context/standalone/PatternPitchSettingsOverlayPresenter.hpp"

#include <array>

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "state/ViewSelectorItems.hpp"
#include "state/sequencer/SequencerScaleCatalog.hpp"

namespace core::context::standalone {

namespace {

namespace catalog = core::state::sequencer::scale_catalog;

constexpr const char* PITCH_CONTEXT_LABEL = "Pitch";
constexpr const char* const ROW_KEYS[] = {"Scale", "Root", "Type", "Pitch Edit"};

}  // namespace

PatternPitchSettingsOverlayPresenter::PatternPitchSettingsOverlayPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListKeyValueOverlay& overlay,
    ms::ui::VirtualListSelectorOverlay& selectorOverlay
)
    : state_refs_(stateRefs)
    , overlay_(overlay)
    , selector_overlay_(selectorOverlay) {}

FLASHMEM void PatternPitchSettingsOverlayPresenter::bind() {
    overlay_watcher_.watchAll(
        [this]() { renderOverlay(); },
        state_refs_.settings.visible,
        state_refs_.settings.focusedRow,
        state_refs_.sequencer.patternScaleRevision,
        state_refs_.trackBank.projectScaleRevisionSignal()
    );

    selector_watcher_.watchAll(
        [this]() { renderSelector(); },
        state_refs_.settings.flowPhase,
        state_refs_.settings.selector.visible,
        state_refs_.settings.selector.editingRow,
        state_refs_.settings.selector.selectedIndex,
        state_refs_.sequencer.patternScaleRevision,
        state_refs_.trackBank.projectScaleRevisionSignal()
    );
}

FLASHMEM void PatternPitchSettingsOverlayPresenter::renderOverlay() {
    if (!state_refs_.settings.visible.get()) {
        overlay_.render({.visible = false});
        return;
    }

    const bool override =
        core::state::sequencer::isPatternScaleOverride(state_refs_.sequencer.scalePolicy);
    auto effectiveScale = override ? state_refs_.sequencer.scaleOverride
                                   : state_refs_.trackBank.projectScaleSettings();
    effectiveScale.clamp();

    const std::array<ms::ui::KeyValueRow, 4> rows{{
        {.key = ROW_KEYS[0], .value = override ? catalog::PATTERN_SCALE_POLICY_LABELS[1]
                                                : catalog::PATTERN_SCALE_POLICY_LABELS[0]},
        {.key = ROW_KEYS[1], .value = catalog::rootLabel(effectiveScale.root)},
        {.key = ROW_KEYS[2], .value = catalog::scaleTypeLabel(effectiveScale.type)},
        {.key = ROW_KEYS[3], .value = catalog::pitchEditModeLabel(state_refs_.sequencer.pitchEditMode)},
    }};

    overlay_.render({
        .title = PITCH_CONTEXT_LABEL,
        .meta = core::state::viewSelectorItemLabel(core::state::ViewSelectorItem::SEQUENCER),
        .rows = rows.data(),
        .rowCount = static_cast<int>(rows.size()),
        .selectedIndex = state_refs_.settings.focusedRow.get(),
        .visible = true,
        .dataRevision = 1U |
            (static_cast<uint32_t>(state_refs_.sequencer.patternScaleRevision.get()) << 8) |
            (static_cast<uint32_t>(state_refs_.trackBank.projectScaleRevisionSignal().get()) << 16),
    });
}

FLASHMEM void PatternPitchSettingsOverlayPresenter::renderSelector() {
    if (state_refs_.settings.flowPhase.get() !=
            core::state::PatternPitchSettingsFlowPhase::VALUE_SELECTOR ||
        !state_refs_.settings.selector.visible.get()) {
        selector_overlay_.render({.visible = false});
        return;
    }

    const uint8_t row = state_refs_.settings.selector.editingRow.get();
    const char* title = row < 4 ? ROW_KEYS[row] : "Value";
    const char* meta = PITCH_CONTEXT_LABEL;
    const char* const* items = catalog::ROOT_LABELS;
    int itemCount = catalog::ROOT_COUNT;

    switch (row) {
        case 0:
            items = catalog::PATTERN_SCALE_POLICY_LABELS;
            itemCount = catalog::PATTERN_SCALE_POLICY_COUNT;
            break;
        case 1:
            items = catalog::ROOT_LABELS;
            itemCount = catalog::ROOT_COUNT;
            break;
        case 2:
            items = catalog::SCALE_TYPE_LABELS;
            itemCount = catalog::SCALE_TYPE_COUNT;
            break;
        case 3:
            items = catalog::PITCH_EDIT_MODE_LABELS;
            itemCount = catalog::PITCH_EDIT_MODE_COUNT;
            break;
        default:
            break;
    }

    selector_overlay_.render({
        .title = title,
        .meta = meta,
        .items = items,
        .itemCount = itemCount,
        .selectedIndex = state_refs_.settings.selector.selectedIndex.get(),
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = 1U | (static_cast<uint32_t>(row) << 24),
    });
}

}  // namespace core::context::standalone
