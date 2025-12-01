#include "TextUtils.hpp"

#include <lvgl.h>
#include <misc/lv_text_private.h>
#include <vector>

namespace TextUtils {

std::string formatTextForTwoLines(const std::string& text, lv_coord_t max_width, const lv_font_t* font) {
    if (!font) return text;

    lv_text_attributes_t attrs;
    lv_text_attributes_init(&attrs);
    lv_coord_t text_width = lv_text_get_width(text.c_str(), text.size(), font, &attrs);

    if (text_width <= max_width) {
        return text;
    }

    std::vector<std::string> words;
    size_t start = 0;

    for (size_t i = 0; i < text.size() && words.size() < 20; i++) {
        if (text[i] == ' ') {
            if (i > start) {
                words.push_back(text.substr(start, i - start));
            }
            start = i + 1;
        }
    }

    // Capture the last word (not followed by a space)
    if (start < text.size() && words.size() < 20) {
        words.push_back(text.substr(start));
    }

    if (words.empty()) return text;

    lv_coord_t first_word_width = lv_text_get_width(words[0].c_str(), words[0].size(), font, &attrs);
    if (first_word_width > max_width) {
        std::string line1 = truncateWithEllipsis(words[0], max_width, font);

        if (words.size() > 1) {
            std::string line2 = words[1];

            for (size_t i = 2; i < words.size(); i++) {
                std::string test_line2 = line2 + " " + words[i];
                lv_coord_t test_width =
                    lv_text_get_width(test_line2.c_str(), test_line2.size(), font, &attrs);

                if (test_width <= max_width) {
                    line2 = test_line2;
                } else {
                    line2 = truncateWithEllipsis(line2, max_width, font);
                    break;
                }
            }

            return line1 + "\n" + line2;
        } else {
            return line1;
        }
    }

    std::string line1;
    std::string line2;

    for (size_t i = 0; i < words.size(); i++) {
        std::string test_line1 = line1.empty() ? words[i] : line1 + " " + words[i];
        lv_coord_t test1_width =
            lv_text_get_width(test_line1.c_str(), test_line1.size(), font, &attrs);

        if (test1_width <= max_width) {
            line1 = test_line1;
        } else {
            for (size_t j = i; j < words.size(); j++) {
                std::string test_line2 = line2.empty() ? words[j] : line2 + " " + words[j];
                lv_coord_t test2_width =
                    lv_text_get_width(test_line2.c_str(), test_line2.size(), font, &attrs);

                if (test2_width <= max_width) {
                    line2 = test_line2;
                } else {
                    if (line2.empty()) {
                        line2 = truncateWithEllipsis(words[j], max_width, font);
                    } else {
                        line2 = truncateWithEllipsis(line2, max_width, font);
                    }
                    break;
                }
            }
            break;
        }
    }

    if (!line2.empty()) {
        return line1 + "\n" + line2;
    } else {
        return line1;
    }
}

std::string truncateWithEllipsis(const std::string& text, lv_coord_t max_width, const lv_font_t* font) {
    if (!font) return text;

    lv_text_attributes_t attrs;
    lv_text_attributes_init(&attrs);
    lv_coord_t full_width = lv_text_get_width(text.c_str(), text.size(), font, &attrs);
    if (full_width <= max_width) {
        return text;
    }

    lv_coord_t ellipsis_width = lv_text_get_width("...", 3, font, &attrs);
    if (max_width <= ellipsis_width) {
        return "...";
    }

    size_t left = 1;
    size_t right = text.size() - 1;
    size_t best_length = 0;

    while (left <= right) {
        size_t mid = (left + right) / 2;
        std::string candidate = text.substr(0, mid) + "...";
        lv_coord_t width = lv_text_get_width(candidate.c_str(), candidate.size(), font, &attrs);

        if (width <= max_width) {
            best_length = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    if (best_length > 0) {
        return text.substr(0, best_length) + "...";
    }

    return "...";
}

std::string sanitizeText(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    for (char c : text) {
        if (c >= 32 && c <= 126) {
            result += c;
        } else if (c == ' ') {
            result += c;
        }
    }
    return result;
}

}  // namespace TextUtils
