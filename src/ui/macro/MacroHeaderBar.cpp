#include "ui/macro/MacroHeaderBar.hpp"

#include <algorithm>
#include <cstring>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace style = oc::ui::lvgl::style;

namespace {

constexpr lv_coord_t HEADER_HEIGHT = 20;
constexpr lv_opa_t HEADER_BG_OPA_IDLE = LV_OPA_TRANSP;
constexpr lv_opa_t HEADER_BG_OPA_CLUTCH = LV_OPA_TRANSP;
constexpr lv_opa_t PAGE_SELECTOR_BASE_OPA_ENABLED = static_cast<lv_opa_t>(18);
constexpr lv_opa_t PAGE_SELECTOR_BASE_OPA_DISABLED = static_cast<lv_opa_t>(8);
constexpr lv_opa_t PAGE_SELECTOR_ACTIVE_BONUS = static_cast<lv_opa_t>(48);
constexpr lv_opa_t ACTIVITY_VELOCITY_RANGE = static_cast<lv_opa_t>(36);
constexpr uint32_t HEADER_BG_COLOR = theme::color::TEXT_PRIMARY;
bool isTrackEnabled(uint16_t enabledMask, uint8_t index) {
    return (enabledMask & static_cast<uint16_t>(1U << index)) != 0;
}

constexpr uint32_t trackColor(uint8_t index) {
    return theme::color::trackColor(index);
}

constexpr uint32_t trackInactiveColor() {
    return theme::color::INACTIVE;
}

lv_opa_t pageSelectorOpa(uint8_t activity, bool isActive, bool enabled) {
    uint16_t opa = enabled ? PAGE_SELECTOR_BASE_OPA_ENABLED : PAGE_SELECTOR_BASE_OPA_DISABLED;
    if (isActive) opa += PAGE_SELECTOR_ACTIVE_BONUS;
    opa += static_cast<uint16_t>(activity) * static_cast<uint16_t>(ACTIVITY_VELOCITY_RANGE) / 127U;
    return static_cast<lv_opa_t>(std::min<uint16_t>(opa, LV_OPA_COVER));
}

const char* focusLabel(bool trackScope) {
    return trackScope ? "TRACKS" : "PAGES";
}

}  // namespace

MacroHeaderBar::MacroHeaderBar(lv_obj_t* parent) {
    createUI(parent);
}

MacroHeaderBar::~MacroHeaderBar() {
    if (container_) {
        header_row_.reset();
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}

FLASHMEM void MacroHeaderBar::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_)
        .size(LV_PCT(100), HEADER_HEIGHT)
        .transparent()
        .pad(0)
        .noScroll()
        .noBorder();
    lv_obj_add_flag(container_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    header_row_ = std::make_unique<TrackHeaderRow>(container_);
}

void MacroHeaderBar::render(const MacroHeaderBarProps& props) {
    if (!container_ || !header_row_) return;

    const uint8_t displayTrack =
        (props.selectingTrack || props.previewTrackAddSlot) ? props.previewTrack : props.activeTrack;
    const uint8_t displayPage =
        (props.selectingPage || props.previewPageAddSlot) ? props.previewPage : props.activePage;
    const bool trackScope = props.selectingTrack || props.focusingTrack;

    TrackHeaderRowProps rowProps;
    rowProps.leftText = focusLabel(trackScope);
    rowProps.itemCount = core::state::macro::PAGE_COUNT;
    rowProps.accentColor =
        isTrackEnabled(props.trackEnabledMask, displayTrack)
            ? trackColor(displayTrack)
            : trackInactiveColor();
    rowProps.accentOpa = LV_OPA_80;
    rowProps.backgroundColor = HEADER_BG_COLOR;
    rowProps.backgroundOpa = props.clutchActive ? HEADER_BG_OPA_CLUTCH : HEADER_BG_OPA_IDLE;
    rowProps.showCursor = props.selectingPage || props.focusingPage;
    rowProps.cursorIndex = displayPage;
    rowProps.selectedMask = props.selectingPage ? props.selectedPageMask : 0;
    rowProps.cursorColor = (props.selectingPage || props.selectingTrack)
        ? theme::color::TEXT_PRIMARY
        : rowProps.accentColor;
    rowProps.cursorOpa = LV_OPA_COVER;

    for (uint8_t i = 0; i < rowProps.itemCount; ++i) {
        const bool isActive = props.activePage == i;
        const bool enabled = (props.enabledMask & static_cast<uint16_t>(1U << i)) != 0;
        rowProps.itemActive[i] = isActive;
        rowProps.itemAddSlot[i] = props.addPageIndex == i && !enabled;
        rowProps.itemColors[i] = enabled ? rowProps.accentColor : theme::color::INACTIVE;
        rowProps.itemOpacities[i] =
            pageSelectorOpa(props.pageOutputActivity[i], isActive, enabled);
    }

    header_row_->render(rowProps);
}

}  // namespace core::ui
