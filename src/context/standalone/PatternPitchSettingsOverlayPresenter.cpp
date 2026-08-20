#include "context/standalone/PatternPitchSettingsOverlayPresenter.hpp"

#include <array>

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>

#include "state/ViewSelectorItems.hpp"
#include "state/sequencer/SequencerScaleCatalog.hpp"
#include "ui/font/StandaloneFonts.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/interaction/SelectorPresentationPolicy.hpp"
#include "ui/theme/StandaloneListVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone {

namespace {

namespace catalog = core::state::sequencer::scale_catalog;

constexpr const char* PITCH_CONTEXT_LABEL = "Pitch Context";
constexpr const char* const ROW_KEYS[] PROGMEM = {
    "Scale",
    "Root",
    "Type",
    "Interval Basis",
};

}  // namespace

FLASHMEM PatternPitchSettingsOverlayPresenter::PatternPitchSettingsOverlayPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListKeyValueOverlay& overlay,
    ms::ui::VirtualListSelectorOverlay& selectorOverlay
)
    : state_refs_(stateRefs)
    , overlay_(overlay)
    , selector_overlay_(selectorOverlay)
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("PatternPitchSettings"),
          &PatternPitchSettingsOverlayPresenter::drainRenderQueue,
          this
      ) {}

FLASHMEM PatternPitchSettingsOverlayPresenter::~PatternPitchSettingsOverlayPresenter() = default;

FLASHMEM bool PatternPitchSettingsOverlayPresenter::bind() {
    bool bound = render_scheduler_.valid();
    overlay_watcher_.bind<&PatternPitchSettingsOverlayPresenter::requestOverlayRender>(
        *this, 0, "PatternPitchSettings.overlay"
    );
    bound = overlay_watcher_.watchAll(
        state_refs_.settings.visible,
        state_refs_.settings.focusedRow,
        state_refs_.sequencer.pattern.patternScaleRevision,
        state_refs_.trackBank.projectScaleRevisionSignal()
    ) && bound;

    selector_watcher_.bind<&PatternPitchSettingsOverlayPresenter::requestSelectorRender>(
        *this, 1, "PatternPitchSettings.selector"
    );
    bound = selector_watcher_.watchAll(
        state_refs_.settings.flowPhase,
        state_refs_.settings.selector.visible,
        state_refs_.settings.selector.editingRow,
        state_refs_.settings.selector.selectedIndex
    ) && bound;
    return bound;
}

FLASHMEM void PatternPitchSettingsOverlayPresenter::requestOverlayRender() {
    render_scheduler_.request(RENDER_OVERLAY);
}

FLASHMEM void PatternPitchSettingsOverlayPresenter::requestSelectorRender() {
    render_scheduler_.request(RENDER_SELECTOR);
}

FLASHMEM void PatternPitchSettingsOverlayPresenter::drainRenderQueue(
    void* context,
    uint32_t flags
) {
    auto* self = static_cast<PatternPitchSettingsOverlayPresenter*>(context);
    if (self) self->renderPending(flags);
}

FLASHMEM void PatternPitchSettingsOverlayPresenter::renderPending(uint32_t flags) {
    if ((flags & RENDER_OVERLAY) != 0) renderOverlay();
    if ((flags & RENDER_SELECTOR) != 0) renderSelector();
}

FLASHMEM void PatternPitchSettingsOverlayPresenter::renderOverlay() {
    if (!state_refs_.settings.visible.get()) {
        overlay_.render({.visible = false});
        return;
    }

    const auto& pattern = state_refs_.sequencer.pattern;
    const bool override =
        core::state::sequencer::isPatternScaleOverride(pattern.scalePolicy);
    auto effectiveScale = override ? pattern.scaleOverride
                                   : state_refs_.trackBank.projectScaleSettings();
    effectiveScale.clamp();
    const uint8_t selectedRow = state_refs_.settings.focusedRow.get();
    const auto iconColor = [selectedRow](uint8_t row) {
        return row == selectedRow ? ::standalone::theme::color::FOCUS_EDIT
                                  : ::standalone::theme::color::TEXT_SECONDARY;
    };

    const std::array<ms::ui::KeyValueRow, 4> rows{{
        {.key = ROW_KEYS[0], .value = override ? catalog::PATTERN_SCALE_POLICY_LABELS[1]
                                                : catalog::PATTERN_SCALE_POLICY_LABELS[0],
         .icon = ::standalone::icons::PATTERN,
         .iconFont = standalone_fonts.icons_14,
         .iconColor = iconColor(0)},
        {.key = ROW_KEYS[1], .value = catalog::rootLabel(effectiveScale.root),
         .icon = ::standalone::icons::NOTE_PROP_PITCH,
         .iconFont = standalone_fonts.icons_14,
         .iconColor = iconColor(1)},
        {.key = ROW_KEYS[2], .value = catalog::scaleTypeLabel(effectiveScale.type),
         .icon = ::standalone::icons::SCALE,
         .iconFont = standalone_fonts.icons_14,
         .iconColor = iconColor(2)},
        {.key = ROW_KEYS[3], .value = catalog::pitchEditModeLabel(pattern.pitchEditMode),
         .icon = ::standalone::icons::LOCK,
         .iconFont = standalone_fonts.icons_14,
         .iconColor = iconColor(3)},
    }};

    overlay_.render({
        .title = PITCH_CONTEXT_LABEL,
        .meta = core::state::viewSelectorItemLabel(core::state::ViewSelectorItem::SEQUENCER),
        .rows = rows.data(),
        .rowCount = static_cast<int>(rows.size()),
        .selectedIndex = state_refs_.settings.focusedRow.get(),
        .visible = true,
        .dataRevision = 1U |
            (static_cast<uint32_t>(selectedRow) << 4) |
            (static_cast<uint32_t>(state_refs_.sequencer.pattern.patternScaleRevision.get()) << 8) |
            (static_cast<uint32_t>(state_refs_.trackBank.projectScaleRevisionSignal().get()) << 16),
        .visualTokens = &::standalone::theme::CONTROLLER_LIST_VISUALS,
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

    selector_overlay_.render(
        core::ui::interaction::decisionSelectorProps(
            title,
            meta,
            items,
            itemCount,
            state_refs_.settings.selector.selectedIndex.get(),
            1U | (static_cast<uint32_t>(row) << 24)
        )
    );
}

}  // namespace core::context::standalone
