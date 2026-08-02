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

/* Subscription teardown is lifecycle-only.  Cancellation made reset() large
 * enough for GCC to emit standalone std::array destructors for the retained
 * subscription banks; keep both the primitive and those loops out of ITCM. */
*(.text._ZN2oc5state12Subscription5resetEv*)
*(.text._ZNSt5arrayIN2oc5state12SubscriptionE*D*Ev)

/* Prepared FullBank History owners are created and destroyed only around
 * authored transactions.  Template helpers emitted outside their FLASHMEM
 * callers belong beside the cold provider, not in realtime code RAM. */
*(.text._ZN4core3app16makeExtmemUniqueINS_5state9sequencer30SequencerHistoryFullBankChangeE*)
*(.text._ZNSt15__uniq_ptr_implIN4core5state9sequencer30SequencerHistoryFullBankChangeE*)

/* Page/Step/Track Structure plans are human-rate authored transactions.
 * Select ordinary per-function sections by object so dead action families remain
 * eligible for --gc-sections instead of sharing one monolithic .flashmem. */
*SequencerPreparedPageStructureMutationPlan.cpp.o(.text* .rodata*)
*SequencerPreparedPageStructureTransaction.cpp.o(.text* .rodata*)
*SequencerDirectTrackStructureTransaction.cpp.o(.text* .rodata*)
*SequencerPreparedTrackStructurePlanValidation.cpp.o(.text* .rodata*)
*SequencerPreparedTrackStructureTransaction.cpp.o(.text* .rodata*)

/* Track Structure coordination, exact History snapshots, and owner rotation
 * execute only at authored transaction/replay boundaries. Their explicitly
 * FLASHMEM entry points can cause GCC to emit lambdas, Signal setters, array
 * comparisons, and fill clones back into ordinary sections; keep the whole
 * cold ownership surface together in cached Flash. */
*SharedTrackDomainServices.cpp.o(.text* .rodata*)
*SequencerHistory.cpp.o(.text* .rodata*)
*SequencerStructureHistory.cpp.o(.text* .rodata*)
*SequencerTrackBankOps.cpp.o(.text* .rodata*)

/* Project workspace allocation is a save/load boundary operation. This
 * inline helper may be emitted by more than one persistence translation unit. */
*(.text._ZN4core11persistence20ProjectFileWorkspace7prepareEv*)

/* Snapshot batches and their compiler-emitted Graph/Signal helpers are used by
 * authored History transactions, never playback. Keep COMDAT selection from
 * moving these cold accessors into ITCM as callsites evolve. */
*SequencerSnapshotOps.cpp.o(.text* .rodata*)
*(.text._ZNK2oc4note9sequencer18StepSequencerGraph8sequenceEt*)
*(.text._ZNK2oc4note9sequencer18StepSequencerGraph8cycleSetEt*)

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
