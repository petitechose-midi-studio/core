#pragma once

#include <string>

#include <lvgl.h>

namespace TextUtils {

std::string formatTextForTwoLines(const std::string& text, lv_coord_t max_width,
                                  const lv_font_t* font);

std::string truncateWithEllipsis(const std::string& text, lv_coord_t max_width,
                                 const lv_font_t* font);

std::string sanitizeText(const std::string& text);

}  // namespace TextUtils
