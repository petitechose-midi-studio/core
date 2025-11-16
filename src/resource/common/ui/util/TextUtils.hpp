#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace TextUtils {

String formatTextForTwoLines(const String& text, lv_coord_t max_width, const lv_font_t* font);

String truncateWithEllipsis(const String& text, lv_coord_t max_width, const lv_font_t* font);

String sanitizeText(const String& text);

}  // namespace TextUtils
