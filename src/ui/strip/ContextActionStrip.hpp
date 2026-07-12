#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>
#include <oc/ui/lvgl/PausableTimer.hpp>

#include "ui/font/StandaloneIcons.hpp"

namespace core::ui {

/**
 * Shared three-slot action strip for contextual controls.
 *
 * Props describe slot visibility, tone, labels/icons, and hold progress.
 * Rendering caches slot state and owns the hold timer used for visual countdown.
 */
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
    WARNING = 4,
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
    std::array<char, 16> labelText{};
    bool holdActive = false;
    uint32_t holdStartedAtMs = 0;
    uint32_t holdDurationMs = 0;
};

struct ContextActionStripProps {
    bool visible = false;
    std::array<ContextActionStripSlotProps, 3> slots{};
};

inline ContextActionStripSlotProps makeStandaloneIconStripSlot(
    const char* icon,
    ContextActionStripVisualState visual,
    ContextActionStripTone tone = ContextActionStripTone::NEUTRAL,
    standalone::icons::Size iconSize = standalone::icons::Size::M
) {
    return {
        .visualState = visual,
        .tone = tone,
        .showIcon = true,
        .icon = icon,
        .iconUsesStandaloneFont = true,
        .iconSize = iconSize,
        .showLabel = false,
        .label = nullptr,
    };
}

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
        bool hold_geometry_initialized = false;
        lv_coord_t indicator_long = -1;
        bool indicator_fill_mode = false;
        std::array<char, 16> hold_text{};
        uint16_t hold_tenths = std::numeric_limits<uint16_t>::max();
        lv_opa_t indicator_opa = LV_OPA_TRANSP;
        const lv_font_t* label_font = nullptr;
        uint32_t label_color = 0;
        lv_opa_t label_opa = LV_OPA_TRANSP;
    };

    void createUI(lv_obj_t* parent);
    void renderSlot(size_t index, const ContextActionStripSlotProps& props);
    void refreshHoldIndicators();
    void updateHoldTimer();
    static void onHoldTimer(lv_timer_t* timer);

    ContextActionStripOrientation orientation_;
    ContextActionStripVerticalLayout vertical_layout_;
    lv_obj_t* container_ = nullptr;
    std::optional<oc::ui::lvgl::PausableTimer> hold_timer_;
    std::array<SlotWidgets, 3> slots_{};
    bool has_rendered_ = false;
    ContextActionStripProps rendered_props_{};
};

}  // namespace core::ui
