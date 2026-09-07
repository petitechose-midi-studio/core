#pragma once

#include <lvgl.h>

#include "state/sequencer/SequencerPatternPreset.hpp"

namespace core::ui::sequencer {

struct SequencerPatternPresetPreviewProps {
    const core::state::sequencer::SequencerPatternPresetDescriptor*
        descriptor = nullptr;
    uint32_t revision = 0U;
    bool visible = false;
};

/**
 * Static musical thumbnail for a Pattern Preset detail surface.
 *
 * The widget retains only the bounded inspection projection. It never reads
 * live authored state, advances a playhead, or allocates while rendering.
 */
class SequencerPatternPresetPreview final {
public:
    void create(lv_obj_t* parent);
    void render(const SequencerPatternPresetPreviewProps& props);

    [[nodiscard]] lv_obj_t* element() const { return surface_; }

private:
    static void onDraw(lv_event_t* event);
    void draw(lv_layer_t* layer) const;

    lv_obj_t* surface_ = nullptr;
    core::state::sequencer::SequencerPatternPresetVisualSummary visual_{};
    std::array<char, 48> timing_{};
    std::array<char, 40> content_{};
    uint32_t revision_ = 0U;
    bool drum_ = false;
    bool visible_ = false;
};

static_assert(sizeof(SequencerPatternPresetPreview) <=
              sizeof(core::state::sequencer::SequencerPatternPresetDescriptor),
              "Retain only displayed preset data, not the full descriptor");

}  // namespace core::ui::sequencer
