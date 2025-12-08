#include "FontLoader.hpp"

#include <vector>

#include <Arduino.h>

#include "data/interdisplay_bold_13.c.inc"
#include "data/interdisplay_bold_14.c.inc"
#include "data/interdisplay_bold_20.c.inc"
#include "data/interdisplay_light_14.c.inc"
#include "data/interdisplay_medium_13.c.inc"
#include "data/interdisplay_medium_14.c.inc"
#include "data/interdisplay_regular_14.c.inc"
#include "data/interdisplay_semibold_14.c.inc"
#include "data/jetbrainsmononl_medium_13.c.inc"
#include "log/Macros.hpp"

struct FontDescriptor {
    lv_font_t** font_ptr;
    const uint8_t* buffer;
    uint32_t len;
    const char* name;
    bool essential;
};

static std::vector<FontDescriptor> registered_fonts;
static size_t next_index = 0;
static bool core_registered = false;

FontRegistry fonts;

// Load font with retry on failure
static lv_font_t* loadFontSafe(const uint8_t* buffer, uint32_t len, const char* name = "unknown") {
    constexpr int MAX_RETRIES = 5;
    constexpr int BASE_DELAY_MS = 10;

    uint32_t start_time = millis();

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        lv_font_t* font = lv_binfont_create_from_buffer((void*)buffer, len);
        if (font != nullptr) {
            uint32_t elapsed = millis() - start_time;
            LOG("[Font] ");
            LOG(name);
            LOG(" ");
            LOG(len / 1024);
            LOG("KB ");
            LOG(elapsed);
            LOG("ms");
            if (attempt > 0) {
                LOG(" (");
                LOG(attempt + 1);
                LOG(" attempts)");
            }
            LOGLN("");
            return font;
        }
        // Log retry
        int delay_ms = BASE_DELAY_MS << attempt;
        LOG("[Font] ");
        LOG(name);
        LOG(" RETRY #");
        LOG(attempt + 1);
        LOG(" (");
        LOG(delay_ms);
        LOGLN("ms)");
        delay(delay_ms);
    }

    // Total failure
    LOG("[Font] ");
    LOG(name);
    LOGLN(" FAILED!");
    return nullptr;
}

void registerFont(lv_font_t** fontPtr, const uint8_t* buffer, uint32_t len) {
    registered_fonts.push_back({fontPtr, buffer, len, "plugin", false});
}

static void addFont(lv_font_t** ptr, const uint8_t* buf, uint32_t len, const char* name,
                    bool essential) {
    registered_fonts.push_back({ptr, buf, len, name, essential});
}

void fontsRegisterCore() {
    if (core_registered) return;
    core_registered = true;

    // Essential (splash)
    addFont(&fonts.splash_title, interdisplay_bold_20_bin, interdisplay_bold_20_bin_len, "Title",
            true);
    addFont(&fonts.splash_version, jetbrainsmononl_medium_13_bin, jetbrainsmononl_medium_13_bin_len,
            "Version", true);

    // Generic
    addFont(&fonts.inter_14_light, interdisplay_light_14_bin, interdisplay_light_14_bin_len,
            "Light", false);
    addFont(&fonts.inter_14_regular, interdisplay_regular_14_bin, interdisplay_regular_14_bin_len,
            "Regular", false);
    addFont(&fonts.inter_14_medium, interdisplay_medium_14_bin, interdisplay_medium_14_bin_len,
            "Medium", false);
    addFont(&fonts.inter_14_semibold, interdisplay_semibold_14_bin,
            interdisplay_semibold_14_bin_len, "SemiBold", false);
    addFont(&fonts.inter_14_bold, interdisplay_bold_14_bin, interdisplay_bold_14_bin_len, "Bold",
            false);

    // Semantic aliases
    addFont(&fonts.parameter_label, interdisplay_regular_14_bin, interdisplay_regular_14_bin_len,
            "Param", false);
    addFont(&fonts.parameter_value_label, interdisplay_medium_13_bin,
            interdisplay_medium_13_bin_len, "Value", false);
    addFont(&fonts.tempo_label, interdisplay_semibold_14_bin, interdisplay_semibold_14_bin_len,
            "Tempo", false);
    addFont(&fonts.list_item_label, interdisplay_semibold_14_bin, interdisplay_semibold_14_bin_len,
            "List", false);

    next_index = 0;
}

bool fontsLoadEssential() {
    if (!core_registered) fontsRegisterCore();

    LOGLN("[Font] Loading essential fonts...");
    uint8_t loaded = 0;
    uint8_t failed = 0;

    for (auto& f : registered_fonts) {
        if (f.essential && *f.font_ptr == nullptr) {
            *f.font_ptr = loadFontSafe(f.buffer, f.len, f.name);
            if (*f.font_ptr) {
                loaded++;
            } else {
                failed++;
            }
            delayMicroseconds(500);  // Brief stabilization (0.5ms)
        }
    }

    LOG("[Font] Essential: ");
    LOG(loaded);
    LOG(" loaded, ");
    LOG(failed);
    LOGLN(" failed");

    // Skip essential fonts
    next_index = 0;
    while (next_index < registered_fonts.size() && registered_fonts[next_index].essential) {
        next_index++;
    }

    return failed == 0;
}

uint8_t fontsGetPendingCount() {
    uint8_t count = 0;
    for (size_t i = next_index; i < registered_fonts.size(); i++) {
        if (*registered_fonts[i].font_ptr == nullptr) count++;
    }
    return count;
}

bool fontsLoadNext(const char** outFontName) {
    while (next_index < registered_fonts.size()) {
        auto& f = registered_fonts[next_index++];
        if (*f.font_ptr != nullptr) continue;

        *f.font_ptr = loadFontSafe(f.buffer, f.len, f.name);
        if (outFontName) *outFontName = f.name;
        return true;
    }
    return false;
}

void loadPluginFonts() {
    for (auto& f : registered_fonts) {
        if (*f.font_ptr == nullptr) { *f.font_ptr = loadFontSafe(f.buffer, f.len, f.name); }
    }
}

void freeFonts() {
    for (auto& f : registered_fonts) {
        if (*f.font_ptr) {
            lv_binfont_destroy(*f.font_ptr);
            *f.font_ptr = nullptr;
        }
    }
    registered_fonts.clear();
    next_index = 0;
    core_registered = false;
}
