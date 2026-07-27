#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include "state/project/ProjectNameKeyboard.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::ui::project {

struct ProjectNameKeyboardViewProps {
    bool visible = false;
    const char* title = "";
    const char* meta = "";
    const char* name = "";
    uint8_t selectedKey =
        core::state::project::PROJECT_NAME_KEYBOARD_CELL_COUNT;
    bool shiftActive = false;
};

class ProjectNameKeyboardView {
public:
    explicit ProjectNameKeyboardView(lv_obj_t* parent);

    [[nodiscard]] bool valid() const { return initialized_; }
    void render(const ProjectNameKeyboardViewProps& props);
    void setVisible(bool visible);

    [[nodiscard]] static ContextActionStripProps leftActionStripProps(
        bool visible,
        bool shiftActive
    );
    [[nodiscard]] static ContextActionStripProps bottomActionStripProps(
        bool visible,
        bool playing
    );

private:
    struct KeyWidgets {
        lv_obj_t* label = nullptr;
        lv_obj_t* shiftLabel = nullptr;
    };

    void createLayout(lv_obj_t* parent);
    void renderKey(uint8_t index, bool selected);
    void applyShiftVisibility(bool shiftActive);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* meta_ = nullptr;
    lv_obj_t* name_box_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    std::array<
        KeyWidgets,
        core::state::project::PROJECT_NAME_KEYBOARD_CELL_COUNT
    > keys_{};
    bool visible_ = false;
    uint8_t rendered_selected_ =
        core::state::project::PROJECT_NAME_KEYBOARD_CELL_COUNT;
    bool rendered_shift_ = false;
    bool initialized_ = false;
};

static_assert(
    sizeof(ProjectNameKeyboardView) <= 1024U,
    "Project name keyboard exceeds its retained PSRAM owner budget"
);
static_assert(
    sizeof(void*) != 4U || sizeof(ProjectNameKeyboardView) <= 480U,
    "Project name keyboard exceeds its Teensy PSRAM owner budget"
);

}  // namespace core::ui::project
