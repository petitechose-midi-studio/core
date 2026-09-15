#pragma once

#include <array>
#include <lvgl.h>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

struct ContextHeaderProps {
    const char* title = nullptr;
    const char* status = nullptr;
    const char* icon = nullptr;
    const char* statusIcon = nullptr;
    uint32_t titleColor = standalone::theme::color::TEXT_PRIMARY;
    uint32_t statusColor = standalone::theme::color::TEXT_SECONDARY;
    uint32_t iconColor = standalone::theme::color::TEXT_PRIMARY;
    lv_opa_t statusOpacity = LV_OPA_COVER;
};

/** Also usable by an existing retained surface, without adding a child widget. */
void drawContextHeader(lv_layer_t* layer, const lv_area_t& bounds,
                       const ContextHeaderProps& props);

/** Fixed, owned text; one drawing object and no subscription or domain state. */
class ContextHeader final {
public:
    explicit ContextHeader(lv_obj_t* parent);
    ~ContextHeader();
    ContextHeader(const ContextHeader&) = delete;
    ContextHeader& operator=(const ContextHeader&) = delete;

    void render(const ContextHeaderProps& props);
    [[nodiscard]] lv_obj_t* getElement() const { return root_; }

private:
    static void onDraw(lv_event_t* event);
    lv_obj_t* root_ = nullptr;
    std::array<char, 24> title_{};
    std::array<char, 40> status_{};
    std::array<char, 8> icon_{};
    std::array<char, 8> status_icon_{};
    uint32_t title_color_ = 0U;
    uint32_t status_color_ = 0U;
    uint32_t icon_color_ = 0U;
    lv_opa_t status_opacity_ = LV_OPA_COVER;
    bool rendered_ = false;
};

}  // namespace core::ui
