#!/usr/bin/env python3

from teensy_product_placement import (
    PAGE_STRUCTURE_BUILDER_FLASH_MARKERS,
    PAGE_STRUCTURE_FLASH_MARKERS,
    PAGE_STRUCTURE_GRAPH_FLASH_MARKERS,
    PAGE_STRUCTURE_HELPER_FLASH_MARKERS,
    PAGE_STRUCTURE_TRANSACTION_FLASH_MARKERS,
    TRACK_STRUCTURE_ADAPTER_FLASH_MARKERS,
    TRACK_STRUCTURE_FLASH_MARKERS,
    TRACK_STRUCTURE_PRESENTATION_FLASH_MARKERS,
    TRACK_STRUCTURE_WORKFLOW_FLASH_MARKERS,
    product_placement_violations,
)


def main() -> int:
    valid = """
1610613000 220 W oc::state::Signal<bool, 4u>::subscribe(std::function<void (bool const&)>)
1610613300 32 t std::_Function_handler<void ()>::_M_manager(std::_Any_data&, std::_Any_data const&, std::_Manager_operation)
280504 8 t __lv_binfont_create_from_buffer_veneer
281312 8 t __lv_draw_sw_box_shadow_veneer
1610613500 1472 T lv_binfont_create
1610615000 2748 T lv_draw_sw_box_shadow
1610618000 416 T FatFormatter::makeFat32()
1610619100 532 T core::handler::buildSequencerPageSelectionPasteMutationPlan(core::state::sequencer::SequencerState const&, core::state::StructureClipboardState const&, core::handler::SequencerPreparedPageStructureTarget, core::handler::SequencerPreparedPageStructureMutationPlan&)
1610619700 532 T core::handler::buildSequencerPageClearMutationPlan(core::state::sequencer::SequencerState const&, unsigned char, unsigned char, core::handler::SequencerPreparedPageStructureMutationPlan&)
1610620300 532 T core::handler::buildSequencerPageDeleteMutationPlan(core::state::sequencer::SequencerState const&, unsigned char, unsigned char, core::handler::SequencerPreparedPageStructureMutationPlan&)
1610620900 488 T core::handler::buildSequencerPagePasteMutationPlan(core::state::sequencer::SequencerState const&, core::state::StructureClipboardState const&, core::handler::SequencerPreparedPageStructureTarget, core::handler::SequencerPreparedPageStructureMutationPlan&)
1610621500 532 T core::handler::buildSequencerStepPasteMutationPlan(core::state::sequencer::SequencerState const&, core::state::StructureClipboardState const&, core::handler::SequencerPreparedStepPasteTarget, core::handler::SequencerPreparedPageStructureMutationPlan&)
1610622100 532 T core::handler::buildSequencerFocusedStepResetMutationPlan(core::state::sequencer::SequencerState const&, core::handler::SequencerPreparedFocusedStepResetTarget, core::handler::SequencerPreparedPageStructureMutationPlan&)
1610622700 532 T core::handler::buildSequencerStepSelectionResetMutationPlan(core::state::sequencer::SequencerState const&, oc::note::sequencer::StepBitMask128 const&, core::handler::SequencerPreparedStepSelectionResetTarget, core::handler::SequencerPreparedPageStructureMutationPlan&)
1610623300 532 T core::handler::buildSequencerPageSelectionResetMutationPlan(core::state::sequencer::SequencerState const&, unsigned char, unsigned short, core::handler::SequencerPreparedPageStructureMutationPlan&)
1610623900 532 T core::handler::buildSequencerPageSelectionDeleteOrDeepResetMutationPlan(core::state::sequencer::SequencerState const&, unsigned char, unsigned short, core::handler::SequencerPreparedPageStructureMutationPlan&)
1610625100 256 T core::handler::SequencerStructureEditWorkflow::pastePageSelectionAfterBoundary(core::handler::SequencerPreparedPageStructureTransaction&)
1610625700 256 T core::handler::SequencerStructureEditWorkflow::clearCurrentPageAfterBoundary(core::handler::SequencerPreparedPageStructureTransaction&)
1610626300 256 T core::handler::SequencerStructureEditWorkflow::deleteCurrentPageAfterBoundary(core::handler::SequencerPreparedPageStructureTransaction&)
1610626900 256 T core::handler::SequencerStructureEditWorkflow::pasteCurrentPageAfterBoundary(core::handler::SequencerPreparedPageStructureTransaction&)
1610627500 256 T core::handler::SequencerStructureEditWorkflow::pasteStepClipboardAfterBoundary(core::handler::SequencerPreparedPageStructureTransaction&)
1610628100 256 T core::handler::SequencerStructureEditWorkflow::resetFocusedStepAfterBoundary(core::handler::SequencerPreparedPageStructureTransaction&, core::handler::StepResetDepth)
1610628700 256 T core::handler::SequencerStructureEditWorkflow::resetStepSelectionAfterBoundary(core::handler::SequencerPreparedPageStructureTransaction&, core::handler::StepResetDepth)
1610629300 256 T core::handler::SequencerStructureEditWorkflow::resetPageSelectionAfterBoundary(core::handler::SequencerPreparedPageStructureTransaction&)
1610629900 256 T core::handler::SequencerStructureEditWorkflow::deleteOrResetPageSelectionAfterBoundary(core::handler::SequencerPreparedPageStructureTransaction&)
1610630500 104 T core::handler::SequencerPreparedPageStructureTransaction::openBoundary()
1610631100 368 T core::handler::SequencerPreparedPageStructureTransaction::execute(core::handler::SequencerPreparedPageStructureExecution const&)
1610631700 96 T oc::note::sequencer::StepSequencerGraph::sequence(unsigned short) const
1610632300 96 T oc::note::sequencer::StepSequencerGraph::cycleSet(unsigned short) const
1610632900 96 T core::handler::executeSequencerCreateTrackStructure(core::handler::SequencerDirectTrackStructureStateRefs)
1610633500 96 T core::handler::executeSequencerRemoveCurrentTrackStructure(core::handler::SequencerDirectTrackStructureStateRefs, unsigned char)
1610634100 256 t core::handler::(anonymous namespace)::captureIntent(core::handler::SequencerDirectTrackStructureStateRefs const&, core::handler::SequencerPreparedTrackStructureAction, unsigned char)
1610634700 256 t core::handler::(anonymous namespace)::executeDirect(core::handler::SequencerDirectTrackStructureStateRefs const&, core::handler::SequencerPreparedTrackStructureAction, unsigned char)
1610635300 256 t core::handler::(anonymous namespace)::intentStillMatches(core::handler::(anonymous namespace)::DirectContext const&, core::handler::SequencerPreparedTrackStructureAction)
1610635900 256 t core::handler::(anonymous namespace)::buildPlan(void const*, core::handler::SequencerPreparedTrackStructureAction, core::handler::SequencerPreparedTrackStructurePlan&)
1610636500 256 t core::handler::(anonymous namespace)::revalidate(void const*, core::handler::SequencerPreparedTrackStructurePlan const&, core::state::sequencer::SequencerHistoryTrackStructureChange const&)
1610637100 256 t core::handler::(anonymous namespace)::reconcileCommitted(void*, core::handler::SequencerPreparedTrackStructurePlan const&, core::state::sequencer::SequencerHistoryTrackStructureChange const&)
1610637700 256 t core::handler::(anonymous namespace)::settleSuccessful(void*, core::handler::SequencerPreparedTrackStructurePlan const&)
1610638300 96 T core::handler::SequencerStructureEditWorkflow::createPreviewedTrackStructure()
1610638900 96 T core::handler::SequencerStructureEditWorkflow::beginSelectionHoldAction(core::state::StructureHoldAction)
1610639500 96 T core::handler::SequencerStructureEditWorkflow::currentTrackRemoveIntentMatches(unsigned char) const
1610640100 96 T core::handler::SequencerStructureEditWorkflow::selectionTrackRemoveIntentMatches(core::handler::SequencerStructureEditWorkflow::TrackSelectionHoldToken const&, unsigned char) const
1610640700 96 T core::handler::SequencerStructureEditWorkflow::applyLatchedCurrentTrackShortPress()
1610641300 96 T core::handler::SequencerStructureEditWorkflow::applyLatchedTrackSelectionShortPress()
1610641900 96 T core::handler::SequencerStructureEditWorkflow::applyLatchedTrackSelectionLongPress()
1610642500 96 t core::handler::(anonymous namespace)::reconcilePreparedTrackPresentationFromCoreState(void*, core::handler::PreparedTrackPresentationKind, unsigned short)
1610643100 96 T core::handler::SharedTrackDomainServices::reconcilePreparedMacroTrackTransfer(unsigned short) const
1610643700 96 T core::handler::SharedTrackDomainServices::canReconcilePreparedSequencerActiveTrackPresentation() const
1610644300 96 T core::handler::SharedTrackDomainServices::reconcilePreparedSequencerActiveTrackPresentation() const
1610644900 96 T core::state::CoreState::reconcilePreparedSequencerActiveTrackPresentation()
1610645500 96 T core::state::CoreState::reconcilePreparedMacroTrackTransfer(unsigned short)
1610646100 96 T core::state::macro::MacroWorkflow::syncActivePagePresentation(core::state::MacroState&, core::state::macro::MacroPagesState const&, core::state::macro::MacroUiState&)
34348 324 T core::handler::MacroValueHandler::handleValueChange(unsigned char, float)
24016 648 T core::handler::MacroAutomationPlaybackService::update(unsigned long)
54180 596 T core::sequencer::RealtimeMidiQueue::pushBatchImpl_(void)
54856 1888 T core::sequencer::SequencerCcLaneRuntime::buildMusicalTickFrame(void)
97048 752 T core::ui::MacroView::processRenderFlags(unsigned long)
86548 702 T core::ui::StepGrid::renderTile(void)
539099136 153600 B Buffer::lvgl
"""
    assert len(PAGE_STRUCTURE_BUILDER_FLASH_MARKERS) == 9
    assert len(PAGE_STRUCTURE_HELPER_FLASH_MARKERS) == 9
    assert len(PAGE_STRUCTURE_TRANSACTION_FLASH_MARKERS) == 2
    assert len(PAGE_STRUCTURE_GRAPH_FLASH_MARKERS) == 2
    assert len(PAGE_STRUCTURE_FLASH_MARKERS) == 22
    assert len(set(PAGE_STRUCTURE_FLASH_MARKERS)) == 22
    assert len(TRACK_STRUCTURE_ADAPTER_FLASH_MARKERS) == 9
    assert len(TRACK_STRUCTURE_WORKFLOW_FLASH_MARKERS) == 7
    assert len(TRACK_STRUCTURE_PRESENTATION_FLASH_MARKERS) == 7
    assert len(TRACK_STRUCTURE_FLASH_MARKERS) == 23
    assert len(set(TRACK_STRUCTURE_FLASH_MARKERS)) == 23
    assert product_placement_violations(valid) == ()

    invalid = valid.replace(
        "1610613000 220 W oc::state::Signal<bool, 4u>::subscribe",
        "24000 220 W oc::state::Signal<bool, 4u>::subscribe",
    ).replace(
        "34348 324 T core::handler::MacroValueHandler::handleValueChange",
        "1610620000 324 T core::handler::MacroValueHandler::handleValueChange",
    ).replace(
        "539099136 153600 B Buffer::lvgl",
        "539099136 230400 B Buffer::lvgl",
    )
    violations = product_placement_violations(invalid)
    assert "Signal subscription setup must execute from Flash" in violations
    assert any("MacroValueHandler" in item for item in violations)
    assert "LVGL draw buffer must be one 320x240 RGB565 frame in RAM2" in violations

    valid_lines = valid.splitlines()
    for marker in (*PAGE_STRUCTURE_FLASH_MARKERS, *TRACK_STRUCTURE_FLASH_MARKERS):
        symbol_line = next(line for line in valid_lines if marker in line)

        missing = "\n".join(line for line in valid_lines if marker not in line)
        assert (
            f"required cold Flash symbol is missing: {marker}"
            in product_placement_violations(missing)
        )

        itcm = valid.replace(
            symbol_line,
            symbol_line.replace(symbol_line.split(maxsplit=1)[0], "24000", 1),
        )
        assert (
            f"cold symbol must execute from Flash: {marker}"
            in product_placement_violations(itcm)
        )

    print("Teensy product placement parser: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
