#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/sequencer/SequencerCcLaneDomain.hpp"

namespace core::ui {

struct SequencerCcLaneGridCell {
    bool visible = false;
    bool authored = false;
    bool focused = false;
    bool playhead = false;
    uint8_t step = 0;
    uint8_t value = 0;
    core::state::sequencer::SequencerCcLaneTransition transition =
        core::state::sequencer::SequencerCcLaneTransition::HOLD;
};

struct SequencerCcLaneGridCurveSample {
    uint8_t position = 0;
    uint8_t value = 0;
};

struct SequencerCcLaneGridCurveSegment {
    static constexpr size_t MAX_POINTS = 16;

    bool visible = false;
    uint8_t pointCount = 0;
    std::array<SequencerCcLaneGridCurveSample, MAX_POINTS> points{};
};

struct SequencerCcLaneGridProps {
    static constexpr size_t CELL_COUNT = 8;

    bool visible = false;
    const char* title = "";
    const char* meta = "";
    const char* hint = "";
    uint32_t accentColor = 0;
    uint32_t statusColor = 0;
    bool transitionPicker = false;
    core::state::sequencer::SequencerCcLaneTransition pickerSelection =
        core::state::sequencer::SequencerCcLaneTransition::HOLD;
    bool contextualHint = false;
    uint8_t hintSourceStep = 0;
    uint8_t hintTargetStep = 0;
    core::state::sequencer::SequencerCcLaneTransition hintTransition =
        core::state::sequencer::SequencerCcLaneTransition::HOLD;
    std::array<SequencerCcLaneGridCell, CELL_COUNT> cells{};
    std::array<SequencerCcLaneGridCurveSegment, CELL_COUNT - 1U> segments{};
};

/** Retained eight-step CC editor with explicit authored/focus/playhead states. */
enum class SequencerCcLaneGridLayout : uint8_t {
    OVERLAY,
    EMBEDDED,
};

class SequencerCcLaneGrid : public oc::ui::lvgl::IWidget {
public:
    explicit SequencerCcLaneGrid(
        lv_obj_t* parent,
        SequencerCcLaneGridLayout layout = SequencerCcLaneGridLayout::OVERLAY
    );
    ~SequencerCcLaneGrid() override;

    SequencerCcLaneGrid(const SequencerCcLaneGrid&) = delete;
    SequencerCcLaneGrid& operator=(const SequencerCcLaneGrid&) = delete;

    void render(const SequencerCcLaneGridProps& props);

    lv_obj_t* getElement() const override { return root_; }

private:
    static constexpr size_t CELL_COUNT = SequencerCcLaneGridProps::CELL_COUNT;

    void createUi(lv_obj_t* parent);
    void drawCurveSegment(lv_layer_t* layer,
                          const lv_area_t& surfaceArea,
                          size_t index,
                          lv_opa_t opacity,
                          lv_coord_t width);
    void drawSurface(lv_layer_t* layer);
    void invalidatePlayheadCell(size_t index);
    [[nodiscard]] bool staticVisualChanged(
        const SequencerCcLaneGridProps& props
    ) const;
    static void onSurfaceDrawEvent(lv_event_t* event);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* meta_ = nullptr;
    lv_obj_t* surface_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    std::array<lv_point_precise_t, SequencerCcLaneGridCurveSegment::MAX_POINTS>
        draw_points_{};
    SequencerCcLaneGridProps rendered_props_{};
    std::array<char, 48> titleText_{};
    std::array<char, 64> metaText_{};
    std::array<char, 64> hintText_{};
    uint32_t accentColor_ = 0;
    uint32_t statusColor_ = 0;
    bool rendered_ = false;
    bool visible_ = false;
    SequencerCcLaneGridLayout layout_ = SequencerCcLaneGridLayout::OVERLAY;
};

}  // namespace core::ui
