#include <cassert>
#include <cstdio>
#include <ms/ui/font/CoreFonts.hpp>

int main() {
    lv_init();
    lv_mem_monitor_t before{}, loaded{}, after{};
    lv_mem_monitor(&before);
    assert(CORE_FONT_COUNT == 6);
    uint64_t fingerprint = 14695981039346656037ULL;
    const auto hash = [&](uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            fingerprint = (fingerprint ^ ((value >> shift) & 255U)) * 1099511628211ULL;
        }
    };
    for (size_t i = 0; i < CORE_FONT_COUNT; ++i) {
        const auto& entry = CORE_FONT_ENTRIES[i];
        *entry.target = lv_binfont_create_from_buffer(const_cast<uint8_t*>(entry.data), entry.size);
        assert(*entry.target);
        for (uint32_t index = 0x20; index <= 0x100; ++index) {
            const uint32_t codepoint = index == 0x100 ? 0x2026 : index;
            lv_font_glyph_dsc_t glyph{};
            const bool present = lv_font_get_glyph_dsc(*entry.target, &glyph, codepoint, 0);
            if (codepoint < 0x7f || codepoint == 0x2026) assert(present);
            hash(codepoint);
            hash(present);
            if (!present) continue;
            hash(glyph.adv_w);
            hash(glyph.box_w);
            hash(glyph.box_h);
            hash(glyph.ofs_x);
            hash(glyph.ofs_y);
            if (!glyph.box_w || !glyph.box_h) continue;
            auto* buffer = lv_draw_buf_create(glyph.box_w, glyph.box_h, LV_COLOR_FORMAT_A8, 0);
            assert(buffer);
            assert(lv_font_get_glyph_bitmap(&glyph, buffer) == buffer);
            for (uint32_t y = 0; y < glyph.box_h; ++y) {
                for (uint32_t x = 0; x < glyph.box_w; ++x) hash(buffer->data[y * buffer->header.stride + x]);
            }
            lv_draw_buf_destroy(buffer);
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
    std::printf("Glyph metrics and masks: %016llx\n", static_cast<unsigned long long>(fingerprint));
    assert(fingerprint == 0x2cf63098f391327eULL);
    std::printf("Core fonts: %zu loaded, %zu bytes, aliases valid, no leaked allocation\n",
                CORE_FONT_COUNT, before.free_size - loaded.free_size);
    lv_deinit();
}
