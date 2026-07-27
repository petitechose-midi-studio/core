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

/* Compressed font assets are decoded only while UI resources are loaded. */
*lz4.c.o(.text* .rodata*)

/* FAT/exFAT path resolution and handle opening happen only at storage session
 * boundaries. Keep streaming read/write primitives in ITCM, but move every
 * open traversal helper out of the realtime code budget. */
*(.text._ZN9ExFatFile4open*)
*(.text._ZN9ExFatFile8openRoot*)
*(.text._ZN9ExFatFile8openNext*)
*(.text._ZN9ExFatFile11openPrivate*)
*(.text._ZN7FatFile4open*)
*(.text._ZN7FatFile8openRoot*)
*(.text._ZN7FatFile8openNext*)
*(.text._ZN7FatFile11openCluster*)
*(.text._ZN7FatFile15openCachedEntry*)

/* Macro overlay projection is rebuilt only after authored/UI state changes.
 * Its top-level formatters already use FLASHMEM, but out-of-line text helpers
 * emitted by the compiler otherwise leak into ITCM. Keep every formatter
 * partition and its labels together in memory-mapped Flash. */
*MacroOverlayPresenter*Formatters.cpp.o(.text* .rodata*)

/* Project handlers serve modal navigation and authored edits. They are never
 * entered by the control evaluator, MIDI publication, sequencer tick, or LVGL
 * draw loop; compiler-generated dispatch thunks therefore belong in Flash. */
*ProjectHandler*.cpp.o(.text* .rodata*)

/* Sequencer and Macro performance handlers dispatch human-rate controller
 * gestures into workflows. Their named methods already use FLASHMEM; keep the
 * compiler-generated std::function predicate/action thunks beside them instead
 * of spending ITCM reserved for playback, MIDI, and retained draw paths. */
*MacroPerformanceHandler.cpp.o(.text* .rodata*)
*SequencerStepHandler.cpp.o(.text* .rodata*)

/* Macro history is entered only by authored edit, undo, and redo commands.
 * It is never part of macro playback, modulation evaluation, MIDI dispatch,
 * or retained-view rendering.  Keep compiler-emitted smart-pointer helpers
 * beside the already-FLASHMEM history transactions instead of leaking those
 * cold templates into scarce ITCM. */
*MacroHistory.cpp.o(.text* .rodata*)

/* Domain codecs run only at explicit project/session save or restore
 * boundaries. Keep low-level filesystem streaming in ITCM, but execute
 * encode/decode logic from cached Flash so persistence does not consume the
 * realtime code budget. */
*PersistenceCodec.cpp.o(.text* .rodata*)
*PersistenceEnvelope.cpp.o(.text* .rodata*)
