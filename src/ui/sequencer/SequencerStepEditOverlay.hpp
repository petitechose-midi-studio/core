#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/sequencer/SequencerStepEditRows.hpp"
#include "ui/font/StandaloneIcons.hpp"

namespace core::ui {

struct SequencerStepEditPropertyChip {
    const char* key = "";
    const char* value = "";
    const char* icon = "";
    uint32_t color = 0;
};

struct SequencerStepEditActionChip {
    const char* key = "";
    const char* value = "";
    const char* icon = "";
    uint32_t color = 0;
};

struct SequencerChordPreviewVoiceMarker {
    bool active = false;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t size = 0;
    uint8_t width = 0;
    uint8_t height = 0;
    uint8_t opa = 0;
    uint32_t color = 0;
};

struct SequencerChordPreviewProps {
    static constexpr size_t MAX_VOICES = 8;

    bool visible = false;
    const char* name = "";
    const char* detail = "";
    uint32_t color = 0;
    bool mapVisible = false;
    bool timingVisible = false;
    uint8_t timingStart = 0;
    uint8_t timingEnd = 0;
    uint32_t timingColor = 0;
    std::array<SequencerChordPreviewVoiceMarker, MAX_VOICES> voices{};
};

enum class SequencerStepEditVisualSlot : uint8_t {
    STATE = 0,
    CHANCE,
    PITCH,
    VELOCITY,
    GATE,
    NUDGE,
    ACTION_0,
    ACTION_1,
    ACTION_2,
    CHORD_MODE,
    CHORD_VOICES,
    CHORD_COLOR,
    CHORD_SHAPE,
    CHORD_SPREAD,
    CHORD_STRUM,
    CHORD_VELOCITY,
    AUTO = 255,
};

struct SequencerStepEditOverlayProps {
    static constexpr size_t PROPERTY_COUNT =
        core::state::sequencer::step_edit_rows::PROPERTIES.size();
    static constexpr size_t TRIGGER_COUNT = 2;
    static constexpr size_t MUSICAL_PROPERTY_COUNT = PROPERTY_COUNT - 1;
    static constexpr size_t ACTION_COUNT = 3;
    static constexpr size_t CHORD_PERFORMANCE_COUNT = 3;

    bool visible = false;
    const char* stepBadge = "";
    const char* title = "";
    const char* meta = "";
    const char* focusLabel = "";
    bool titleCentered = false;
    bool focusLabelVisible = true;
    bool chordDetailLayout = false;
    bool enabled = false;
    bool microSequence = false;
    bool cycleStates = false;
    bool probabilityActive = false;
    bool actionsVisible = true;
    uint32_t dataRevision = 0;
    int selectedIndex = 0;
    SequencerStepEditVisualSlot selectedVisualSlot = SequencerStepEditVisualSlot::AUTO;
    uint32_t focusColor = 0;
    uint32_t titleColor = 0;
    SequencerStepEditPropertyChip state{};
    std::array<SequencerStepEditPropertyChip, PROPERTY_COUNT> properties{};
    std::array<SequencerStepEditPropertyChip, CHORD_PERFORMANCE_COUNT> chordPerformance{};
    std::array<SequencerStepEditActionChip, ACTION_COUNT> actions{};
    SequencerChordPreviewProps chordPreview{};
};

class SequencerStepEditOverlay : public oc::ui::lvgl::IWidget {
public:
    explicit SequencerStepEditOverlay(lv_obj_t* parent);
    ~SequencerStepEditOverlay() override;

    SequencerStepEditOverlay(const SequencerStepEditOverlay&) = delete;
    SequencerStepEditOverlay& operator=(const SequencerStepEditOverlay&) = delete;

    void render(const SequencerStepEditOverlayProps& props);

    lv_obj_t* getElement() const override { return overlay_; }

private:
    static constexpr size_t PROPERTY_COUNT = SequencerStepEditOverlayProps::PROPERTY_COUNT;
    static constexpr size_t TRIGGER_COUNT = SequencerStepEditOverlayProps::TRIGGER_COUNT;
    static constexpr size_t MUSICAL_PROPERTY_COUNT =
        SequencerStepEditOverlayProps::MUSICAL_PROPERTY_COUNT;
    static constexpr size_t ACTION_COUNT = SequencerStepEditOverlayProps::ACTION_COUNT;
    static constexpr size_t CHORD_PERFORMANCE_COUNT =
        SequencerStepEditOverlayProps::CHORD_PERFORMANCE_COUNT;

    struct ChipWidgets {
        lv_obj_t* box = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* value = nullptr;
    };

    struct ActionWidgets {
        lv_obj_t* box = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* value = nullptr;
    };

    struct ChipRenderCache;

    void createUI(lv_obj_t* parent);
    void renderChip(ChipWidgets& widgets,
                    ChipRenderCache& cache,
                    const SequencerStepEditPropertyChip& chip,
                    bool selected,
                    bool active,
                    standalone::icons::Size iconSize);
    void renderAction(size_t index,
                      const SequencerStepEditActionChip& chip,
                      bool selected);
    void resetRenderCaches();

    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* header_row_ = nullptr;
    lv_obj_t* summary_column_ = nullptr;
    lv_obj_t* step_badge_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* meta_ = nullptr;
    lv_obj_t* focus_label_ = nullptr;
    lv_obj_t* chord_preview_ = nullptr;
    lv_obj_t* chord_preview_name_ = nullptr;
    lv_obj_t* chord_preview_map_ = nullptr;
    lv_obj_t* chord_preview_timing_rail_ = nullptr;
    lv_obj_t* chord_preview_timing_span_ = nullptr;
    lv_obj_t* chord_preview_detail_ = nullptr;
    lv_obj_t* trigger_row_ = nullptr;
    lv_obj_t* property_row_ = nullptr;
    lv_obj_t* spacer_ = nullptr;
    lv_obj_t* action_row_ = nullptr;
    std::array<ChipWidgets, TRIGGER_COUNT> trigger_widgets_{};
    std::array<ChipWidgets, MUSICAL_PROPERTY_COUNT> property_widgets_{};
    std::array<ActionWidgets, ACTION_COUNT> action_widgets_{};

    struct LabelRenderCache {
        std::array<char, 32> text{};
        uint32_t color = UINT32_MAX;
        int16_t opa = -1;
    };

    struct ChipRenderCache {
        std::array<char, 16> value{};
        const char* icon = nullptr;
        uint32_t color = UINT32_MAX;
        uint32_t valueColor = UINT32_MAX;
        int16_t iconOpa = -1;
        int16_t valueOpa = -1;
        standalone::icons::Size iconSize = standalone::icons::Size::M;
        bool selected = false;
        bool active = false;
        bool valid = false;
    };

    struct ChordVoiceMarkerRenderCache {
        bool active = false;
        lv_coord_t x = -1;
        lv_coord_t y = -1;
        lv_coord_t width = -1;
        lv_coord_t height = -1;
        uint32_t color = UINT32_MAX;
        int16_t opa = -1;
    };

    bool visible_cache_ = false;
    bool has_rendered_props_cache_ = false;
    uint32_t data_revision_cache_ = 0;
    int selected_index_cache_ = -1;
    bool actions_visible_cache_ = true;
    bool title_centered_cache_ = false;
    bool focus_label_visible_cache_ = true;
    bool chord_detail_layout_cache_ = false;
    SequencerStepEditVisualSlot selected_visual_slot_cache_ =
        SequencerStepEditVisualSlot::AUTO;
    uint32_t focus_color_cache_ = 0;
    LabelRenderCache step_badge_cache_{};
    LabelRenderCache title_cache_{};
    LabelRenderCache meta_cache_{};
    LabelRenderCache focus_label_cache_{};
    LabelRenderCache chord_preview_name_cache_{};
    LabelRenderCache chord_preview_detail_cache_{};
    bool chord_preview_visible_cache_ = false;
    bool chord_preview_map_visible_cache_ = false;
    bool chord_preview_timing_visible_cache_ = false;
    bool chord_preview_detail_visible_cache_ = false;
    uint32_t chord_preview_color_cache_ = 0;
    lv_coord_t chord_preview_timing_x_cache_ = -1;
    lv_coord_t chord_preview_timing_width_cache_ = -1;
    uint32_t chord_preview_timing_color_cache_ = UINT32_MAX;
    std::array<lv_obj_t*, SequencerChordPreviewProps::MAX_VOICES> chord_preview_voice_dots_{};
    std::array<ChordVoiceMarkerRenderCache, SequencerChordPreviewProps::MAX_VOICES>
        chord_preview_voice_cache_{};
    std::array<ChipRenderCache, TRIGGER_COUNT> trigger_cache_{};
    std::array<ChipRenderCache, MUSICAL_PROPERTY_COUNT> property_cache_{};
    std::array<ChipRenderCache, ACTION_COUNT> action_cache_{};
};

}  // namespace core::ui
