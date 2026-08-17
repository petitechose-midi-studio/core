import re


FLASH_START = 0x60000000
ITCM_END = 0x00080000
RAM2_START = 0x20200000
RAM2_END = 0x20280000
RGB565_FRAME_BYTES = 320 * 240 * 2

_NM_SYMBOL_RE = re.compile(r"^(\d+)\s+(\d+)\s+([A-Za-z])\s+(.+)$")

PAGE_STRUCTURE_BUILDER_FLASH_MARKERS = (
    "buildSequencerPageSelectionPasteMutationPlan(",
    "buildSequencerPageClearMutationPlan(",
    "buildSequencerPageDeleteMutationPlan(",
    "buildSequencerPagePasteMutationPlan(",
    "buildSequencerStepPasteMutationPlan(",
    "buildSequencerFocusedStepResetMutationPlan(",
    "buildSequencerStepSelectionResetMutationPlan(",
    "buildSequencerPageSelectionResetMutationPlan(",
    "buildSequencerPageSelectionDeleteOrDeepResetMutationPlan(",
)

PAGE_STRUCTURE_HELPER_FLASH_MARKERS = (
    "SequencerStructureEditWorkflow::pastePageSelectionAfterBoundary(",
    "SequencerStructureEditWorkflow::clearCurrentPageAfterBoundary(",
    "SequencerStructureEditWorkflow::deleteCurrentPageAfterBoundary(",
    "SequencerStructureEditWorkflow::pasteCurrentPageAfterBoundary(",
    "SequencerStructureEditWorkflow::pasteStepClipboardAfterBoundary(",
    "SequencerStructureEditWorkflow::resetFocusedStepAfterBoundary(",
    "SequencerStructureEditWorkflow::resetStepSelectionAfterBoundary(",
    "SequencerStructureEditWorkflow::resetPageSelectionAfterBoundary(",
    "SequencerStructureEditWorkflow::deleteOrResetPageSelectionAfterBoundary(",
)

PAGE_STRUCTURE_TRANSACTION_FLASH_MARKERS = (
    "SequencerPreparedPageStructureTransaction::openBoundary()",
    "SequencerPreparedPageStructureTransaction::execute(",
)

PAGE_STRUCTURE_GRAPH_FLASH_MARKERS = (
    "oc::note::sequencer::StepSequencerGraph::sequence(",
    "oc::note::sequencer::StepSequencerGraph::cycleSet(",
)

PAGE_STRUCTURE_FLASH_MARKERS = (
    *PAGE_STRUCTURE_BUILDER_FLASH_MARKERS,
    *PAGE_STRUCTURE_HELPER_FLASH_MARKERS,
    *PAGE_STRUCTURE_TRANSACTION_FLASH_MARKERS,
    *PAGE_STRUCTURE_GRAPH_FLASH_MARKERS,
)

TRACK_STRUCTURE_ADAPTER_FLASH_MARKERS = (
    "executeSequencerCreateTrackStructure(",
    "executeSequencerRemoveCurrentTrackStructure(",
    "executeSequencerRemoveSelectionTrackStructure(",
    "(anonymous namespace)::captureIntent(",
    "(anonymous namespace)::executeDirect(",
    "(anonymous namespace)::intentStillMatches(",
    "(anonymous namespace)::buildPlan(void const*, core::handler::SequencerPreparedTrackStructureAction",
    "(anonymous namespace)::revalidate(void const*, core::handler::SequencerPreparedTrackStructurePlan const&",
    "(anonymous namespace)::reconcileCommitted(void*, core::handler::SequencerPreparedTrackStructurePlan const&",
    "(anonymous namespace)::settleSuccessful(void*, core::handler::SequencerPreparedTrackStructurePlan const&",
)

TRACK_STRUCTURE_WORKFLOW_FLASH_MARKERS = (
    "SequencerStructureEditWorkflow::createPreviewedTrackStructure(",
    "SequencerStructureEditWorkflow::beginSelectionHoldAction(",
    "SequencerStructureEditWorkflow::currentTrackRemoveIntentMatches(",
    "SequencerStructureEditWorkflow::selectionTrackRemoveIntentMatches(",
    "SequencerStructureEditWorkflow::applyLatchedCurrentTrackShortPress(",
    "SequencerStructureEditWorkflow::applyLatchedTrackSelectionShortPress(",
    "SequencerStructureEditWorkflow::applyLatchedTrackSelectionLongPress(",
)

TRACK_STRUCTURE_PRESENTATION_FLASH_MARKERS = (
    "(anonymous namespace)::reconcilePreparedTrackPresentationFromCoreState(",
    "SharedTrackDomainServices::reconcilePreparedMacroTrackTransfer(",
    "SharedTrackDomainServices::canReconcilePreparedSequencerActiveTrackPresentation(",
    "SharedTrackDomainServices::reconcilePreparedSequencerActiveTrackPresentation(",
    "CoreState::reconcilePreparedSequencerActiveTrackPresentation(",
    "CoreState::reconcilePreparedMacroTrackTransfer(",
    "MacroWorkflow::syncActivePagePresentation(",
)

TRACK_TRANSFER_FLASH_MARKERS = (
    "prepareSequencerTrackTransfer(",
    "commitPreparedSequencerTrackTransfer(",
    "executeSequencerTrackTransfer(",
    "(anonymous namespace)::clipboardPayloadFingerprint(",
    "(anonymous namespace)::prepareMacroStructureTransfer(",
    "(anonymous namespace)::statusForChronology(",
)

TRACK_STRUCTURE_FLASH_MARKERS = (
    *TRACK_STRUCTURE_ADAPTER_FLASH_MARKERS,
    *TRACK_STRUCTURE_WORKFLOW_FLASH_MARKERS,
    *TRACK_STRUCTURE_PRESENTATION_FLASH_MARKERS,
    *TRACK_TRANSFER_FLASH_MARKERS,
)

MACRO_DIRECT_TRACK_STRUCTURE_FLASH_MARKERS = (
    "executeMacroDeleteTrackStructure(",
    "executeMacroResetTrackStructure(",
    "executeMacroPasteTrackStructure(",
    "executeMacroCreateTrackStructure(",
    "executePrepared(core::handler::(anonymous namespace)::DirectContext&)",
    "executeDirect(core::state::CoreState&",
    "validIntent(core::handler::(anonymous namespace)::DirectContext const&)",
    "pasteSourcesMatch(core::handler::(anonymous namespace)::DirectContext const&)",
    "intentStillMatches(core::handler::(anonymous namespace)::DirectContext const&)",
    "clearManualAndMaybeSync(core::handler::(anonymous namespace)::DirectContext&",
    "settleNoChange(void*, core::handler::SequencerPreparedTrackStructurePlan const&)",
    "settleSuccessful(void*, core::handler::SequencerPreparedTrackStructurePlan const&)",
    "reconcileCommitted(void*, core::handler::SequencerPreparedTrackStructurePlan const&",
    "prepareMacroAfter(void const*, core::handler::SequencerPreparedTrackStructurePlan const&",
    "buildPlan(void const*, core::handler::SequencerPreparedTrackStructureAction",
    "revalidate(void const*, core::handler::SequencerPreparedTrackStructurePlan const&",
)

COUPLED_HISTORY_REPLAY_FLASH_MARKERS = (
    "CoreState::traverseSequencerHistory_(",
    "CoreState::armPreparedSequencerHistoryActivation_(",
    "CoreState::traversePreparedSequencerStructureHistory_(",
    "CoreState::traverseGenericSequencerHistory_(",
    "CoreState::publishSequencerHistoryTraversal_(",
    "SequencerHistoryService::prepareStructureHistoryReplay(",
    "SequencerHistoryService::commitPreparedStructureHistoryReplay(",
    "prepareHistoryStructureReplayOwners(",
    "commitPreparedHistoryStructureReplayState(",
    "SequencerPreparedStructureHistoryReplay::reset()",
)


def _symbols(nm_output: str) -> tuple[tuple[int, int, str, str], ...]:
    symbols: list[tuple[int, int, str, str]] = []
    for line in nm_output.splitlines():
        match = _NM_SYMBOL_RE.match(line.strip())
        if match is None:
            continue
        symbols.append(
            (
                int(match.group(1)),
                int(match.group(2)),
                match.group(3),
                match.group(4),
            )
        )
    return tuple(symbols)


def _code_addresses(
    symbols: tuple[tuple[int, int, str, str], ...],
    marker: str,
) -> tuple[int, ...]:
    return tuple(
        address
        for address, _size, symbol_type, name in symbols
        if symbol_type in "TtWw" and marker in name
    )


def _exact_code_addresses(
    symbols: tuple[tuple[int, int, str, str], ...],
    expected_name: str,
) -> tuple[int, ...]:
    return tuple(
        address
        for address, _size, symbol_type, name in symbols
        if symbol_type in "TtWw" and name == expected_name
    )


def product_placement_violations(nm_output: str) -> tuple[str, ...]:
    symbols = _symbols(nm_output)
    violations: list[str] = []

    flash_markers = (
        "FatFormatter::makeFat32(",
        *PAGE_STRUCTURE_FLASH_MARKERS,
        *TRACK_STRUCTURE_FLASH_MARKERS,
        *MACRO_DIRECT_TRACK_STRUCTURE_FLASH_MARKERS,
        *COUPLED_HISTORY_REPLAY_FLASH_MARKERS,
    )
    for marker in flash_markers:
        matches = _code_addresses(symbols, marker)
        if not matches:
            violations.append(f"required cold Flash symbol is missing: {marker}")
        elif any(address < FLASH_START for address in matches):
            violations.append(f"cold symbol must execute from Flash: {marker}")

    for name in ("lv_binfont_create", "lv_draw_sw_box_shadow"):
        matches = _exact_code_addresses(symbols, name)
        if not matches:
            violations.append(f"required cold Flash symbol is missing: {name}")
        elif any(address < FLASH_START for address in matches):
            violations.append(f"cold symbol must execute from Flash: {name}")

    managers = _code_addresses(symbols, "::_M_manager(")
    if not managers:
        violations.append("std::function manager symbols are missing from the ELF")
    elif any(address < FLASH_START for address in managers):
        violations.append("std::function managers must execute from Flash")

    signal_subscriptions = tuple(
        address
        for address, _size, symbol_type, name in symbols
        if symbol_type in "TtWw"
        and "oc::state::Signal<" in name
        and ">::subscribe(" in name
    )
    if not signal_subscriptions:
        violations.append("Signal subscription symbols are missing from the ELF")
    elif any(address < FLASH_START for address in signal_subscriptions):
        violations.append("Signal subscription setup must execute from Flash")

    extmem_lifecycle = tuple(
        (address, name)
        for address, _size, symbol_type, name in symbols
        if symbol_type in "TtWw"
        and (
            name.startswith("core::app::allocateExtmemStrict(")
            or name.startswith("core::app::freeExtmemStrict(")
            or name.startswith("core::app::makeExtmemUnique")
            or name.startswith("core::app::ExtmemDeleter<")
            or name.startswith("core::app::ExtmemArrayDeleter<")
            or (
                "core::app::Extmem" in name
                and (
                    (
                        name.startswith("std::unique_ptr<")
                        and "::~unique_ptr()" in name
                    )
                    or (
                        name.startswith("std::__uniq_ptr_impl<")
                        and ">::reset(" in name
                    )
                )
            )
        )
    )
    if not extmem_lifecycle:
        violations.append("strict PSRAM lifecycle symbols are missing from the ELF")
    elif any(address < FLASH_START for address, _name in extmem_lifecycle):
        violations.append("strict PSRAM allocation/lifecycle must execute from Flash")

    hot_markers = (
        "core::handler::MacroValueHandler::handleValueChange(",
        "core::handler::MacroAutomationPlaybackService::update(",
        "core::sequencer::RealtimeMidiQueue::pushBatchImpl_(",
        "core::sequencer::SequencerCcLaneRuntime::buildMusicalTickFrame(",
        "core::ui::MacroView::processRenderFlags(",
        "core::ui::StepGrid::renderTile(",
    )
    for marker in hot_markers:
        matches = _code_addresses(symbols, marker)
        if not matches:
            violations.append(f"required realtime ITCM symbol is missing: {marker}")
        elif any(address >= ITCM_END for address in matches):
            violations.append(f"realtime symbol must execute from ITCM: {marker}")

    lvgl_buffers = tuple(
        (address, size)
        for address, size, symbol_type, name in symbols
        if symbol_type in "BbDdVv"
        and name == "ms::device_support::v1::buffers::lvgl"
    )
    if not lvgl_buffers:
        violations.append("RGB565 LVGL draw buffer is missing from the ELF")
    elif any(
        size != RGB565_FRAME_BYTES
        or address < RAM2_START
        or address + size > RAM2_END
        for address, size in lvgl_buffers
    ):
        violations.append(
            "LVGL draw buffer must be one 320x240 RGB565 frame in RAM2"
        )

    return tuple(violations)
