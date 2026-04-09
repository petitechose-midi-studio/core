#include "ui/macro/MacroHeaderBar.hpp"

#include <algorithm>
#include <cstring>

#include <oc/type/TextFormat.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace style = oc::ui::lvgl::style;

namespace {

constexpr lv_coord_t HEADER_HEIGHT = 16;
constexpr lv_opa_t HEADER_BG_OPA_IDLE = LV_OPA_TRANSP;
constexpr lv_opa_t HEADER_BG_OPA_CLUTCH = LV_OPA_TRANSP;
constexpr lv_opa_t PAGE_SELECTOR_BASE_OPA_ENABLED = static_cast<lv_opa_t>(18);
constexpr lv_opa_t PAGE_SELECTOR_BASE_OPA_DISABLED = static_cast<lv_opa_t>(8);
constexpr lv_opa_t PAGE_SELECTOR_ACTIVE_BONUS = static_cast<lv_opa_t>(48);
constexpr lv_opa_t PAGE_SELECTOR_PREVIEW_BONUS = static_cast<lv_opa_t>(120);
constexpr lv_opa_t ACTIVITY_VELOCITY_RANGE = static_cast<lv_opa_t>(36);
constexpr uint32_t HEADER_BG_COLOR = theme::color::TEXT_PRIMARY;
constexpr uint32_t PAGE_ACTIVITY_COLOR = theme::color::TEXT_PRIMARY;

bool isTrackEnabled(uint16_t enabledMask, uint8_t index) {
    return (enabledMask & static_cast<uint16_t>(1U << index)) != 0;
}

constexpr uint32_t trackColor(uint8_t index) {
    return theme::color::trackColor(index);
}

constexpr uint32_t trackInactiveColor() {
    return theme::color::INACTIVE;
}

lv_opa_t pageSelectorOpa(uint8_t activity, bool isActive, bool isPreview, bool enabled) {
    uint16_t opa = enabled ? PAGE_SELECTOR_BASE_OPA_ENABLED : PAGE_SELECTOR_BASE_OPA_DISABLED;
    if (isActive) opa += PAGE_SELECTOR_ACTIVE_BONUS;
    if (isPreview) opa += PAGE_SELECTOR_PREVIEW_BONUS;
    opa += static_cast<uint16_t>(activity) * static_cast<uint16_t>(ACTIVITY_VELOCITY_RANGE) / 127U;
    return static_cast<lv_opa_t>(std::min<uint16_t>(opa, LV_OPA_COVER));
}

template <size_t N>
void formatTrackPageText(std::array<char, N>& out, uint8_t track, uint8_t page) {
    std::memset(out.data(), 0, out.size());
    size_t pos = oc::type::text::appendString(out.data(), out.size(), 0, "Track ");
    pos = oc::type::text::appendUnsigned(out.data(), out.size(), pos, static_cast<uint32_t>(track + 1U));
    pos = oc::type::text::appendString(out.data(), out.size(), pos, " : Page ");
    pos = oc::type::text::appendUnsigned(out.data(), out.size(), pos, static_cast<uint32_t>(page + 1U));
    oc::type::text::terminate(out.data(), out.size(), pos);
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

    header_row_ = std::make_unique<TrackHeaderRow>(container_);
}

void MacroHeaderBar::render(const MacroHeaderBarProps& props) {
    if (!container_ || !header_row_) return;

    const uint8_t displayPage = props.selectingPage ? props.previewPage : props.activePage;
    std::array<char, 32> labelText{};
    formatTrackPageText(labelText, props.activeTrack, displayPage);

    TrackHeaderRowProps rowProps;
    rowProps.leftText = labelText.data();
    rowProps.accentColor =
        isTrackEnabled(props.trackEnabledMask, props.activeTrack)
            ? trackColor(props.activeTrack)
            : trackInactiveColor();
    rowProps.accentOpa = LV_OPA_80;
    rowProps.backgroundColor = HEADER_BG_COLOR;
    rowProps.backgroundOpa = props.clutchActive ? HEADER_BG_OPA_CLUTCH : HEADER_BG_OPA_IDLE;

    for (uint8_t i = 0; i < rowProps.ITEM_COUNT; ++i) {
        const bool isActive = props.activePage == i;
        const bool isPreview = props.previewPage == i;
        const bool enabled = (props.enabledMask & static_cast<uint8_t>(1U << i)) != 0;
        rowProps.itemColors[i] = PAGE_ACTIVITY_COLOR;
        rowProps.itemOpacities[i] =
            pageSelectorOpa(props.pageOutputActivity[i], isActive, isPreview, enabled);
    }

    header_row_->render(rowProps);
}

}  // namespace core::ui
