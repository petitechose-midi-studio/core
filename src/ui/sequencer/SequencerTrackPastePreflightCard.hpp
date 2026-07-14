#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include <lvgl.h>
#include <oc/ui/lvgl/PausableTimer.hpp>

#include "ui/sequencer/SequencerTrackPastePreflightViewModel.hpp"

namespace core::ui::sequencer {

/** Temporary 320x240 Track-paste decision card; owns no domain state. */
class SequencerTrackPastePreflightCard {
public:
    explicit SequencerTrackPastePreflightCard(lv_obj_t* parent);
    ~SequencerTrackPastePreflightCard();

    SequencerTrackPastePreflightCard(const SequencerTrackPastePreflightCard&) = delete;
    SequencerTrackPastePreflightCard& operator=(
        const SequencerTrackPastePreflightCard&
    ) = delete;

    [[nodiscard]] bool valid() const;
    [[nodiscard]] lv_obj_t* getElement() const { return panel_; }
    void render(const SequencerTrackPastePreflightViewModel& model);

private:
    static constexpr uint32_t APPLIED_CONFIRMATION_MS = 1100;

    static void copyText(char* destination, size_t capacity, const char* source);

    static void onAppliedTimeout(lv_timer_t* timer);
    void hide();
    void show();
    void applyTone(SequencerTrackPastePreflightTone tone);
    void renderText(const SequencerTrackPastePreflightViewModel& model);

    lv_obj_t* panel_ = nullptr;
    lv_obj_t* header_ = nullptr;
    lv_obj_t* mapping_ = nullptr;
    lv_obj_t* footprint_ = nullptr;
    lv_obj_t* route_ = nullptr;
    lv_obj_t* lane_bindings_ = nullptr;
    lv_obj_t* detail_ = nullptr;
    std::optional<oc::ui::lvgl::PausableTimer> applied_timer_;

    std::array<char, 40> header_text_{};
    std::array<char, 272> mapping_text_{};
    std::array<char, 48> footprint_text_{};
    std::array<char, 64> route_text_{};
    std::array<char, 64> lane_bindings_text_{};
    std::array<char, 64> detail_text_{};
    uint32_t shown_applied_generation_ = 0;
    uint32_t dismissed_applied_generation_ = 0;
};

}  // namespace core::ui::sequencer
