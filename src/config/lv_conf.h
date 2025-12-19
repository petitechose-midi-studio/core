/**
 * @file lv_conf.h
 * @brief LVGL v9.x configuration for Teensy 4.1 + ILI9341
 *
 * Key settings:
 * - 4MB EXTMEM pool (PSRAM) - 8MB available
 * - RGB565 color (16-bit)
 * - NO LV_DEF_REFR_PERIOD (set at runtime by Bridge)
 * - ARGB8888 enabled for transparency animations
 * - Image cache for splash screen
 */

#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

// Memory: 4MB EXTMEM (PSRAM) - 8MB available on Teensy 4.1
#define LVGL_MEMORY_POOL_SIZE_KB 4000
#define LVGL_MEMORY_POOL_SIZE (LVGL_MEMORY_POOL_SIZE_KB * 1024)

#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

#define LV_STDINT_INCLUDE <stdint.h>
#define LV_STDDEF_INCLUDE <stddef.h>
#define LV_STDBOOL_INCLUDE <stdbool.h>
#define LV_INTTYPES_INCLUDE <inttypes.h>
#define LV_LIMITS_INCLUDE <limits.h>
#define LV_STDARG_INCLUDE <stdarg.h>

#define LV_MEM_SIZE LVGL_MEMORY_POOL_SIZE
#define LV_MEM_POOL_EXPAND_SIZE 0
#define LV_MEM_ADR 0
#define LV_MEM_POOL_INCLUDE "config/LvglMemory.hpp"
#define LV_MEM_POOL_ALLOC getLvglMemoryPool

// NO LV_DEF_REFR_PERIOD - set at runtime by Bridge via Config::Timing::LVGL_HZ
#define LV_DPI_DEF 130

#define LV_USE_OS LV_OS_NONE

// Cortex-M7 cache line alignment for optimal DMA/memory performance
#define LV_DRAW_BUF_STRIDE_ALIGN 4
#define LV_DRAW_BUF_ALIGN 32
#define LV_DRAW_TRANSFORM_USE_MATRIX 1
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE (128 * 1024)
#define LV_DRAW_LAYER_MAX_MEMORY (512 * 1024)

#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW == 1
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB565A8 1
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888 1          // Required for transparency animations
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_A8 1                // Alpha channel for transparency
#define LV_DRAW_SW_SUPPORT_I1 0
#define LV_DRAW_SW_DRAW_UNIT_CNT 0
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_SW_SHADOW_CACHE_SIZE 32        // Cache shadows (32² = 1KB RAM)
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 1     // Keep for future use
#endif

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0
#define LV_ASSERT_HANDLER_INCLUDE <stdint.h>
#define LV_ASSERT_HANDLER while (1);
#define LV_USE_REFR_DEBUG 0
#define LV_USE_LAYER_DEBUG 0
#define LV_USE_PARALLEL_DRAW_DEBUG 0
#define LV_ENABLE_GLOBAL_CUSTOM 0

// Image caching (for splash screen and decoded images)
#define LV_CACHE_DEF_SIZE (512 * 1024)         // 512KB cache for decoded images
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 32

#define LV_GRADIENT_MAX_STOPS 2
#define LV_OBJ_STYLE_CACHE 1
#define LV_USE_OBJ_ID 0
#define LV_USE_OBJ_NAME 0
#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_FLOAT 1
#define LV_USE_MATRIX 1

#define LV_BIG_ENDIAN_SYSTEM 0
#define LV_ATTRIBUTE_TICK_INC
#define LV_ATTRIBUTE_TIMER_HANDLER
#define LV_ATTRIBUTE_FLUSH_READY
#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 1
#define LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY
#define LV_ATTRIBUTE_FAST_MEM
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning
#define LV_ATTRIBUTE_EXTERN_DATA

// Fonts
#define LV_FONT_MONTSERRAT_8 0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_DEFAULT &lv_font_montserrat_12
#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 1
#define LV_USE_FONT_PLACEHOLDER 1

// Text
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

// Widgets
#define LV_WIDGETS_HAS_DEFAULT_VALUE 1
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BUTTON 1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CANVAS 1
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 1
#define LV_USE_IMAGE 1
#define LV_USE_LABEL 1
#if LV_USE_LABEL
#define LV_LABEL_TEXT_SELECTION 1
#define LV_LABEL_LONG_TXT_HINT 1
#define LV_LABEL_WAIT_CHAR_COUNT 3
#endif
#define LV_USE_LED 0
#define LV_USE_LINE 1
#define LV_USE_LIST 1
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TABVIEW 0
#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LOTTIE 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABLE 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

// Themes
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 1
#define LV_USE_THEME_MONO 0

// Layouts
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

// 3rd party
#define LV_USE_FS_MEMFS 1
#if LV_USE_FS_MEMFS
#define LV_FS_MEMFS_LETTER 'M'
#endif
#define LV_USE_LODEPNG 0
#define LV_USE_LIBPNG 0
#define LV_USE_BMP 0
#define LV_USE_TJPGD 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_BARCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_RLOTTIE 0
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_SVG 0
#define LV_USE_FFMPEG 0
#define LV_USE_LZ4_INTERNAL 1
#define LV_BIN_DECODER_RAM_LOAD 1
#define LV_USE_RLE 1

// Others
#define LV_USE_SNAPSHOT 0

// System Monitor - usage: -D PERF_MON (CPU/FPS) and/or -D MEM_MON (memory)
#if defined(PERF_MON) || defined(MEM_MON)
    #define LV_USE_SYSMON 1
    #ifdef PERF_MON
        #define LV_USE_PERF_MONITOR 1
    #else
        #define LV_USE_PERF_MONITOR 0
    #endif
    #ifdef MEM_MON
        #define LV_USE_MEM_MONITOR 1
    #else
        #define LV_USE_MEM_MONITOR 0
    #endif
#else
    #define LV_USE_SYSMON 0
#endif

#define LV_USE_PROFILER 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_OBSERVER 1
#define LV_USE_IME_PINYIN 0
#define LV_USE_FILE_EXPLORER 0
#define LV_USE_FONT_MANAGER 0

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif
#endif
