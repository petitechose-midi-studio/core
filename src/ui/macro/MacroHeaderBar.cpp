#include "ui/macro/MacroHeaderBar.hpp"

#include <algorithm>
#include <cstdio>
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
constexpr lv_opa_t PAGE_SELECTOR_ACTIVE_BONUS = static_cast<lv_opa_t>(72);
constexpr lv_opa_t ACTIVITY_VELOCITY_RANGE = static_cast<lv_opa_t>(36);
constexpr uint32_t HEADER_BG_COLOR = theme::color::TEXT_PRIMARY;

uint8_t bitCount(uint16_t mask) {
    uint8_t count = 0U;
    while (mask != 0U) {
        count = static_cast<uint8_t>(count + (mask & 1U));
        mask = static_cast<uint16_t>(mask >> 1U);
    }
    return count;
}

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
    return trackScope ? "Tracks" : "Pages";
}

}  // namespace

FLASHMEM MacroHeaderBar::MacroHeaderBar(lv_obj_t* parent) {
    createUI(parent);
}

FLASHMEM MacroHeaderBar::~MacroHeaderBar() {
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

    header_row_ = core::app::makeExtmemUnique<TrackHeaderRow>(container_);
}

FLASHMEM void MacroHeaderBar::render(const MacroHeaderBarProps& props) {
    if (!container_ || !header_row_) return;

    const uint8_t displayTrack =
        (props.focusingTrack || props.previewTrackAddSlot)
            ? props.previewTrack
            : props.activeTrack;
    const uint8_t displayPage =
        (props.focusingPage || props.previewPageAddSlot)
            ? props.previewPage
            : props.activePage;
    const bool trackScope = props.focusingTrack;
    char recordingLabel[24] = {};
    if (props.automationTakePhase ==
        core::state::macro::MacroAutomationTakePhase::ARMED) {
        std::snprintf(recordingLabel,
                      sizeof(recordingLabel),
                      "Take \xC2\xB7 %s",
                      core::state::macro::macroAutomationTakeTimingLabel(
                          props.automationTakeTiming
                      ));
    } else if (props.automationTakePhase ==
               core::state::macro::MacroAutomationTakePhase::RECORDING) {
        const unsigned count = bitCount(props.automationTakeTouchedMask);
        std::snprintf(recordingLabel,
                      sizeof(recordingLabel),
                      props.automationRecordingStatus ==
                              core::state::macro::MacroAutomationRecordingStatus::REDUCED
                          ? "Rec~ \xC2\xB7 %u macro%s"
                          : "Rec \xC2\xB7 %u macro%s",
                      count,
                      count == 1U ? "" : "s");
    } else if (props.automationRecordingStatus ==
               core::state::macro::MacroAutomationRecordingStatus::TOO_SHORT) {
        std::snprintf(recordingLabel, sizeof(recordingLabel), "Rec \xC2\xB7 Short");
    } else if (props.automationRecordingStatus ==
               core::state::macro::MacroAutomationRecordingStatus::COMMIT_FAILED) {
        std::snprintf(recordingLabel, sizeof(recordingLabel), "Rec \xC2\xB7 Error");
    }
    const bool showRecordingStatus = recordingLabel[0] != '\0';

    TrackHeaderRowProps rowProps;
    rowProps.leftText = showRecordingStatus
        ? recordingLabel
        : props.slotSelectionActive
            ? "Macros"
            : props.pageSelectionActive
                ? "Pages"
            : focusLabel(trackScope);
    rowProps.itemCount = core::state::macro::PAGE_COUNT;
    rowProps.accentColor = showRecordingStatus
        ? theme::color::MACRO_AUTOMATION
        : (isTrackEnabled(props.trackEnabledMask, displayTrack)
            ? trackColor(displayTrack)
            : trackInactiveColor());
    rowProps.accentOpa = LV_OPA_80;
    rowProps.backgroundColor = HEADER_BG_COLOR;
    rowProps.showStatusDot = showRecordingStatus;
    rowProps.backgroundOpa =
        props.performanceOverlayMode !=
                core::state::macro::MacroPerformanceOverlayMode::NONE
            ? HEADER_BG_OPA_CLUTCH
            : HEADER_BG_OPA_IDLE;
    rowProps.showCursor =
        props.focusingPage || props.slotSelectionActive ||
        props.pageSelectionActive;
    rowProps.cursorIndex = displayPage;
    rowProps.cursorColor = theme::color::FOCUS_EDIT;
    rowProps.cursorOpa = LV_OPA_COVER;

    for (uint8_t i = 0; i < rowProps.itemCount; ++i) {
        const bool isActive = props.activePage == i;
        const bool enabled = (props.enabledMask & static_cast<uint16_t>(1U << i)) != 0;
        rowProps.itemActive[i] = isActive;
        rowProps.itemAddSlot[i] = props.addPageIndex == i && !enabled;
        const uint16_t bit = static_cast<uint16_t>(1U << i);
        const bool selected = (props.pageSelectedMask & bit) != 0U;
        const bool destination =
            (props.pageDestinationMask & bit) != 0U;
        const bool overwrite =
            (props.pageOverwriteMask & bit) != 0U;
        const bool blocked =
            (props.pageBlockedMask & bit) != 0U;
        rowProps.itemColors[i] = blocked
            ? theme::color::MACRO_AUTOMATION_RECORDING
            : destination
                ? (overwrite
                    ? theme::color::MACRO_CONFLICT
                    : theme::color::MACRO_CC_COLOR)
                : selected
                    ? theme::color::TEXT_PRIMARY
                    : enabled
                        ? rowProps.accentColor
                        : theme::color::INACTIVE;
        rowProps.itemOpacities[i] =
            selected || destination
                ? LV_OPA_80
                : pageSelectorOpa(
                      props.pageOutputActivity[i],
                      isActive,
                      enabled
                  );
    }

    header_row_->render(rowProps);
}

}  // namespace core::ui
