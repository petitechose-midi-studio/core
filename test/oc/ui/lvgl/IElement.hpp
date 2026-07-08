#pragma once

// Minimal native-test stand-in for OpenControl's LVGL UI interfaces.

#include <lvgl.h>

namespace oc::ui::lvgl {

class IElement {
public:
    virtual ~IElement() = default;
    virtual lv_obj_t* getElement() const = 0;
};

}  // namespace oc::ui::lvgl
