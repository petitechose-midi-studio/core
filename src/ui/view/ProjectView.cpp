#include "ui/view/ProjectView.hpp"

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace {

namespace style = oc::ui::lvgl::style;
namespace theme = standalone::theme;

constexpr lv_coord_t TAB_STRIP_HEIGHT = 24;
constexpr lv_coord_t TAB_ACTIVE_WIDTH = 92;
constexpr lv_coord_t TAB_INACTIVE_WIDTH = 30;
constexpr uint32_t RENDER_TIMER_PERIOD_MS =
    (Config::Timing::LVGL_HZ > 1000) ? 1 : ((1000 + Config::Timing::LVGL_HZ - 1) / Config::Timing::LVGL_HZ);

FLASHMEM ms::ui::MenuRowKind toMenuRowKind(core::state::project::ProjectMenuRowKind kind) {
    switch (kind) {
        case core::state::project::ProjectMenuRowKind::Folder:
            return ms::ui::MenuRowKind::Folder;
        case core::state::project::ProjectMenuRowKind::Action:
            return ms::ui::MenuRowKind::Action;
        case core::state::project::ProjectMenuRowKind::Toggle:
            return ms::ui::MenuRowKind::Toggle;
        case core::state::project::ProjectMenuRowKind::Disabled:
            return ms::ui::MenuRowKind::Disabled;
        case core::state::project::ProjectMenuRowKind::Value:
        default:
            return ms::ui::MenuRowKind::Value;
    }
}

FLASHMEM core::state::project::ProjectTab tabAt(uint8_t index) {
    return static_cast<core::state::project::ProjectTab>(index);
}

FLASHMEM const char* tabIcon(core::state::project::ProjectTab tab) {
    switch (tab) {
        case core::state::project::ProjectTab::MUSIC:
            return standalone::icons::SCALE;
        case core::state::project::ProjectTab::TRANSPORT:
            return standalone::icons::TEMPO;
        case core::state::project::ProjectTab::STORAGE:
            return standalone::icons::STORAGE;
        case core::state::project::ProjectTab::ROUTING:
            return standalone::icons::ROUTING;
        case core::state::project::ProjectTab::OVERVIEW:
        default:
            return standalone::icons::HOME;
    }
}

FLASHMEM uint32_t tabAccentColor(core::state::project::ProjectTab tab) {
    switch (tab) {
        case core::state::project::ProjectTab::MUSIC:
            return theme::color::MACRO_5;
        case core::state::project::ProjectTab::TRANSPORT:
            return theme::color::MACRO_4;
        case core::state::project::ProjectTab::STORAGE:
            return theme::color::MACRO_2;
        case core::state::project::ProjectTab::ROUTING:
            return theme::color::MACRO_6;
        case core::state::project::ProjectTab::OVERVIEW:
        default:
            return theme::color::TEXT_SECONDARY;
    }
}

}  // namespace

FLASHMEM ProjectView::ProjectView(lv_obj_t* parent, StateRefs stateRefs)
    : state_refs_(stateRefs) {
    createLayout(parent);
    render_timer_ = core::app::makeExtmemUnique<core::ui::PausableLvglTimer>(
        RENDER_TIMER_PERIOD_MS,
        onRenderTimer,
        this
    );
    bindToState();
}

FLASHMEM ProjectView::~ProjectView() {
    render_timer_.reset();
    menu_.reset();
    frame_.reset();
    container_ = nullptr;
    body_container_ = nullptr;
    tab_strip_ = nullptr;
    tab_widgets_ = {};
}

FLASHMEM void ProjectView::onActivate() {
    if (!container_) return;

    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    scheduleRender(true);
}

FLASHMEM void ProjectView::onDeactivate() {
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }
    pauseRenderTimerIfIdle();
}

FLASHMEM void ProjectView::createLayout(lv_obj_t* parent) {
    frame_ = core::app::makeExtmemUnique<MainViewFrame>(parent);
    container_ = frame_->container();
    body_container_ = frame_->body();
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    tab_strip_ = lv_obj_create(body_container_);
    style::apply(tab_strip_)
        .size(LV_PCT(100), TAB_STRIP_HEIGHT)
        .transparent()
        .noBorder()
        .pad(0)
        .noScroll();
    lv_obj_set_layout(tab_strip_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab_strip_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        tab_strip_,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(tab_strip_, 3, 0);
    lv_obj_set_style_pad_top(tab_strip_, 2, 0);
    lv_obj_set_style_pad_bottom(tab_strip_, 2, 0);

    for (uint8_t i = 0; i < tab_widgets_.size(); ++i) {
        auto* tab = lv_obj_create(tab_strip_);
        tab_widgets_[i].container = tab;
        style::apply(tab).transparent().noBorder().pad(0).noScroll();
        lv_obj_set_height(tab, 18);
        lv_obj_set_style_radius(tab, 2, 0);
        lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(tab, 4, 0);
        lv_obj_set_style_pad_left(tab, 5, 0);
        lv_obj_set_style_pad_right(tab, 5, 0);
        lv_obj_set_style_pad_top(tab, 1, 0);
        lv_obj_set_style_pad_bottom(tab, 1, 0);

        auto* icon = lv_label_create(tab);
        tab_widgets_[i].icon = icon;
        standalone::icons::set(icon, tabIcon(tabAt(i)), standalone::icons::Size::M);
        lv_label_set_long_mode(icon, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);

        auto* label = lv_label_create(tab);
        tab_widgets_[i].label = label;
        lv_obj_set_style_text_font(label, fonts.inter_12_medium, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

    menu_ = core::app::makeExtmemUnique<ms::ui::MenuListView>(body_container_);
    if (menu_ && menu_->getElement()) {
        lv_obj_set_height(menu_->getElement(), 0);
        lv_obj_set_flex_grow(menu_->getElement(), 1);
    }
}

FLASHMEM void ProjectView::bindToState() {
    watcher_.watchAll(
        [this]() { requestRender(); },
        state_refs_.navigation.activeTab,
        state_refs_.navigation.currentNode,
        state_refs_.navigation.depth,
        state_refs_.navigation.focusedRow,
        state_refs_.navigation.physicalHoldActive,
        state_refs_.navigation.contentRevision,
        state_refs_.sequencerTracks.projectScaleRevisionSignal(),
        state_refs_.statusBar.tempo,
        state_refs_.midiSync.mode
    );
}

FLASHMEM void ProjectView::requestRender() {
    dirty_ = true;
    scheduleRender();
}

FLASHMEM void ProjectView::scheduleRender(bool ready) {
    dirty_ = true;
    if (render_timer_) {
        render_timer_->resume(ready);
    }
}

FLASHMEM void ProjectView::pauseRenderTimerIfIdle() {
    if (!render_timer_) return;
    if (dirty_ && container_ && !lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;
    render_timer_->pause();
}

FLASHMEM void ProjectView::render() {
    if (!dirty_ || !menu_ || !container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) {
        pauseRenderTimerIfIdle();
        return;
    }

    renderTabs();

    const auto page = core::state::project::buildProjectMenuPage(
        state_refs_.navigation,
        [this]() {
            core::state::project::ProjectMenuContext context{
                state_refs_.sequencerTracks.projectScaleSettings(),
                state_refs_.statusBar.tempo.get(),
                state_refs_.midiSync.mode.get()
            };
            for (uint8_t i = 0; i < context.outputMidiChannels.size(); ++i) {
                context.outputMidiChannels[i] =
                    state_refs_.sequencerTracks.track(i).midiChannel.get();
            }
            return context;
        }()
    );
    for (uint8_t i = 0; i < page.rowCount && i < rows_.size(); ++i) {
        const auto& source = page.rows[i];
        rows_[i] = ms::ui::MenuRow{
            .label = source.label,
            .value = source.displayValue(),
            .kind = toMenuRowKind(source.kind),
            .enabled = source.enabled,
        };
    }

    menu_->render(ms::ui::MenuListViewProps{
        .title = page.title,
        .meta = page.meta,
        .rows = rows_.data(),
        .rowCount = page.rowCount,
        .selectedIndex = page.selectedIndex,
        .dataRevision = page.dataRevision,
    });
    dirty_ = false;
    pauseRenderTimerIfIdle();
}

FLASHMEM void ProjectView::renderTabs() {
    const auto activeTab = state_refs_.navigation.activeTab.get();
    const bool holdActive = state_refs_.navigation.physicalHoldActive.get();
    if (tabs_rendered_ &&
        rendered_active_tab_ == activeTab &&
        rendered_hold_active_ == holdActive) {
        return;
    }

    for (uint8_t i = 0; i < tab_widgets_.size(); ++i) {
        auto& widgets = tab_widgets_[i];
        if (!widgets.container || !widgets.icon || !widgets.label) continue;

        const auto tab = tabAt(i);
        const bool active = tab == activeTab;
        const uint32_t accent = tabAccentColor(tab);

        if (!widgets.contentInitialized) {
            standalone::icons::set(widgets.icon, tabIcon(tab), standalone::icons::Size::M);
            lv_label_set_text(widgets.label, core::state::project::projectTabLabel(tab));
            widgets.contentInitialized = true;
        }

        if (widgets.styleInitialized &&
            widgets.active == active &&
            widgets.holdActive == holdActive) {
            continue;
        }

        lv_obj_set_width(widgets.container, active ? TAB_ACTIVE_WIDTH : TAB_INACTIVE_WIDTH);
        if (!widgets.styleInitialized || widgets.active != active) {
            lv_obj_set_style_bg_color(widgets.container, lv_color_hex(accent), 0);
        }
        lv_obj_set_style_bg_opa(
            widgets.container,
            active ? (holdActive ? LV_OPA_30 : static_cast<lv_opa_t>(38)) : LV_OPA_TRANSP,
            0
        );
        lv_obj_set_style_text_color(
            widgets.icon,
            lv_color_hex(active ? theme::color::TEXT_PRIMARY : accent),
            0
        );
        lv_obj_set_style_text_opa(widgets.icon, active ? LV_OPA_COVER : (holdActive ? LV_OPA_70 : LV_OPA_40), 0);
        lv_obj_set_style_text_color(
            widgets.label,
            lv_color_hex(active ? theme::color::TEXT_PRIMARY : theme::color::INACTIVE),
            0
        );
        lv_obj_set_style_text_opa(widgets.label, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (active) {
            lv_obj_clear_flag(widgets.label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(widgets.label, LV_OBJ_FLAG_HIDDEN);
        }
        widgets.active = active;
        widgets.holdActive = holdActive;
        widgets.styleInitialized = true;
    }

    tabs_rendered_ = true;
    rendered_active_tab_ = activeTab;
    rendered_hold_active_ = holdActive;
}

FLASHMEM void ProjectView::onRenderTimer(lv_timer_t* timer) {
    auto* self = static_cast<ProjectView*>(lv_timer_get_user_data(timer));
    if (!self) return;
    self->render();
}

}  // namespace core::ui
