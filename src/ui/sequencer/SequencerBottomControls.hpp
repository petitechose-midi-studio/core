#pragma once

#include <array>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/sequencer/SequencerUiState.hpp"

namespace core::ui {

struct SequencerBottomControlsProps {
    bool selectingQuickControls = false;
    core::state::sequencer::PatternQuickControlItem focusedQuickControl =
        core::state::sequencer::PatternQuickControlItem::OFFSET;
    int8_t offsetSteps = 0;
    uint8_t stepsPerBeat = 2;
    uint8_t length = 8;
    bool microSequenceContext = false;
};

class SequencerBottomControls : public oc::ui::lvgl::IWidget {
public:
    explicit SequencerBottomControls(lv_obj_t* parent);
    ~SequencerBottomControls() override;

    SequencerBottomControls(const SequencerBottomControls&) = delete;
    SequencerBottomControls& operator=(const SequencerBottomControls&) = delete;

    void render(const SequencerBottomControlsProps& props);

    lv_obj_t* getElement() const override { return container_; }

private:
    using QuickItem = core::state::sequencer::PatternQuickControlItem;

    struct QuickControlWidgets {
        QuickItem item = QuickItem::OFFSET;
        lv_obj_t* slot = nullptr;
        lv_obj_t* content = nullptr;
        lv_obj_t* label = nullptr;
        lv_obj_t* value = nullptr;
        std::array<char, 16> renderedLabel{};
        std::array<char, 16> renderedValue{};
        bool labelInitialized = false;
        bool valueInitialized = false;
        bool highlightedInitialized = false;
        bool highlighted = false;
    };

    void createUI(lv_obj_t* parent);
    void createQuickControl(
        QuickControlWidgets& widgets,
        lv_obj_t* parent,
        QuickItem item,
        const char* labelText,
        lv_coord_t slotWidth,
        lv_flex_align_t align,
        const lv_font_t* valueFont
    );
    void renderQuickControls(const SequencerBottomControlsProps& props);
    void renderQuickControl(
        QuickControlWidgets& widgets,
        const SequencerBottomControlsProps& props
    );
    void positionQuickControlCursor(const SequencerBottomControlsProps& props);
    void ensureCursorGeometry();
    lv_obj_t* quickControlAnchor(QuickItem item) const;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* top_row_ = nullptr;
    QuickControlWidgets length_;
    QuickControlWidgets offset_;
    QuickControlWidgets division_;
    lv_obj_t* quick_cursor_ = nullptr;
    bool has_rendered_ = false;
    SequencerBottomControlsProps rendered_props_{};
    bool cursor_visible_cache_ = false;
    lv_coord_t cursor_x_cache_ = 0;
    lv_coord_t cursor_y_cache_ = 0;
    bool geometry_cache_initialized_ = false;
    lv_coord_t geometry_cache_width_ = 0;
    lv_coord_t geometry_cache_height_ = 0;
    std::array<lv_point_t, 3> cursor_positions_{};
};

}  // namespace core::ui
