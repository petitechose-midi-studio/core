#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "ui/font/StandaloneIcons.hpp"

namespace core::ui {

enum class ContextActionStripOrientation : uint8_t {
    HORIZONTAL = 0,
    VERTICAL = 1,
};

enum class ContextActionStripVerticalLayout : uint8_t {
    COMPACT = 0,
    SPREAD = 1,
};

enum class ContextActionStripTone : uint8_t {
    NEUTRAL = 0,
    CONSTRUCTIVE = 1,
    DESTRUCTIVE = 2,
    POSITIVE = 3,
};

enum class ContextActionStripVisualState : uint8_t {
    HIDDEN = 0,
    DISABLED = 1,
    DIM = 2,
    ACTIVE = 3,
    ARMED = 4,
};

struct ContextActionStripSlotProps {
    ContextActionStripVisualState visualState = ContextActionStripVisualState::HIDDEN;
    ContextActionStripTone tone = ContextActionStripTone::NEUTRAL;
    bool showIcon = false;
    const char* icon = nullptr;
    bool iconUsesStandaloneFont = true;
    standalone::icons::Size iconSize = standalone::icons::Size::M;
    bool showLabel = false;
    const char* label = nullptr;
};

struct ContextActionStripProps {
    bool visible = false;
    std::array<ContextActionStripSlotProps, 3> slots{};
};

class ContextActionStrip : public oc::ui::lvgl::IWidget {
public:
    ContextActionStrip(
        lv_obj_t* parent,
        ContextActionStripOrientation orientation,
        ContextActionStripVerticalLayout verticalLayout = ContextActionStripVerticalLayout::COMPACT
    );
    ~ContextActionStrip() override;

    ContextActionStrip(const ContextActionStrip&) = delete;
    ContextActionStrip& operator=(const ContextActionStrip&) = delete;

    void render(const ContextActionStripProps& props);

    lv_obj_t* getElement() const override { return container_; }

private:
    struct SlotWidgets {
        lv_obj_t* container = nullptr;
        lv_obj_t* indicator = nullptr;
        lv_obj_t* content = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* label = nullptr;
    };

    void createUI(lv_obj_t* parent);
    void renderSlot(size_t index, const ContextActionStripSlotProps& props);

    ContextActionStripOrientation orientation_;
    ContextActionStripVerticalLayout vertical_layout_;
    lv_obj_t* container_ = nullptr;
    std::array<SlotWidgets, 3> slots_{};
    bool has_rendered_ = false;
    ContextActionStripProps rendered_props_{};
};

}  // namespace core::ui
