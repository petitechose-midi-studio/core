#pragma once

#include <array>
#include <lvgl.h>

#include "ui/interaction/InteractiveSurfaceVisual.hpp"

namespace core::ui {

struct ParameterCardVisual {
    uint32_t accent = standalone::theme::color::TEXT_SECONDARY;
    interaction::InteractiveSurfaceState state = interaction::InteractiveSurfaceState::IDLE;
    lv_opa_t iconOpacity = LV_OPA_COVER;
    lv_opa_t labelOpacity = LV_OPA_80;
    lv_opa_t valueOpacity = LV_OPA_COVER;
    lv_opa_t activityOpacity = LV_OPA_TRANSP;
    bool emphasizeValue = true;

    bool operator==(const ParameterCardVisual& other) const {
        return accent == other.accent && state == other.state &&
            iconOpacity == other.iconOpacity && labelOpacity == other.labelOpacity &&
            valueOpacity == other.valueOpacity && activityOpacity == other.activityOpacity &&
            emphasizeValue == other.emphasizeValue;
    }
};

struct ParameterCardProps {
    const char* icon = nullptr;
    const char* label = nullptr;
    const char* value = nullptr;
    ParameterCardVisual visual{};
    uint8_t encoderNumber = 0U; // 1..8 replaces the icon with E1..E8; zero keeps it.
};

/** One retained drawing object. The caller owns layout and parameter semantics. */
class ParameterCard final {
public:
    explicit ParameterCard(lv_obj_t* parent);
    ~ParameterCard();
    ParameterCard(const ParameterCard&) = delete;
    ParameterCard& operator=(const ParameterCard&) = delete;

    void render(const ParameterCardProps& props);
    [[nodiscard]] lv_obj_t* getElement() const { return root_; }

private:
    static void onDraw(lv_event_t* event);
    void draw(lv_layer_t* layer) const;

    lv_obj_t* root_ = nullptr;
    std::array<char, 8> icon_{};
    std::array<char, 32> label_{};
    std::array<char, 32> value_{};
    ParameterCardVisual visual_{};
    uint8_t encoder_number_ = 0U;
    bool rendered_ = false;
};

}  // namespace core::ui
