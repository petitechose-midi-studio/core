#include "ui/view/DeviceSettingsView.hpp"

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

namespace core::ui {
namespace {

namespace style = oc::ui::lvgl::style;

constexpr uint32_t RENDER_TIMER_PERIOD_MS =
    (Config::Timing::LVGL_HZ > 1000) ? 1 : ((1000 + Config::Timing::LVGL_HZ - 1) / Config::Timing::LVGL_HZ);

}  // namespace

FLASHMEM DeviceSettingsView::DeviceSettingsView(lv_obj_t* parent, StateRefs stateRefs)
    : state_refs_(stateRefs) {
    createLayout(parent);
    render_timer_ = core::app::makeExtmemUnique<core::ui::PausableLvglTimer>(
        RENDER_TIMER_PERIOD_MS,
        onRenderTimer,
        this
    );
    bindToState();
}

FLASHMEM DeviceSettingsView::~DeviceSettingsView() {
    render_timer_.reset();
    menu_.reset();
    frame_.reset();
    container_ = nullptr;
    body_container_ = nullptr;
}

FLASHMEM void DeviceSettingsView::onActivate() {
    if (!container_) return;

    state_refs_.settings.openView();
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    scheduleRender(true);
}

FLASHMEM void DeviceSettingsView::onDeactivate() {
    state_refs_.settings.closeView();
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }
    pauseRenderTimerIfIdle();
}

FLASHMEM void DeviceSettingsView::createLayout(lv_obj_t* parent) {
    frame_ = core::app::makeExtmemUnique<MainViewFrame>(parent);
    container_ = frame_->container();
    body_container_ = frame_->body();
    style::apply(container_).transparent().noScroll();
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    menu_ = core::app::makeExtmemUnique<ms::ui::MenuListView>(body_container_);
    if (menu_ && menu_->getElement()) {
        lv_obj_set_height(menu_->getElement(), 0);
        lv_obj_set_flex_grow(menu_->getElement(), 1);
    }
}

FLASHMEM void DeviceSettingsView::bindToState() {
    watcher_.watchAll(
        [this]() { requestRender(); },
        state_refs_.settings.visible,
        state_refs_.settings.focusedRow,
        state_refs_.midiSync.mode,
        state_refs_.midiSync.followTransport,
        state_refs_.midiSync.autoFallbackMs,
        state_refs_.midiSync.autoLockClockCount,
        state_refs_.midiSync.activeSource,
        state_refs_.midiSync.externalClockPresent
    );
}

FLASHMEM void DeviceSettingsView::requestRender() {
    dirty_ = true;
    scheduleRender();
}

FLASHMEM void DeviceSettingsView::scheduleRender(bool ready) {
    dirty_ = true;
    if (render_timer_) {
        render_timer_->resume(ready);
    }
}

FLASHMEM void DeviceSettingsView::pauseRenderTimerIfIdle() {
    if (!render_timer_) return;
    if (dirty_ && container_ && !lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;
    render_timer_->pause();
}

FLASHMEM void DeviceSettingsView::render() {
    if (!dirty_ || !menu_ || !container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) {
        pauseRenderTimerIfIdle();
        return;
    }

    const auto page = core::state::settings::buildDeviceSettingsMenuPage(
        state_refs_.settings,
        core::state::settings::DeviceSettingsMenuContext{
            state_refs_.midiSync.mode.get(),
            state_refs_.midiSync.followTransport.get(),
            state_refs_.midiSync.autoFallbackMs.get(),
            state_refs_.midiSync.autoLockClockCount.get(),
            state_refs_.midiSync.activeSource.get(),
            state_refs_.midiSync.externalClockPresent.get(),
        }
    );

    for (uint8_t i = 0; i < page.rowCount && i < rows_.size(); ++i) {
        const auto& source = page.rows[i];
        rows_[i] = ms::ui::MenuRow{
            .label = source.label,
            .value = source.value,
            .kind = ms::ui::MenuRowKind::Value,
            .enabled = source.enabled,
        };
    }

    menu_->render(ms::ui::MenuListViewProps{
        .title = page.title,
        .meta = page.meta.data(),
        .rows = rows_.data(),
        .rowCount = page.rowCount,
        .selectedIndex = page.selectedIndex,
        .dataRevision = page.dataRevision,
    });

    dirty_ = false;
    pauseRenderTimerIfIdle();
}

FLASHMEM void DeviceSettingsView::onRenderTimer(lv_timer_t* timer) {
    auto* self = static_cast<DeviceSettingsView*>(lv_timer_get_user_data(timer));
    if (!self) return;
    self->render();
}

}  // namespace core::ui
