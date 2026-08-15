#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/sequencer/SequencerPatternEditorState.hpp"
#include "state/sequencer/SequencerPatternRandomizeOps.hpp"
#include "ui/sequencer/SequencerPatternTimelineModel.hpp"

namespace core::ui {

struct SequencerPatternEditorFieldChip {
    const char* icon = "";
    const char* value = "";
    uint32_t color = 0U;
    bool selected = false;
};

struct SequencerPatternEditorOverlayProps {
    static constexpr std::size_t FIELD_COUNT = static_cast<std::size_t>(
        core::state::sequencer::SequencerPatternEditorField::COUNT
    );

    bool visible = false;
    const char* title = "";
    const char* meta = "";
    const char* layer = "";
    const char* transientHint = "";
    uint32_t layerColor = 0U;
    uint8_t fieldCount = static_cast<uint8_t>(FIELD_COUNT);
    std::array<SequencerPatternEditorFieldChip, FIELD_COUNT> fields{};
    const core::ui::sequencer::SequencerPatternTimelineGeometry* geometry = nullptr;
    uint32_t geometryRevision = 0U;
    core::ui::sequencer::SequencerPatternTimelinePlayhead playhead{};
    core::state::sequencer::SequencerPatternEditorLayer focusedLayer =
        core::state::sequencer::SequencerPatternEditorLayer::NOTES;
    core::state::sequencer::SequencerPatternEditorNavigationMode navigationMode =
        core::state::sequencer::SequencerPatternEditorNavigationMode::FIELDS;
    oc::note::sequencer::StepBitMask128 randomizeChangedSteps{};
    core::state::sequencer::SequencerPatternRandomizeProperty randomizeProperty =
        core::state::sequencer::SequencerPatternRandomizeProperty::NOTE;
    bool randomizePreview = false;
};

/**
 * Retained Pattern Editor surface.
 *
 * Static geometry and the playhead are two custom-drawn LVGL surfaces.
 * Musical samples are never represented by child objects; a playhead update
 * invalidates only its old/new narrow bands and static redraw is clip-aware.
 */
class SequencerPatternEditorOverlay final : public oc::ui::lvgl::IWidget {
public:
    explicit SequencerPatternEditorOverlay(lv_obj_t* parent);
    ~SequencerPatternEditorOverlay() override;

    SequencerPatternEditorOverlay(const SequencerPatternEditorOverlay&) = delete;
    SequencerPatternEditorOverlay& operator=(const SequencerPatternEditorOverlay&) = delete;

    void render(const SequencerPatternEditorOverlayProps& props);
    void renderPlayhead(
        core::ui::sequencer::SequencerPatternTimelinePlayhead playhead
    );

    lv_obj_t* getElement() const override { return root_; }

private:
    struct CachedField {
        std::array<char, 8> icon{};
        std::array<char, 12> value{};
        uint32_t color = 0U;
        bool selected = false;
    };

    void createUi(lv_obj_t* parent);
    void drawTimeline(lv_layer_t* layer);
    void drawPlayhead(lv_layer_t* layer);
    void drawFields(lv_layer_t* layer);
    void invalidateTimeline();
    void invalidateFields();
    static void onTimelineDraw(lv_event_t* event);
    static void onPlayheadDraw(lv_event_t* event);
    static void onFieldsDraw(lv_event_t* event);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* meta_ = nullptr;
    lv_obj_t* layer_ = nullptr;
    lv_obj_t* timeline_ = nullptr;
    lv_obj_t* playhead_surface_ = nullptr;
    lv_obj_t* transient_hint_ = nullptr;
    lv_obj_t* fields_ = nullptr;

    const core::ui::sequencer::SequencerPatternTimelineGeometry* geometry_ = nullptr;
    core::ui::sequencer::SequencerPatternTimelinePlayhead playhead_{};
    core::state::sequencer::SequencerPatternEditorLayer focused_layer_ =
        core::state::sequencer::SequencerPatternEditorLayer::NOTES;
    core::state::sequencer::SequencerPatternEditorNavigationMode navigation_mode_ =
        core::state::sequencer::SequencerPatternEditorNavigationMode::FIELDS;
    oc::note::sequencer::StepBitMask128 randomize_changed_steps_{};
    core::state::sequencer::SequencerPatternRandomizeProperty randomize_property_ =
        core::state::sequencer::SequencerPatternRandomizeProperty::NOTE;
    uint32_t layer_color_ = 0U;
    bool randomize_preview_ = false;
    std::array<CachedField, SequencerPatternEditorOverlayProps::FIELD_COUNT>
        field_cache_{};
    std::array<lv_point_precise_t,
               core::ui::sequencer::SEQUENCER_PATTERN_TIMELINE_MAX_WIDTH>
        curve_points_{};
    std::array<char, 48> title_text_{};
    std::array<char, 40> meta_text_{};
    std::array<char, 24> layer_text_{};
    std::array<char, 64> hint_text_{};
    uint32_t geometry_revision_ = 0U;
    uint8_t field_count_ = static_cast<uint8_t>(
        SequencerPatternEditorOverlayProps::FIELD_COUNT
    );
    bool visible_ = false;
    bool rendered_ = false;
};

}  // namespace core::ui
