#include "TextUtils.hpp"

#include <Arduino.h>
#include <lvgl.h>
#include <misc/lv_text_private.h>

namespace TextUtils {

String formatTextForTwoLines(const String& text, lv_coord_t max_width, const lv_font_t* font) {
    if (!font) return text;

    lv_text_attributes_t attrs;
    lv_text_attributes_init(&attrs);
    lv_coord_t text_width = lv_text_get_width(text.c_str(), text.length(), font, &attrs);

    if (text_width <= max_width) {
        return text;
    }

    String words[20];
    int word_count = 0;
    unsigned int start = 0;

    for (unsigned int i = 0; i < text.length() && word_count < 20; i++) {
        if (text[(int)i] == ' ') {
            if (i > start) {
                words[word_count++] = text.substring(start, i);
            }
            start = i + 1;
        }
    }

    // Capture the last word (not followed by a space)
    if (start < text.length() && word_count < 20) {
        words[word_count++] = text.substring(start);
    }

    if (word_count == 0) return text;

    lv_coord_t first_word_width = lv_text_get_width(words[0].c_str(), words[0].length(), font, &attrs);
    if (first_word_width > max_width) {
        String line1 = truncateWithEllipsis(words[0], max_width, font);

        if (word_count > 1) {
            String line2 = words[1];

            for (int i = 2; i < word_count; i++) {
                String test_line2 = line2 + " " + words[i];
                lv_coord_t test_width =
                    lv_text_get_width(test_line2.c_str(), test_line2.length(), font, &attrs);

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

    String line1 = "";
    String line2 = "";

    for (int i = 0; i < word_count; i++) {
        String test_line1 = (line1.length() > 0) ? line1 + " " + words[i] : words[i];
        lv_coord_t test1_width =
            lv_text_get_width(test_line1.c_str(), test_line1.length(), font, &attrs);

        if (test1_width <= max_width) {
            line1 = test_line1;
        } else {
            for (int j = i; j < word_count; j++) {
                String test_line2 = (line2.length() > 0) ? line2 + " " + words[j] : words[j];
                lv_coord_t test2_width =
                    lv_text_get_width(test_line2.c_str(), test_line2.length(), font, &attrs);

                if (test2_width <= max_width) {
                    line2 = test_line2;
                } else {
                    if (line2.length() == 0) {
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

    if (line2.length() > 0) {
        return line1 + "\n" + line2;
    } else {
        return line1;
    }
}

String truncateWithEllipsis(const String& text, lv_coord_t max_width, const lv_font_t* font) {
    if (!font) return text;

    lv_text_attributes_t attrs;
    lv_text_attributes_init(&attrs);
    lv_coord_t full_width = lv_text_get_width(text.c_str(), text.length(), font, &attrs);
    if (full_width <= max_width) {
        return text;
    }

    lv_coord_t ellipsis_width = lv_text_get_width("...", 3, font, &attrs);
    if (max_width <= ellipsis_width) {
        return "...";
    }

    int left = 1;
    int right = text.length() - 1;
    int best_length = 0;

    while (left <= right) {
        int mid = (left + right) / 2;
        String candidate = text.substring(0, mid) + "...";
        lv_coord_t width = lv_text_get_width(candidate.c_str(), candidate.length(), font, &attrs);

        if (width <= max_width) {
            best_length = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    if (best_length > 0) {
        return text.substring(0, best_length) + "...";
    }

    return "...";
}

String sanitizeText(const String& text) {
    String result = "";
    for (unsigned int i = 0; i < text.length(); i++) {
        char c = text[i];

        if (c >= 32 && c <= 126) {
            result += c;
        } else if (c == ' ') {
            result += c;
        }
    }
    return result;
}

}  // namespace TextUtils
