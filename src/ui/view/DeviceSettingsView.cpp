#include "ui/view/DeviceSettingsView.hpp"

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "ui/view/RetainedViewRenderPolicy.hpp"

namespace core::ui {
namespace {

namespace style = oc::ui::lvgl::style;

constexpr uint32_t RENDER_TIMER_PERIOD_MS =
    (Config::Timing::RETAINED_VIEW_HZ > 1000)
        ? 1
        : ((1000 + Config::Timing::RETAINED_VIEW_HZ - 1) /
           Config::Timing::RETAINED_VIEW_HZ);

}  // namespace

FLASHMEM DeviceSettingsView::DeviceSettingsView(lv_obj_t* parent, StateRefs stateRefs)
    : state_refs_(stateRefs) {
    createLayout(parent);
    if (!frame_ || !frame_->valid() || !container_ || !body_container_ ||
        !menu_ || !menu_->getElement()) return;
    render_scheduler_ =
        core::app::makeExtmemUnique<core::ui::CoalescedLvglRenderScheduler>(
            core::ui::renderSchedulerDebugLabel("DeviceSettingsView"),
            &DeviceSettingsView::drainRender,
            this,
            RENDER_TIMER_PERIOD_MS,
            &DeviceSettingsView::canDrainRender
        );
    if (!render_scheduler_ || !render_scheduler_->valid() || !bindToState()) return;
    initialized_ = true;
}

FLASHMEM DeviceSettingsView::~DeviceSettingsView() {
    render_scheduler_.reset();
    menu_.reset();
    frame_.reset();
    container_ = nullptr;
    body_container_ = nullptr;
}

FLASHMEM void DeviceSettingsView::onActivate() {
    if (!container_) return;

    RetainedViewRenderPolicy::show(container_);
    if (render_scheduler_) render_scheduler_->request(1U, true);
}

FLASHMEM void DeviceSettingsView::onDeactivate() {
    if (render_scheduler_) render_scheduler_->pause();
    RetainedViewRenderPolicy::hide(container_);
}

FLASHMEM void DeviceSettingsView::createLayout(lv_obj_t* parent) {
    frame_ = core::app::makeExtmemUnique<MainViewFrame>(parent);
    if (!frame_ || !frame_->valid()) return;
    container_ = frame_->container();
    body_container_ = frame_->body();
    style::apply(container_).noScroll();
    RetainedViewRenderPolicy::initializeHidden(container_);

    menu_ = core::app::makeExtmemUnique<ms::ui::MenuListView>(body_container_);
    if (menu_ && menu_->getElement()) {
        lv_obj_set_height(menu_->getElement(), 0);
        lv_obj_set_flex_grow(menu_->getElement(), 1);
    }
}

FLASHMEM bool DeviceSettingsView::bindToState() {
    watcher_.bind<&DeviceSettingsView::requestRender>(*this, 0, "DeviceSettings.view");
    return watcher_.watchAll(
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

void DeviceSettingsView::requestRender() {
    if (render_scheduler_) render_scheduler_->request(1U);
}

void DeviceSettingsView::render() {
    if (!menu_ || !RetainedViewRenderPolicy::visible(container_)) return;

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

}

bool DeviceSettingsView::canDrainRender(void* context) {
    const auto* self = static_cast<const DeviceSettingsView*>(context);
    return self && RetainedViewRenderPolicy::visible(self->container_);
}

void DeviceSettingsView::drainRender(void* context, uint32_t flags) {
    if ((flags & 1U) == 0) return;
    auto* self = static_cast<DeviceSettingsView*>(context);
    if (self) self->render();
}

}  // namespace core::ui
