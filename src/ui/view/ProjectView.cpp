#include "ui/view/ProjectView.hpp"

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"
#include "ui/view/RetainedViewRenderPolicy.hpp"

namespace core::ui {

namespace {

namespace style = oc::ui::lvgl::style;
namespace theme = standalone::theme;

constexpr lv_coord_t TAB_STRIP_HEIGHT = 24;
constexpr lv_coord_t TAB_ACTIVE_WIDTH = 92;
constexpr lv_coord_t TAB_INACTIVE_WIDTH = 30;
constexpr uint32_t RENDER_TIMER_PERIOD_MS =
    (Config::Timing::RETAINED_VIEW_HZ > 1000)
        ? 1
        : ((1000 + Config::Timing::RETAINED_VIEW_HZ - 1) /
           Config::Timing::RETAINED_VIEW_HZ);

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

FLASHMEM bool isProjectNameEditorNode(core::state::project::ProjectNodeId node) {
    return node == core::state::project::ProjectNodeId::SAVE_AS_PROJECT_NAME ||
           node == core::state::project::ProjectNodeId::RENAME_PROJECT_NAME ||
           node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_RENAME;
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
        case core::state::project::ProjectTab::MODULATORS:
            return standalone::icons::MACRO_MODULATION;
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
        case core::state::project::ProjectTab::MODULATORS:
            return theme::color::MACRO_MODULATION;
        case core::state::project::ProjectTab::OVERVIEW:
        default:
            return theme::color::TEXT_SECONDARY;
    }
}

}  // namespace

FLASHMEM ProjectView::ProjectView(lv_obj_t* parent, StateRefs stateRefs)
    : state_refs_(stateRefs) {
    createLayout(parent);
    if (!frame_ || !frame_->valid() || !container_ || !body_container_ ||
        !interaction_container_ || !center_column_ || !tab_strip_ ||
        !left_action_strip_ || !left_action_strip_->getElement() ||
        !bottom_action_strip_ || !bottom_action_strip_->getElement() ||
        !menu_ || !menu_->getElement() || !modulator_registry_ ||
        !modulator_registry_->getElement() || !modulator_workspace_ ||
        !modulator_workspace_->valid() || !project_name_keyboard_ ||
        !project_name_keyboard_->valid()) {
        return;
    }
    render_scheduler_ =
        core::app::makeExtmemUnique<core::ui::CoalescedLvglRenderScheduler>(
            core::ui::renderSchedulerDebugLabel("ProjectView"),
            &ProjectView::drainRender,
            this,
            RENDER_TIMER_PERIOD_MS,
            &ProjectView::canDrainRender
        );
    if (!render_scheduler_ || !render_scheduler_->valid() || !bindToState()) return;
    initialized_ = true;
}

FLASHMEM ProjectView::~ProjectView() {
    render_scheduler_.reset();
    modulator_workspace_.reset();
    modulator_registry_.reset();
    menu_.reset();
    bottom_action_strip_.reset();
    left_action_strip_.reset();
    project_name_keyboard_.reset();
    frame_.reset();
    container_ = nullptr;
    body_container_ = nullptr;
    interaction_container_ = nullptr;
    center_column_ = nullptr;
    tab_strip_ = nullptr;
    tab_widgets_ = {};
}

FLASHMEM void ProjectView::onActivate() {
    if (!container_) return;

    RetainedViewRenderPolicy::show(container_);
    if (render_scheduler_) {
        render_scheduler_->request(RENDER_CONTENT, true);
    }
}

FLASHMEM void ProjectView::onDeactivate() {
    if (render_scheduler_) render_scheduler_->pause();
    RetainedViewRenderPolicy::hide(container_);
}

FLASHMEM void ProjectView::createLayout(lv_obj_t* parent) {
    frame_ = core::app::makeExtmemUnique<MainViewFrame>(parent);
    if (!frame_ || !frame_->valid()) return;
    container_ = frame_->container();
    body_container_ = frame_->body();
    RetainedViewRenderPolicy::initializeHidden(container_);

    tab_strip_ = lv_obj_create(body_container_);
    if (!tab_strip_) return;
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
        if (!tab) return;
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
        if (!icon) return;
        tab_widgets_[i].icon = icon;
        standalone::icons::set(icon, tabIcon(tabAt(i)), standalone::icons::Size::M);
        lv_label_set_long_mode(icon, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);

        auto* label = lv_label_create(tab);
        if (!label) return;
        tab_widgets_[i].label = label;
        lv_obj_set_style_text_font(label, fonts.inter_12_medium, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

    frame_->createInteractionRow();
    interaction_container_ = frame_->interactionRow();
    if (!interaction_container_) return;

    left_action_strip_ = core::app::makeExtmemUnique<ContextActionStrip>(
        interaction_container_,
        ContextActionStripOrientation::VERTICAL,
        ContextActionStripVerticalLayout::SPREAD
    );
    if (!left_action_strip_ || !left_action_strip_->getElement()) return;
    if (left_action_strip_ && left_action_strip_->getElement()) {
        lv_obj_set_width(left_action_strip_->getElement(), 32);
        lv_obj_set_style_pad_left(left_action_strip_->getElement(), 3, 0);
        lv_obj_set_style_pad_right(left_action_strip_->getElement(), 1, 0);
    }

    frame_->createCenterColumn();
    center_column_ = frame_->centerColumn();
    if (!center_column_) return;

    menu_ = core::app::makeExtmemUnique<ms::ui::MenuListView>(center_column_);
    if (menu_ && menu_->getElement()) {
        lv_obj_set_height(menu_->getElement(), 0);
        lv_obj_set_flex_grow(menu_->getElement(), 1);
    }

    modulator_registry_ = core::app::makeExtmemUnique<
        ms::ui::VirtualListKeyValueOverlay>(center_column_);
    if (!modulator_registry_ || !modulator_registry_->getElement()) return;

    modulator_workspace_ = core::app::makeExtmemUnique<
        core::ui::project::ProjectModulatorWorkspace>(center_column_);
    if (!modulator_workspace_ || !modulator_workspace_->valid()) return;

    bottom_action_strip_ = core::app::makeExtmemUnique<ContextActionStrip>(
        body_container_,
        ContextActionStripOrientation::HORIZONTAL
    );
    if (!bottom_action_strip_ || !bottom_action_strip_->getElement()) return;

    project_name_keyboard_.emplace(center_column_);
}

FLASHMEM bool ProjectView::bindToState() {
    watcher_.bind<&ProjectView::requestRender>(*this, 0, "Project.view");
    const bool contentBound = watcher_.watchAll(
        state_refs_.navigation.activeTab,
        state_refs_.navigation.currentNode,
        state_refs_.navigation.depth,
        state_refs_.navigation.focusedRow,
        state_refs_.navigation.physicalHoldActive,
        state_refs_.navigation.contentRevision,
        state_refs_.navigation.modulatorGuard,
        state_refs_.navigation.modulatorClipboardGuard,
        state_refs_.projectTracks.revision,
        state_refs_.sequencerTracks.projectScaleRevisionSignal(),
        state_refs_.statusBar.tempo,
        state_refs_.midiSync.mode
    );
    modulator_capture_watcher_.bind<
        &ProjectView::requestModulatorCaptureRender
    >(*this, 2, "Project.modulator-capture");
    const bool captureBound = modulator_capture_watcher_.watch(
        state_refs_.macroUi.recordedShapeCaptureRevision
    );
    return contentBound && captureBound;
}

void ProjectView::requestRender() {
    if (render_scheduler_) render_scheduler_->request(RENDER_CONTENT);
}

void ProjectView::requestModulatorCaptureRender() {
    if (render_scheduler_) {
        render_scheduler_->request(RENDER_MODULATOR_CAPTURE);
    }
}

void ProjectView::render() {
    if (!menu_ || !RetainedViewRenderPolicy::visible(container_)) return;

    renderTabs();

    const auto node = state_refs_.navigation.currentNode.get();
    const bool modulatorPage =
        node == core::state::project::ProjectNodeId::MODULATORS_ROOT ||
        node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS ||
        node == core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS ||
        node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER ||
        node == core::state::project::ProjectNodeId::MODULATOR_TRIGGER ||
        node ==
            core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER;
    if (modulatorPage) {
        if (project_name_keyboard_) {
            project_name_keyboard_->setVisible(false);
        }
        if (menu_) menu_->hide();
        renderModulators();
        return;
    }
    if (modulator_registry_) {
        modulator_registry_->render({.visible = false});
    }
    if (modulator_workspace_) {
        modulator_workspace_->render({.visible = false});
    }

    const bool keyboardActive = isProjectNameEditorNode(state_refs_.navigation.currentNode.get());
    renderKeyboardActionStrips(keyboardActive);

    if (keyboardActive) {
        if (menu_) menu_->hide();
        if (project_name_keyboard_) {
            project_name_keyboard_->render({
                .visible = true,
                .title =
                    node ==
                            core::state::project::ProjectNodeId::
                                SAVE_AS_PROJECT_NAME
                        ? "SAVE AS"
                        : "RENAME",
                .meta = state_refs_.navigation.lifecycleFeedback.empty()
                    ? (node ==
                               core::state::project::ProjectNodeId::
                                   MODULATOR_SOURCE_RENAME
                           ? "SOURCE"
                           : "")
                    : state_refs_.navigation.lifecycleFeedback.get(),
                .name =
                    state_refs_.navigation.editingProjectSlug.data(),
                .selectedKey =
                    state_refs_.navigation.projectNameKeyIndex,
                .shiftActive =
                    state_refs_.navigation.projectNameShiftActive,
            });
        }
        return;
    }

    const auto page = core::state::project::buildProjectMenuPage(
        state_refs_.navigation,
        [this]() {
            core::state::project::ProjectMenuContext context{
                state_refs_.sequencerTracks.projectScaleSettings(),
                state_refs_.statusBar.tempo.get(),
                state_refs_.midiSync.mode.get()
            };
            context.projectId = state_refs_.project.metadata.id;
            context.projectName = state_refs_.project.metadata.name;
            context.projectDirty = state_refs_.project.metadata.dirty;
            context.projectHasSavedIdentity = state_refs_.project.metadata.hasSavedIdentity;
            for (uint8_t i = 0; i < context.outputMidiChannels.size(); ++i) {
                context.outputMidiChannels[i] =
                    core::state::project::projectTrackMidiChannel(
                        state_refs_.projectTracks,
                        i
                    );
            }
            return context;
        }()
    );

    if (project_name_keyboard_) {
        project_name_keyboard_->setVisible(false);
    }
    if (menu_) menu_->show();

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
        .meta = state_refs_.navigation.lifecycleFeedback.empty()
            ? page.displayMeta()
            : state_refs_.navigation.lifecycleFeedback.get(),
        .rows = rows_.data(),
        .rowCount = page.rowCount,
        .selectedIndex = page.selectedIndex,
        .dataRevision = page.dataRevision,
    });
}



void ProjectView::renderKeyboardActionStrips(bool visible) {
    if (left_action_strip_) {
        left_action_strip_->render(
            core::ui::project::ProjectNameKeyboardView::
                leftActionStripProps(
                    visible,
                    state_refs_.navigation.projectNameShiftActive
                )
        );
    }
    if (bottom_action_strip_) {
        bottom_action_strip_->render(
            core::ui::project::ProjectNameKeyboardView::
                bottomActionStripProps(
                    visible,
                    state_refs_.statusBar.playing.get()
                )
        );
    }
}

void ProjectView::renderTabs() {
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

bool ProjectView::canDrainRender(void* context) {
    const auto* self = static_cast<const ProjectView*>(context);
    return self && RetainedViewRenderPolicy::visible(self->container_);
}

void ProjectView::drainRender(void* context, uint32_t flags) {
    auto* self = static_cast<ProjectView*>(context);
    if (!self) return;
    if ((flags & RENDER_CONTENT) != 0U) {
        self->render();
        return;
    }
    if ((flags & RENDER_MODULATOR_CAPTURE) != 0U) {
        self->renderModulatorCapture();
    }
}

}  // namespace core::ui
