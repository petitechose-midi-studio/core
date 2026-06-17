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

struct SequencerStepEditOverlayProps {
    static constexpr size_t PROPERTY_COUNT =
        core::state::sequencer::step_edit_rows::PROPERTIES.size();
    static constexpr size_t TRIGGER_COUNT = 2;
    static constexpr size_t MUSICAL_PROPERTY_COUNT = PROPERTY_COUNT - 1;
    static constexpr size_t ACTION_COUNT = 2;

    bool visible = false;
    const char* stepBadge = "";
    const char* title = "";
    const char* meta = "";
    const char* focusLabel = "";
    bool enabled = false;
    bool microSequence = false;
    bool cycleStates = false;
    bool probabilityActive = false;
    uint32_t dataRevision = 0;
    int selectedIndex = 0;
    SequencerStepEditPropertyChip state{};
    std::array<SequencerStepEditPropertyChip, PROPERTY_COUNT> properties{};
    std::array<SequencerStepEditActionChip, ACTION_COUNT> actions{};
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
    lv_obj_t* trigger_row_ = nullptr;
    lv_obj_t* property_row_ = nullptr;
    lv_obj_t* spacer_ = nullptr;
    lv_obj_t* action_row_ = nullptr;
    std::array<ChipWidgets, TRIGGER_COUNT> trigger_widgets_{};
    std::array<ChipWidgets, MUSICAL_PROPERTY_COUNT> property_widgets_{};
    std::array<ActionWidgets, ACTION_COUNT> action_widgets_{};

    struct LabelRenderCache {
        std::array<char, 24> text{};
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

    bool visible_cache_ = false;
    bool has_rendered_props_cache_ = false;
    uint32_t data_revision_cache_ = 0;
    int selected_index_cache_ = -1;
    LabelRenderCache step_badge_cache_{};
    LabelRenderCache title_cache_{};
    LabelRenderCache meta_cache_{};
    LabelRenderCache focus_label_cache_{};
    std::array<ChipRenderCache, TRIGGER_COUNT> trigger_cache_{};
    std::array<ChipRenderCache, MUSICAL_PROPERTY_COUNT> property_cache_{};
    std::array<ChipRenderCache, ACTION_COUNT> action_cache_{};
};

}  // namespace core::ui
