#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include "state/interaction/TextKeyboardLayout.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::ui::interaction {

struct TextKeyboardViewProps {
    bool visible = false;
    const char* title = "";
    const char* meta = "";
    const char* name = "";
    uint8_t selectedKey =
        core::state::interaction::TEXT_KEYBOARD_CELL_COUNT;
    bool shiftActive = false;
};

class TextKeyboardView {
public:
    explicit TextKeyboardView(lv_obj_t* parent);

    [[nodiscard]] bool valid() const { return initialized_; }
    [[nodiscard]] lv_obj_t* getElement() const { return container_; }
    void render(const TextKeyboardViewProps& props);
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
        core::state::interaction::TEXT_KEYBOARD_CELL_COUNT
    > keys_{};
    bool visible_ = false;
    uint8_t rendered_selected_ =
        core::state::interaction::TEXT_KEYBOARD_CELL_COUNT;
    bool rendered_shift_ = false;
    bool initialized_ = false;
};

static_assert(
    sizeof(TextKeyboardView) <= 1024U,
    "Text keyboard exceeds its retained PSRAM owner budget"
);
static_assert(
    sizeof(void*) != 4U || sizeof(TextKeyboardView) <= 480U,
    "Text keyboard exceeds its Teensy PSRAM owner budget"
);

}  // namespace core::ui::interaction
