#include <cassert>
#include <cstdio>
#include <ms/ui/font/CoreFonts.hpp>

int main() {
    lv_init();
    lv_mem_monitor_t before{}, loaded{}, after{};
    lv_mem_monitor(&before);
    assert(CORE_FONT_COUNT == 6);
    for (size_t i = 0; i < CORE_FONT_COUNT; ++i) {
        const auto& entry = CORE_FONT_ENTRIES[i];
        *entry.target = lv_binfont_create_from_buffer(const_cast<uint8_t*>(entry.data), entry.size);
        assert(*entry.target);
        for (uint32_t codepoint = 0x20; codepoint < 0x7f; ++codepoint) {
            lv_font_glyph_dsc_t glyph{};
            assert(lv_font_get_glyph_dsc(*entry.target, &glyph, codepoint, 0));
        }
    }
    linkCoreFontAliases();
    assert(fonts.parameter_label == fonts.inter_14_regular);
    assert(fonts.parameter_value_label == fonts.inter_14_medium);
    assert(fonts.tempo_label == fonts.inter_14_semibold);
    assert(fonts.list_item_label == fonts.inter_14_semibold);
    assert(fonts.context_title() && fonts.header_label() && fonts.primary_value());
    assert(fonts.compact_label() && fonts.compact_selected() && fonts.meta_label());
    lv_mem_monitor(&loaded);
    for (size_t i = 0; i < CORE_FONT_COUNT; ++i) {
        lv_binfont_destroy(*CORE_FONT_ENTRIES[i].target);
        *CORE_FONT_ENTRIES[i].target = nullptr;
    }
    linkCoreFontAliases();
    lv_mem_monitor(&after);
    assert(after.free_size == before.free_size);
    std::printf("Core fonts: %zu loaded, %zu bytes, aliases valid, no leaked allocation\n",
                CORE_FONT_COUNT, before.free_size - loaded.free_size);
    lv_deinit();
}
