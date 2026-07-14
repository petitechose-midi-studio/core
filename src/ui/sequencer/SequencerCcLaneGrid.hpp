#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

namespace core::ui {

struct SequencerCcLaneGridCell {
    bool visible = false;
    bool authored = false;
    bool focused = false;
    bool playhead = false;
    uint8_t step = 0;
    uint8_t value = 0;
};

struct SequencerCcLaneGridProps {
    static constexpr size_t CELL_COUNT = 8;

    bool visible = false;
    const char* title = "";
    const char* meta = "";
    const char* hint = "";
    uint32_t accentColor = 0;
    uint32_t statusColor = 0;
    std::array<SequencerCcLaneGridCell, CELL_COUNT> cells{};
};

/** Retained eight-step CC editor with explicit authored/focus/playhead states. */
class SequencerCcLaneGrid : public oc::ui::lvgl::IWidget {
public:
    explicit SequencerCcLaneGrid(lv_obj_t* parent);
    ~SequencerCcLaneGrid() override;

    SequencerCcLaneGrid(const SequencerCcLaneGrid&) = delete;
    SequencerCcLaneGrid& operator=(const SequencerCcLaneGrid&) = delete;

    void render(const SequencerCcLaneGridProps& props);

    lv_obj_t* getElement() const override { return root_; }

private:
    static constexpr size_t CELL_COUNT = SequencerCcLaneGridProps::CELL_COUNT;

    struct CellWidgets {
        lv_obj_t* root = nullptr;
        lv_obj_t* playhead = nullptr;
        lv_obj_t* step = nullptr;
        lv_obj_t* plot = nullptr;
        lv_obj_t* fill = nullptr;
        lv_obj_t* value = nullptr;
        std::array<char, 4> stepText{};
        std::array<char, 5> valueText{};
        bool rendered = false;
        bool visible = false;
        bool authored = false;
        bool focused = false;
        bool playheadVisible = false;
        uint8_t valueCache = 0;
        uint32_t accentColor = 0;
    };

    void createUi(lv_obj_t* parent);
    void createCell(size_t index);
    void renderCell(
        CellWidgets& widgets,
        const SequencerCcLaneGridCell& cell,
        uint32_t accentColor
    );

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* meta_ = nullptr;
    lv_obj_t* grid_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    std::array<CellWidgets, CELL_COUNT> cells_{};
    std::array<char, 48> titleText_{};
    std::array<char, 64> metaText_{};
    std::array<char, 64> hintText_{};
    uint32_t statusColor_ = 0;
    bool visible_ = false;
};

}  // namespace core::ui
