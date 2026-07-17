/*
 * Input-section selectors for code that is never part of the realtime render,
 * MIDI, automation, or sequencer dispatch paths.
 *
 * This fragment is included inside .text.progmem before the generic .text*
 * and .rodata* collectors. Keep runtime std::function invokers in ITCM: only
 * their setup/destruction managers and subscription construction live here.
 */

/* Fixed-topology callback construction and teardown. */
*(.text.*_M_manager*)
*(.text.*9subscribe*)

/* Logging formatting is an error/diagnostic path, never a realtime path. */
*(.text._ZN2oc3log6detail*)
*(.rodata._ZN2oc3log6detail*)

/* Font parsing happens during UI bootstrap; glyph rendering uses other code. */
*lv_binfont_loader.c.o(.text* .rodata*)
*lv_bin_decoder.c.o(.text* .rodata*)

/* The product theme does not use software shadows. Retain the fallback in
 * Flash so an accidental style remains functionally correct without spending
 * scarce ITCM on an inactive renderer. */
*lv_draw_sw_blur.c.o(.text* .rodata*)
*lv_draw_sw_box_shadow.c.o(.text* .rodata*)

/* Filesystem formatting is restricted to initialization/recovery. Normal
 * project and preset reads/writes retain their existing placement. */
*FatFormatter.cpp.o(.text* .rodata*)
*ExFatFormatter.cpp.o(.text* .rodata*)

/* Macro overlay projection is rebuilt only after authored/UI state changes.
 * Its top-level formatters already use FLASHMEM, but out-of-line text helpers
 * emitted by the compiler otherwise leak into ITCM. Keep the complete cold
 * projection object and its labels together in memory-mapped Flash. */
*MacroOverlayPresenterFormatters.cpp.o(.text* .rodata*)

/* Project handlers serve modal navigation and authored edits. They are never
 * entered by the control evaluator, MIDI publication, sequencer tick, or LVGL
 * draw loop; compiler-generated dispatch thunks therefore belong in Flash. */
*ProjectHandler*.cpp.o(.text* .rodata*)
