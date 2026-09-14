#pragma once

#include <array>
#include <cstdio>
#include <cstdint>
#include <optional>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>
#include <oc/ui/lvgl/PausableTimer.hpp>

#include "ui/font/StandaloneIcons.hpp"

namespace core::ui {

/**
 * Retained action presentation shared by views and overlays.
 * Horizontal surfaces own the footer's side commands and the explanation row;
 * their middle slot is contextual information, never a replacement for Play.
 * Vertical surfaces keep the physical three-button arrangement. One draw object
 * and bounded text caches replace per-slot widget trees. The timer runs only
 * while a visible hold needs visual progress; it never commits a command.
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
    AVAILABLE = ACTIVE,
    PRESSED = 4,
    ARMED = 5,
    CANCELLED = 6,
    APPLIED = 7,
};

struct ContextActionStripSlotProps {
    ContextActionStripVisualState visualState = ContextActionStripVisualState::HIDDEN;
    ContextActionStripTone tone = ContextActionStripTone::NEUTRAL;
    bool showIcon = false;
    const char* icon = nullptr;
    bool iconUsesStandaloneFont = true;
    standalone::icons::Size iconSize = standalone::icons::Size::L;
    bool showLabel = false;
    const char* label = nullptr;
    std::array<char, 16> labelText{};
    bool holdOnly = false;
    bool holdActive = false;
    uint32_t holdStartedAtMs = 0;
    uint32_t holdDurationMs = 0;
};

struct ContextActionStripProps {
    bool visible = false;
    std::array<ContextActionStripSlotProps, 3> slots{};
    const char* hintLeft = nullptr;
    const char* hintRight = nullptr;
};

inline ContextActionStripSlotProps makeStandaloneIconStripSlot(
    const char* icon,
    ContextActionStripVisualState visual,
    ContextActionStripTone tone = ContextActionStripTone::NEUTRAL,
    standalone::icons::Size iconSize = standalone::icons::Size::L
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

inline ContextActionStripSlotProps makeStructureSelectionCountStripSlot(
    uint8_t selectedCount
) {
    ContextActionStripSlotProps slot{
        .visualState = ContextActionStripVisualState::ACTIVE,
        .tone = ContextActionStripTone::NEUTRAL,
        .showLabel = true,
    };
    std::snprintf(
        slot.labelText.data(),
        slot.labelText.size(),
        "%u selected",
        static_cast<unsigned>(selectedCount)
    );
    return slot;
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
    void alignToFooter();

    lv_obj_t* getElement() const override { return container_; }

private:
    void createUI(lv_obj_t* parent);
    void draw(lv_layer_t* layer) const;
    void drawSlot(lv_layer_t* layer, size_t index, const lv_area_t& bounds) const;
    void refreshHoldProgress();
    void updateHoldTimer();
    static void onHoldTimer(lv_timer_t* timer);
    static void onDraw(lv_event_t* event);

    ContextActionStripOrientation orientation_;
    ContextActionStripVerticalLayout vertical_layout_;
    lv_obj_t* container_ = nullptr;
    std::optional<oc::ui::lvgl::PausableTimer> hold_timer_;
    std::array<std::array<char, 24>, 3> label_text_{};
    std::array<std::array<char, 32>, 3> command_text_{};
    std::array<char, 48> hint_left_{};
    std::array<char, 48> hint_right_{};
    std::array<uint16_t, 3> hold_progress_{};
    std::array<char, 48> hold_text_{};
    bool has_rendered_ = false;
    ContextActionStripProps rendered_props_{};
};

}  // namespace core::ui
