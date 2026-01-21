#include "HwSimulator.hpp"
#include <oc/hal/sdl/InputMapper.hpp>
#include <cmath>

namespace desktop {

using namespace Config;

HwSimulator::HwSimulator(lv_obj_t* parent)
    : parent_(parent ? parent : lv_screen_active()) {}

HwSimulator::~HwSimulator() {
    if (panel_) {
        lv_obj_delete(panel_);
    }
}

void HwSimulator::init(const HwLayout& layout) {
    layout_ = layout;

    createPanel();
    createScreenArea();
    createButtons();
    createEncoders();
}

void HwSimulator::updateLayout(const HwLayout& layout) {
    layout_ = layout;
    updatePositions();
}

void HwSimulator::createPanel() {
    panel_ = lv_obj_create(parent_);

    // Size and position (centered)
    lv_obj_set_size(panel_, layout_.panelSize, layout_.panelSize);
    lv_obj_center(panel_);

    // Transparent background
    lv_obj_set_style_bg_opa(panel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_, 0, 0);
    lv_obj_set_style_pad_all(panel_, 0, 0);

    // No scrolling
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
}

void HwSimulator::createScreenArea() {
    screenArea_ = lv_obj_create(panel_);

    // Position and size from layout
    lv_obj_set_pos(screenArea_, layout_.screenX, layout_.screenY);
    lv_obj_set_size(screenArea_, layout_.screenW, layout_.screenH);

    // Style: dark background, no border
    lv_obj_set_style_bg_color(screenArea_, toLvColor(HwColor::SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(screenArea_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(screenArea_, 4, 0);
    lv_obj_set_style_border_width(screenArea_, 0, 0);
    lv_obj_set_style_pad_all(screenArea_, 0, 0);

    // No scrolling
    lv_obj_remove_flag(screenArea_, LV_OBJ_FLAG_SCROLLABLE);
}

void HwSimulator::createButtons() {
    // Left buttons (vertical)
    createButton(ButtonID::LEFT_TOP, layout_.leftBtnX, layout_.leftBtnYTop, layout_.btnRadius, HwColor::LEFT_TOP);
    createButton(ButtonID::LEFT_CENTER, layout_.leftBtnX, layout_.leftBtnYCenter, layout_.btnRadius, HwColor::LEFT_CENTER);
    createButton(ButtonID::LEFT_BOTTOM, layout_.leftBtnX, layout_.leftBtnYBottom, layout_.btnRadius, HwColor::LEFT_BOTTOM);

    // Bottom buttons (horizontal)
    createButton(ButtonID::BOTTOM_LEFT, layout_.bottomBtnXLeft, layout_.bottomBtnY, layout_.btnRadius, HwColor::BOTTOM_LEFT);
    createButton(ButtonID::BOTTOM_CENTER, layout_.bottomBtnXCenter, layout_.bottomBtnY, layout_.btnRadius, HwColor::BOTTOM_CENTER);
    createButton(ButtonID::BOTTOM_RIGHT, layout_.bottomBtnXRight, layout_.bottomBtnY, layout_.btnRadius, HwColor::BOTTOM_RIGHT);
}

void HwSimulator::createButton(ButtonID id, int x, int y, int radius, uint32_t color) {
    (void)color;  // Unused - transparent buttons
    lv_obj_t* btn = lv_obj_create(panel_);

    // Circular button
    int size = radius * 2;
    lv_obj_set_size(btn, size, size);
    lv_obj_set_pos(btn, x - radius, y - radius);

    // Gray style (visible but neutral)
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x404040), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    // Pressed state
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x606060), LV_STATE_PRESSED);

    // Make clickable
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    // Store user data for callback (pack ButtonID as pointer)
    lv_obj_set_user_data(btn, this);
    lv_obj_add_event_cb(btn, buttonEventCb, LV_EVENT_ALL,
                        reinterpret_cast<void*>(static_cast<intptr_t>(static_cast<oc::type::ButtonID>(id))));

    buttons_.push_back({id, btn, color});
}

void HwSimulator::createEncoders() {
    // NAV encoder with button
    createEncoder(EncoderID::NAV, ButtonID::NAV, layout_.rightX, layout_.navY, layout_.macroRadius, HwColor::NAV_GRAY);

    // OPT encoder (no button)
    createEncoder(EncoderID::OPT, ButtonID{0}, layout_.rightX, layout_.optY, layout_.macroRadius, HwColor::OPT_GRAY);

    // Macro encoders (4x2 grid)
    for (int i = 0; i < 8; i++) {
        int col = i % 4;
        int row = i / 4;
        int cx = layout_.macroStartX + col * layout_.macroSpacingX;
        int cy = layout_.macroStartY + row * layout_.macroSpacingY;

        createEncoder(MACRO_ENCODERS[i], MACRO_BUTTONS[i], cx, cy, layout_.macroRadius, HwColor::ENCODER_GRAY);
    }
}

void HwSimulator::createEncoder(EncoderID encId, ButtonID btnId, int x, int y, int radius, uint32_t color) {
    // Container
    lv_obj_t* container = lv_obj_create(panel_);
    int size = radius * 2;
    lv_obj_set_size(container, size, size);
    lv_obj_set_pos(container, x - radius, y - radius);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    // Arc (value indicator)
    lv_obj_t* arc = lv_arc_create(container);
    lv_obj_set_size(arc, size, size);
    lv_obj_center(arc);

    // Arc style
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_value(arc, 50);
    lv_arc_set_range(arc, 0, 100);

    // Background arc (track)
    lv_obj_set_style_arc_color(arc, lv_color_darken(toLvColor(color), LV_OPA_40), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);

    // Indicator arc
    lv_obj_set_style_arc_color(arc, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);

    // Hide knob
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    // Drag zone (invisible, covers arc area for drag interaction)
    lv_obj_t* dragZone = lv_obj_create(container);
    lv_obj_set_size(dragZone, size, size);
    lv_obj_center(dragZone);
    lv_obj_set_style_bg_opa(dragZone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dragZone, 0, 0);
    lv_obj_add_flag(dragZone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(dragZone, LV_OBJ_FLAG_SCROLLABLE);

    // Store encoder index for callback
    size_t encIndex = encoders_.size();
    lv_obj_set_user_data(dragZone, this);
    lv_obj_add_event_cb(dragZone, encoderEventCb, LV_EVENT_ALL,
                        reinterpret_cast<void*>(encIndex));

    // Center circle (encoder body - visual only)
    lv_obj_t* centerCircle = lv_obj_create(container);
    int centerSize = radius * 2 - 16;
    lv_obj_set_size(centerCircle, centerSize, centerSize);
    lv_obj_center(centerCircle);
    lv_obj_set_style_radius(centerCircle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(centerCircle, toLvColor(color), 0);
    lv_obj_set_style_bg_opa(centerCircle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(centerCircle, 1, 0);
    lv_obj_set_style_border_color(centerCircle, lv_color_darken(toLvColor(color), LV_OPA_20), 0);
    lv_obj_remove_flag(centerCircle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(centerCircle, LV_OBJ_FLAG_CLICKABLE);  // Visual only

    // Center button (if encoder has button)
    lv_obj_t* centerBtn = nullptr;
    bool hasButton = static_cast<oc::type::ButtonID>(btnId) != 0;
    if (hasButton) {
        centerBtn = lv_obj_create(container);
        int btnSize = radius * 2 / 3;
        lv_obj_set_size(centerBtn, btnSize, btnSize);
        lv_obj_center(centerBtn);
        lv_obj_set_style_radius(centerBtn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(centerBtn, lv_color_darken(toLvColor(color), LV_OPA_30), 0);
        lv_obj_set_style_bg_opa(centerBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(centerBtn, 1, 0);
        lv_obj_set_style_border_color(centerBtn, lv_color_darken(toLvColor(color), LV_OPA_50), 0);

        // Pressed state
        lv_obj_set_style_bg_color(centerBtn, lv_color_white(), LV_STATE_PRESSED);

        lv_obj_add_flag(centerBtn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(centerBtn, LV_OBJ_FLAG_SCROLLABLE);

        // Event callback
        lv_obj_set_user_data(centerBtn, this);
        lv_obj_add_event_cb(centerBtn, buttonEventCb, LV_EVENT_ALL,
                            reinterpret_cast<void*>(static_cast<intptr_t>(static_cast<oc::type::ButtonID>(btnId))));
    }

    // Store encoder data
    encoders_.push_back({encId, btnId, container, arc, dragZone, centerBtn, color, 0.5f, 0, false});
}

void HwSimulator::updatePositions() {
    if (!panel_) return;

    // Update panel
    lv_obj_set_size(panel_, layout_.panelSize, layout_.panelSize);
    lv_obj_set_style_radius(panel_, layout_.panelRadius, 0);
    lv_obj_center(panel_);

    // Update screen area
    if (screenArea_) {
        lv_obj_set_pos(screenArea_, layout_.screenX, layout_.screenY);
        lv_obj_set_size(screenArea_, layout_.screenW, layout_.screenH);
    }

    // Update buttons
    for (auto& btn : buttons_) {
        int x = 0, y = 0;
        int radius = layout_.btnRadius;

        // Map button ID to position
        switch (btn.id) {
            case ButtonID::LEFT_TOP:     x = layout_.leftBtnX; y = layout_.leftBtnYTop; break;
            case ButtonID::LEFT_CENTER:  x = layout_.leftBtnX; y = layout_.leftBtnYCenter; break;
            case ButtonID::LEFT_BOTTOM:  x = layout_.leftBtnX; y = layout_.leftBtnYBottom; break;
            case ButtonID::BOTTOM_LEFT:  x = layout_.bottomBtnXLeft; y = layout_.bottomBtnY; break;
            case ButtonID::BOTTOM_CENTER: x = layout_.bottomBtnXCenter; y = layout_.bottomBtnY; break;
            case ButtonID::BOTTOM_RIGHT: x = layout_.bottomBtnXRight; y = layout_.bottomBtnY; break;
            default: continue;
        }

        int size = radius * 2;
        lv_obj_set_size(btn.obj, size, size);
        lv_obj_set_pos(btn.obj, x - radius, y - radius);
    }

    // Update encoders
    for (auto& enc : encoders_) {
        int x = 0, y = 0;
        int radius = layout_.macroRadius;

        // Map encoder ID to position
        if (enc.encId == EncoderID::NAV) {
            x = layout_.rightX;
            y = layout_.navY;
        } else if (enc.encId == EncoderID::OPT) {
            x = layout_.rightX;
            y = layout_.optY;
        } else {
            // Macro encoders - find index
            for (int i = 0; i < 8; i++) {
                if (enc.encId == MACRO_ENCODERS[i]) {
                    int col = i % 4;
                    int row = i / 4;
                    x = layout_.macroStartX + col * layout_.macroSpacingX;
                    y = layout_.macroStartY + row * layout_.macroSpacingY;
                    break;
                }
            }
            if (x == 0 && y == 0) continue;
        }

        int size = radius * 2;
        lv_obj_set_size(enc.container, size, size);
        lv_obj_set_pos(enc.container, x - radius, y - radius);

        // Update arc size
        lv_obj_set_size(enc.arc, size, size);

        // Update drag zone
        if (enc.dragZone) {
            lv_obj_set_size(enc.dragZone, size, size);
        }

        // Update center circle (child index 2: after arc and dragZone)
        lv_obj_t* centerCircle = lv_obj_get_child(enc.container, 2);
        if (centerCircle) {
            int centerSize = radius * 2 - 16;
            lv_obj_set_size(centerCircle, centerSize, centerSize);
        }

        // Update center button
        if (enc.centerBtn) {
            int btnSize = radius * 2 / 3;
            lv_obj_set_size(enc.centerBtn, btnSize, btnSize);
        }
    }
}

void HwSimulator::setButtonPressed(oc::type::ButtonID id, bool pressed) {
    // Check standalone buttons
    for (auto& btn : buttons_) {
        if (static_cast<oc::type::ButtonID>(btn.id) == id) {
            if (pressed) {
                lv_obj_add_state(btn.obj, LV_STATE_PRESSED);
            } else {
                lv_obj_remove_state(btn.obj, LV_STATE_PRESSED);
            }
            return;
        }
    }

    // Check encoder buttons
    for (auto& enc : encoders_) {
        if (static_cast<oc::type::ButtonID>(enc.btnId) == id && enc.centerBtn) {
            if (pressed) {
                lv_obj_add_state(enc.centerBtn, LV_STATE_PRESSED);
            } else {
                lv_obj_remove_state(enc.centerBtn, LV_STATE_PRESSED);
            }
            return;
        }
    }
}

void HwSimulator::setEncoderValue(oc::type::EncoderID id, float value) {
    for (auto& enc : encoders_) {
        if (static_cast<oc::type::EncoderID>(enc.encId) == id) {
            enc.value = value;
            int arcValue = static_cast<int>(value * 100.0f);
            lv_arc_set_value(enc.arc, arcValue);
            return;
        }
    }
}

bool HwSimulator::handleMouseWheel(int mouseX, int mouseY, int delta) {
    if (!inputMapper_ || delta == 0) return false;

    // Check each encoder's container
    for (auto& enc : encoders_) {
        if (!enc.container) continue;

        // Get container position and size
        lv_area_t area;
        lv_obj_get_coords(enc.container, &area);

        // Check if mouse is inside
        if (mouseX >= area.x1 && mouseX <= area.x2 &&
            mouseY >= area.y1 && mouseY <= area.y2) {
            // Send delta to this encoder
            float encoderDelta = static_cast<float>(delta) * 0.02f;
            inputMapper_->post(static_cast<oc::type::EncoderID>(enc.encId), encoderDelta);
            return true;
        }
    }
    return false;
}

void HwSimulator::buttonEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    HwSimulator* self = static_cast<HwSimulator*>(lv_obj_get_user_data(obj));
    oc::type::ButtonID id = static_cast<oc::type::ButtonID>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));

    if (!self || !self->inputMapper_) return;

    if (code == LV_EVENT_PRESSED) {
        self->inputMapper_->post(id, true);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        self->inputMapper_->post(id, false);
    }
}

void HwSimulator::encoderEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    HwSimulator* self = static_cast<HwSimulator*>(lv_obj_get_user_data(obj));
    size_t encIndex = reinterpret_cast<size_t>(lv_event_get_user_data(e));

    if (!self || encIndex >= self->encoders_.size()) return;
    auto& enc = self->encoders_[encIndex];

    if (code == LV_EVENT_PRESSED) {
        // Start drag
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);
        enc.dragStartY = p.y;
        enc.dragging = true;
    }
    else if (code == LV_EVENT_PRESSING && enc.dragging) {
        // Continue drag - calculate delta
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);
        int deltaY = enc.dragStartY - p.y;  // Up = positive

        if (deltaY != 0 && self->inputMapper_) {
            // Sensitivity: ~100 pixels for full range
            float delta = static_cast<float>(deltaY) / 100.0f;
            self->inputMapper_->post(static_cast<oc::type::EncoderID>(enc.encId), delta);
            enc.dragStartY = p.y;  // Reset for continuous drag
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        enc.dragging = false;
    }
}

} // namespace desktop
