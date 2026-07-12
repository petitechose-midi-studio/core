#include "ui/view/ProjectView.hpp"

#include <cstring>

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
constexpr lv_coord_t KEYBOARD_KEY_W = 25;
constexpr lv_coord_t KEYBOARD_KEY_H = 24;
constexpr lv_coord_t KEYBOARD_KEY_GAP = 3;
constexpr lv_coord_t KEYBOARD_GRID_X = 5;
constexpr lv_coord_t KEYBOARD_GRID_Y = 38;
constexpr lv_coord_t KEYBOARD_ROW_CENTER_OFFSET = (KEYBOARD_KEY_W + KEYBOARD_KEY_GAP) / 2;
constexpr lv_coord_t KEYBOARD_LABEL_Y_OFFSET = 0;
constexpr uint32_t RENDER_TIMER_PERIOD_MS =
    (Config::Timing::LVGL_HZ > 1000)
        ? 1
        : ((1000 + Config::Timing::LVGL_HZ - 1) / Config::Timing::LVGL_HZ);

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
           node == core::state::project::ProjectNodeId::RENAME_PROJECT_NAME;
}

FLASHMEM void setLabelTextIfChanged(lv_obj_t* label, const char* text) {
    if (!label) return;
    const char* next = text ? text : "";
    const char* current = lv_label_get_text(label);
    if (current && std::strcmp(current, next) == 0) return;
    lv_label_set_text(label, next);
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

FLASHMEM void configureKeyboardKeyLabel(
    lv_obj_t* label,
    const char* text
) {
    if (!label) return;

    lv_obj_set_style_text_font(label, fonts.inter_13_bold, 0);
    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, KEYBOARD_LABEL_Y_OFFSET);
}

FLASHMEM bool isKeyboardLetter(char character) {
    return character >= 'a' && character <= 'z';
}

FLASHMEM char shiftedKeyboardCharacter(char character) {
    return isKeyboardLetter(character)
        ? static_cast<char>(character - 'a' + 'A')
        : character;
}

FLASHMEM void setKeyboardKeyTextStyle(lv_obj_t* label, bool selected) {
    if (!label) return;
    lv_obj_set_style_text_color(
        label,
        lv_color_hex(selected ? theme::color::TEXT_PRIMARY : theme::color::TEXT_SECONDARY),
        0
    );
    lv_obj_set_style_text_opa(
        label,
        selected ? LV_OPA_COVER : LV_OPA_70,
        0
    );
}

FLASHMEM ContextActionStripSlotProps keyboardStandaloneIconSlot(
    const char* icon,
    ContextActionStripTone tone = ContextActionStripTone::NEUTRAL,
    standalone::icons::Size iconSize = standalone::icons::Size::M,
    ContextActionStripVisualState visualState = ContextActionStripVisualState::ACTIVE
) {
    return ContextActionStripSlotProps{
        .visualState = visualState,
        .tone = tone,
        .showIcon = true,
        .icon = icon,
        .iconUsesStandaloneFont = true,
        .iconSize = iconSize,
    };
}

FLASHMEM ContextActionStripSlotProps keyboardLabelSlot(
    const char* label,
    ContextActionStripTone tone = ContextActionStripTone::NEUTRAL
) {
    return ContextActionStripSlotProps{
        .visualState = ContextActionStripVisualState::ACTIVE,
        .tone = tone,
        .showLabel = true,
        .label = label,
    };
}

FLASHMEM ContextActionStripProps keyboardLeftActionStripProps(bool visible, bool shiftActive) {
    ContextActionStripProps props;
    props.visible = visible;
    if (!visible) return props;
    props.slots[1] = keyboardStandaloneIconSlot(
        standalone::icons::MODIFIER_SHIFT,
        ContextActionStripTone::NEUTRAL,
        standalone::icons::Size::M,
        shiftActive
            ? ContextActionStripVisualState::ACTIVE
            : ContextActionStripVisualState::DIM
    );
    props.slots[2] = keyboardStandaloneIconSlot(
        standalone::icons::ACTION_CLEAR,
        ContextActionStripTone::DESTRUCTIVE,
        standalone::icons::Size::S
    );
    return props;
}

FLASHMEM ContextActionStripProps keyboardBottomActionStripProps(bool visible) {
    ContextActionStripProps props;
    props.visible = visible;
    if (!visible) return props;
    props.slots[0] = keyboardStandaloneIconSlot(
        standalone::icons::ACTION_BACKWARD,
        ContextActionStripTone::WARNING
    );
    props.slots[1] = keyboardLabelSlot("Space");
    props.slots[2] = keyboardStandaloneIconSlot(
        standalone::icons::ACTION_VALIDATE,
        ContextActionStripTone::POSITIVE
    );
    return props;
}

}  // namespace

FLASHMEM ProjectView::ProjectView(lv_obj_t* parent, StateRefs stateRefs)
    : state_refs_(stateRefs) {
    createLayout(parent);
    if (!frame_ || !frame_->valid() || !container_ || !body_container_ ||
        !interaction_container_ || !center_column_ || !tab_strip_ ||
        !left_action_strip_ || !left_action_strip_->getElement() ||
        !bottom_action_strip_ || !bottom_action_strip_->getElement() ||
        !menu_ || !menu_->getElement() || !keyboard_container_ ||
        !keyboard_title_ || !keyboard_meta_ || !keyboard_name_box_ ||
        !keyboard_name_label_) {
        return;
    }
    for (uint8_t i = 0; i < keyboard_keys_.size(); ++i) {
        const auto& widgets = keyboard_keys_[i];
        const auto& cell = core::state::project::projectNameKeyboardCellAt(i);
        if (!widgets.container || !widgets.label ||
            (isKeyboardLetter(cell.character) && !widgets.shiftLabel)) {
            return;
        }
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
    menu_.reset();
    bottom_action_strip_.reset();
    left_action_strip_.reset();
    keyboard_container_ = nullptr;
    keyboard_title_ = nullptr;
    keyboard_meta_ = nullptr;
    keyboard_name_box_ = nullptr;
    keyboard_name_label_ = nullptr;
    keyboard_keys_ = {};
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
    if (render_scheduler_) render_scheduler_->request(1U, true);
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

    bottom_action_strip_ = core::app::makeExtmemUnique<ContextActionStrip>(
        body_container_,
        ContextActionStripOrientation::HORIZONTAL
    );
    if (!bottom_action_strip_ || !bottom_action_strip_->getElement()) return;

    createKeyboardLayout();
}

FLASHMEM bool ProjectView::bindToState() {
    watcher_.bind<&ProjectView::requestRender>(*this, 0, "Project.view");
    return watcher_.watchAll(
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

void ProjectView::requestRender() {
    if (render_scheduler_) render_scheduler_->request(1U);
}

void ProjectView::render() {
    if (!menu_ || !RetainedViewRenderPolicy::visible(container_)) return;

    renderTabs();

    const bool keyboardActive = isProjectNameEditorNode(state_refs_.navigation.currentNode.get());
    renderKeyboardActionStrips(keyboardActive);

    if (keyboardActive) {
        if (menu_) menu_->hide();
        setKeyboardVisible(true);
        renderKeyboard();
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
            context.projectOverwriteSafe = state_refs_.project.metadata.overwriteSafe;
            for (uint8_t i = 0; i < context.outputMidiChannels.size(); ++i) {
                context.outputMidiChannels[i] =
                    state_refs_.sequencerTracks.track(i).midiChannel.get();
            }
            return context;
        }()
    );

    setKeyboardVisible(false);
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
            keyboardLeftActionStripProps(visible, state_refs_.navigation.projectNameShiftActive)
        );
    }
    if (bottom_action_strip_) {
        bottom_action_strip_->render(keyboardBottomActionStripProps(visible));
    }
}

FLASHMEM void ProjectView::createKeyboardLayout() {
    if (!center_column_) return;

    keyboard_container_ = lv_obj_create(center_column_);
    if (!keyboard_container_) return;
    style::apply(keyboard_container_)
        .size(LV_PCT(100), LV_PCT(100))
        .transparent()
        .noBorder()
        .pad(0)
        .noScroll();
    lv_obj_set_height(keyboard_container_, 0);
    lv_obj_set_flex_grow(keyboard_container_, 1);
    lv_obj_add_flag(keyboard_container_, LV_OBJ_FLAG_HIDDEN);

    keyboard_title_ = lv_label_create(keyboard_container_);
    if (!keyboard_title_) return;
    lv_label_set_text(keyboard_title_, "");
    lv_obj_set_pos(keyboard_title_, 10, 4);
    lv_obj_set_size(keyboard_title_, 78, 24);
    lv_obj_set_style_text_font(keyboard_title_, fonts.inter_14_bold, 0);
    lv_obj_set_style_text_color(keyboard_title_, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
    lv_label_set_long_mode(keyboard_title_, LV_LABEL_LONG_CLIP);

    keyboard_meta_ = lv_label_create(keyboard_container_);
    if (!keyboard_meta_) return;
    lv_label_set_text(keyboard_meta_, "");
    lv_obj_set_pos(keyboard_meta_, 224, 5);
    lv_obj_set_size(keyboard_meta_, 88, 20);
    lv_obj_set_style_text_font(keyboard_meta_, fonts.inter_12_medium, 0);
    lv_obj_set_style_text_color(keyboard_meta_, lv_color_hex(theme::color::MACRO_2), 0);
    lv_obj_set_style_text_opa(keyboard_meta_, LV_OPA_90, 0);
    lv_obj_set_style_text_align(keyboard_meta_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(keyboard_meta_, LV_LABEL_LONG_DOT);

    keyboard_name_box_ = lv_obj_create(keyboard_container_);
    if (!keyboard_name_box_) return;
    style::apply(keyboard_name_box_).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_pos(keyboard_name_box_, 90, 2);
    lv_obj_set_size(keyboard_name_box_, 130, 26);
    lv_obj_set_style_radius(keyboard_name_box_, 3, 0);
    lv_obj_set_style_bg_color(keyboard_name_box_, lv_color_hex(theme::color::KNOB_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(keyboard_name_box_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(keyboard_name_box_, 1, 0);
    lv_obj_set_style_border_color(keyboard_name_box_, lv_color_hex(theme::color::INACTIVE), 0);
    lv_obj_set_style_border_opa(keyboard_name_box_, LV_OPA_70, 0);

    keyboard_name_label_ = lv_label_create(keyboard_name_box_);
    if (!keyboard_name_label_) return;
    lv_label_set_text(keyboard_name_label_, "");
    lv_obj_set_pos(keyboard_name_label_, 6, 2);
    lv_obj_set_size(keyboard_name_label_, 118, 18);
    lv_obj_set_style_text_font(keyboard_name_label_, fonts.inter_14_semibold, 0);
    lv_obj_set_style_text_color(keyboard_name_label_, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
    lv_label_set_long_mode(keyboard_name_label_, LV_LABEL_LONG_DOT);

    for (uint8_t i = 0; i < keyboard_keys_.size(); ++i) {
        const auto& cell = core::state::project::projectNameKeyboardCellAt(i);
        const lv_coord_t centeredOffset =
            (cell.row == 2 || cell.row == 3) ? KEYBOARD_ROW_CENTER_OFFSET : 0;
        const lv_coord_t x = static_cast<lv_coord_t>(
            KEYBOARD_GRID_X + centeredOffset + cell.column * (KEYBOARD_KEY_W + KEYBOARD_KEY_GAP)
        );
        const lv_coord_t y = static_cast<lv_coord_t>(
            KEYBOARD_GRID_Y + cell.row * (KEYBOARD_KEY_H + KEYBOARD_KEY_GAP)
        );
        const lv_coord_t w = static_cast<lv_coord_t>(
            cell.columnSpan * KEYBOARD_KEY_W + (cell.columnSpan - 1U) * KEYBOARD_KEY_GAP
        );

        auto& widgets = keyboard_keys_[i];
        widgets.container = lv_obj_create(keyboard_container_);
        if (!widgets.container) return;
        style::apply(widgets.container).transparent().noBorder().pad(0).noScroll();
        lv_obj_set_pos(widgets.container, x, y);
        lv_obj_set_size(widgets.container, w, KEYBOARD_KEY_H);
        lv_obj_set_style_radius(widgets.container, 3, 0);
        lv_obj_set_style_border_width(widgets.container, 1, 0);

        widgets.label = lv_label_create(widgets.container);
        if (!widgets.label) return;
        configureKeyboardKeyLabel(widgets.label, cell.label);
        if (isKeyboardLetter(cell.character)) {
            char shiftedText[2] = {shiftedKeyboardCharacter(cell.character), '\0'};
            widgets.shiftLabel = lv_label_create(widgets.container);
            if (!widgets.shiftLabel) return;
            configureKeyboardKeyLabel(widgets.shiftLabel, shiftedText);
            lv_obj_add_flag(widgets.shiftLabel, LV_OBJ_FLAG_HIDDEN);
        }
        renderKeyboardKey(i, false, true);
    }
}

void ProjectView::renderKeyboard() {
    if (!keyboard_container_) return;

    const auto node = state_refs_.navigation.currentNode.get();
    setLabelTextIfChanged(
        keyboard_title_,
        node == core::state::project::ProjectNodeId::SAVE_AS_PROJECT_NAME
            ? "SAVE AS"
            : "RENAME"
    );
    setLabelTextIfChanged(
        keyboard_meta_,
        state_refs_.navigation.lifecycleFeedback.empty()
            ? ""
            : state_refs_.navigation.lifecycleFeedback.get()
    );
    setLabelTextIfChanged(
        keyboard_name_label_,
        state_refs_.navigation.editingProjectSlug.data()
    );

    const uint8_t selected = state_refs_.navigation.projectNameKeyIndex;
    const bool shiftActive = state_refs_.navigation.projectNameShiftActive;
    if (rendered_keyboard_shift_ != shiftActive) {
        applyKeyboardShiftVisibility(shiftActive);
        rendered_keyboard_shift_ = shiftActive;
    }

    if (rendered_keyboard_selected_ != selected) {
        if (rendered_keyboard_selected_ < keyboard_keys_.size()) {
            renderKeyboardKey(rendered_keyboard_selected_, false);
        }
        renderKeyboardKey(selected, true);
        rendered_keyboard_selected_ = selected;
    }
}

void ProjectView::renderKeyboardKey(uint8_t index, bool selected, bool force) {
    if (index >= keyboard_keys_.size()) return;

    auto& widgets = keyboard_keys_[index];
    if (!widgets.container || !widgets.label) return;
    if (!force && widgets.styleInitialized && widgets.selected == selected) return;

    const uint32_t accent = theme::color::MACRO_2;

    lv_obj_set_style_bg_color(
        widgets.container,
        lv_color_hex(selected ? accent : theme::color::KNOB_BACKGROUND),
        0
    );
    lv_obj_set_style_bg_opa(
        widgets.container,
        selected ? LV_OPA_80 : LV_OPA_20,
        0
    );
    lv_obj_set_style_border_color(
        widgets.container,
        lv_color_hex(selected ? accent : theme::color::INACTIVE),
        0
    );
    lv_obj_set_style_border_opa(
        widgets.container,
        selected ? LV_OPA_COVER : LV_OPA_40,
        0
    );
    setKeyboardKeyTextStyle(widgets.label, selected);
    setKeyboardKeyTextStyle(widgets.shiftLabel, selected);

    widgets.selected = selected;
    widgets.styleInitialized = true;
}

void ProjectView::applyKeyboardShiftVisibility(bool shiftActive) {
    for (auto& widgets : keyboard_keys_) {
        if (!widgets.label || !widgets.shiftLabel || widgets.shiftVisible == shiftActive) continue;
        if (shiftActive) {
            lv_obj_add_flag(widgets.label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(widgets.shiftLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(widgets.label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(widgets.shiftLabel, LV_OBJ_FLAG_HIDDEN);
        }
        widgets.shiftVisible = shiftActive;
    }
}

void ProjectView::setKeyboardVisible(bool visible) {
    if (!keyboard_container_ || keyboard_visible_ == visible) return;
    keyboard_visible_ = visible;
    if (visible) {
        lv_obj_clear_flag(keyboard_container_, LV_OBJ_FLAG_HIDDEN);
    } else {
        applyKeyboardShiftVisibility(false);
        lv_obj_add_flag(keyboard_container_, LV_OBJ_FLAG_HIDDEN);
        rendered_keyboard_selected_ =
            core::state::project::PROJECT_NAME_KEYBOARD_CELL_COUNT;
        rendered_keyboard_shift_ = false;
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
    if ((flags & 1U) == 0) return;
    auto* self = static_cast<ProjectView*>(context);
    if (self) self->render();
}

}  // namespace core::ui
