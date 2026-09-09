#pragma once

// Native CMake tests do not link LVGL. This stub lets pure projection tests
// include UI prop headers without pulling the firmware/UI runtime.

#include <cstddef>
#include <cstdint>

using lv_coord_t = int16_t;
using lv_opa_t = uint8_t;

struct lv_obj_t {};
struct lv_font_t {};
struct lv_layer_t {};
struct lv_area_t {
    lv_coord_t x1 = 0;
    lv_coord_t y1 = 0;
    lv_coord_t x2 = 0;
    lv_coord_t y2 = 0;
};
struct lv_timer_t {};
struct lv_event_t {};
struct lv_style_value_t { const void* ptr = nullptr; };
enum lv_style_res_t { LV_STYLE_RES_NOT_FOUND, LV_STYLE_RES_FOUND };
inline constexpr int LV_STYLE_TEXT_FONT = 0;

// Declare inline-header dependencies only; actual rendering uses real LVGL tests.
lv_style_res_t lv_obj_get_local_style_prop(lv_obj_t*, int, lv_style_value_t*, int);
const char* lv_label_get_text(const lv_obj_t*);

inline constexpr lv_opa_t LV_OPA_TRANSP = 0;
inline constexpr lv_opa_t LV_OPA_60 = 153;
inline constexpr lv_opa_t LV_OPA_80 = 204;
inline constexpr lv_opa_t LV_OPA_COVER = 255;

inline void lv_obj_set_style_text_font(lv_obj_t*, lv_font_t*, int) {}
inline void lv_label_set_text(lv_obj_t*, const char*) {}
