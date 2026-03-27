#pragma once

#include <array>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/sequencer/SequencerUiState.hpp"

namespace core::ui {

struct PatternQuickControlsProps {
    bool selecting = false;
    core::state::sequencer::PatternQuickControlItem focusedItem =
        core::state::sequencer::PatternQuickControlItem::CHANNEL;
    uint8_t midiChannel = 0;
    uint8_t stepsPerBeat = 2;
    uint8_t length = 8;
};

class PatternQuickControls : public oc::ui::lvgl::IWidget {
public:
    explicit PatternQuickControls(lv_obj_t* parent);
    ~PatternQuickControls() override;

    PatternQuickControls(const PatternQuickControls&) = delete;
    PatternQuickControls& operator=(const PatternQuickControls&) = delete;

    void render(const PatternQuickControlsProps& props);

    lv_obj_t* getElement() const override { return container_; }

private:
    static constexpr size_t ITEM_COUNT = 3;

    void createUI(lv_obj_t* parent);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* selection_cursor_ = nullptr;
    std::array<lv_obj_t*, ITEM_COUNT> items_{};
    std::array<lv_obj_t*, ITEM_COUNT> contents_{};
    std::array<lv_obj_t*, ITEM_COUNT> labels_{};
    std::array<lv_obj_t*, ITEM_COUNT> values_{};
};

}  // namespace core::ui
