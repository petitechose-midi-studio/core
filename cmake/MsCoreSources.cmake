get_filename_component(MS_CORE_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(MS_CORE_SOURCE_ROOT "${MS_CORE_ROOT_DIR}/src")

set(MS_CORE_NATIVE_SOURCE_PATTERNS
    "${MS_CORE_SOURCE_ROOT}/state/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/protocol/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/sequencer/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/validation/ux/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/validation/project/ProjectModulationBenchmark.cpp"
    "${MS_CORE_SOURCE_ROOT}/handler/common/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/handler/macro/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/handler/project/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/handler/sequencer/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/handler/settings/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/handler/transport/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/handler/view/*.cpp")

file(GLOB_RECURSE MS_CORE_NATIVE_SOURCES CONFIGURE_DEPENDS
    ${MS_CORE_NATIVE_SOURCE_PATTERNS})

set(MS_CORE_NATIVE_EXTRA_SOURCES
    "${MS_CORE_SOURCE_ROOT}/context/standalone/MacroOverlayInvalidationBindings.cpp"
    "${MS_CORE_SOURCE_ROOT}/context/standalone/MacroViewActivationContract.cpp"
    "${MS_CORE_SOURCE_ROOT}/context/standalone/SequencerChordFieldPresentation.cpp"
    "${MS_CORE_SOURCE_ROOT}/context/standalone/SequencerEncoderSyncCoordinator.cpp"
    "${MS_CORE_SOURCE_ROOT}/ui/common/GlobalTrackNavigationStripModel.cpp"
    "${MS_CORE_SOURCE_ROOT}/ui/modulation/ModulatorAdsrUiModel.cpp"
    "${MS_CORE_SOURCE_ROOT}/ui/sequencer/SequencerChordPresetPresentation.cpp"
    "${MS_CORE_SOURCE_ROOT}/ui/sequencer/SequencerPresetLibraryPresentation.cpp"
    "${MS_CORE_SOURCE_ROOT}/ui/sequencer/SequencerPatternTimelineModel.cpp"
    "${MS_CORE_SOURCE_ROOT}/ui/sequencer/SequencerStepContentDraftTransitionLabels.cpp"
    "${MS_CORE_SOURCE_ROOT}/ui/sequencer/StepContentBadgeProjection.cpp")
list(APPEND MS_CORE_NATIVE_EXTRA_SOURCES
    "${MS_CORE_SOURCE_ROOT}/ui/project/ProjectTrackEditorViewModel.cpp"
    "${MS_CORE_SOURCE_ROOT}/ui/sequencer/SequencerTrackPastePreflightViewModel.cpp"
    "${MS_CORE_SOURCE_ROOT}/ui/sequencer/SequencerTrackPastePendingViewModel.cpp")

list(APPEND MS_CORE_NATIVE_SOURCES ${MS_CORE_NATIVE_EXTRA_SOURCES})

set(MS_CORE_PROJECT_FILE_OPEN_CONTROL_SOURCES
    "${MS_CORE_OC_FRAMEWORK_DIR}/src/oc/state/NotificationQueue.cpp"
    "${MS_CORE_OC_NOTE_DIR}/src/oc/note/sequencer/StepSequencerChord.cpp"
    "${MS_CORE_OC_NOTE_DIR}/src/oc/note/sequencer/StepSequencerChordProjection.cpp"
    "${MS_CORE_OC_NOTE_DIR}/src/oc/note/sequencer/StepSequencerChordSpec.cpp"
    "${MS_CORE_OC_NOTE_DIR}/src/oc/note/sequencer/StepSequencerGraph.cpp"
    "${MS_CORE_OC_NOTE_DIR}/src/oc/note/sequencer/StepSequencerState.cpp")

set(MS_CORE_PROJECT_FILE_CORE_SOURCES
    "${MS_CORE_SOURCE_ROOT}/persistence/PersistenceChecksum.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectControlAutomationPersistence.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectControlModulationPersistence.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectControlPersistenceCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectFileContainer.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectFileInspection.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/MacroTrackBankPersistenceCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectSnapshotPersistenceCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectStatePersistenceCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectTrackStatePersistenceCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/SequencerCcLanePersistenceCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/SequencerGraphAssetCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/SequencerGraphRecordCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/SequencerPersistenceCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/SequencerPersistenceEnvelope.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/MacroEditState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/contextual/OperationFeedbackState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/StructureClipboardPastePlan.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/StructureClipboardState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/StructureNavigationState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroAutomationDomain.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistory.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryAssignmentInternals.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryAutomationInternals.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryCommit.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryModulatorAudition.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryModulatorCreationInternals.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryModulatorDeleteInternals.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryModulatorTopology.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryRecordedCreationInternals.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryRecordedEditInternals.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryRecordedShapeCreation.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryRecordedShapeEdit.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryRedo.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistorySlotEdits.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistorySnapshots.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryStructureInternals.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryUndo.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroHistoryValueEdits.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroPagesState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/modulation/ProjectControlMacroConversion.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/modulation/ProjectControlMacroCurveOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/modulation/ProjectControlState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/modulation/ProjectControlMacroOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/modulation/ProjectControlMacroOpsInternal.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/modulation/ProjectModulationAutomationOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/modulation/ProjectModulationBindingOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/modulation/ProjectModulationDomainOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/modulation/ProjectModulationDomainQueries.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/modulation/ProjectModulationDomainValidation.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/modulation/ProjectModulatorSourceOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/project/ProjectSnapshotLifecycle.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/project/ProjectSlug.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/project/ProjectState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/project/ProjectHistoryCoordinator.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerContentProjectionOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerContentStepOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerContentViewInternal.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerContentViewOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerStepContentDraftSession.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerStepContentDraftOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerCcLaneDomain.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerCcLanePatternOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerCcLaneProjectionOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerChordContextProjection.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphAsset.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphCanonicalPolicy.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphChildOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphContentOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphPropertyOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerHistory.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerPatternRegionOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerPatternRandomizeOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerPatternRandomizeSession.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerPatternEditorState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerPatternEditorOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerPitchEditAuthority.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerQuickControlsDraft.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerSnapshotOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerStructureHistory.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerTrackBankOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerTrackBankState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerUiState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/shared/StructureSlotOps.cpp")

function(ms_core_assert_sources_exist)
    foreach(source IN LISTS ARGN)
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "Core source not found: ${source}")
        endif()
    endforeach()
endfunction()

ms_core_assert_sources_exist(
    ${MS_CORE_NATIVE_EXTRA_SOURCES}
    ${MS_CORE_PROJECT_FILE_OPEN_CONTROL_SOURCES}
    ${MS_CORE_PROJECT_FILE_CORE_SOURCES})
