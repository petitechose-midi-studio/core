get_filename_component(MS_CORE_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(MS_CORE_SOURCE_ROOT "${MS_CORE_ROOT_DIR}/src")

set(MS_CORE_NATIVE_SOURCE_PATTERNS
    "${MS_CORE_SOURCE_ROOT}/state/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/protocol/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/sequencer/*.cpp"
    "${MS_CORE_SOURCE_ROOT}/validation/ux/*.cpp"
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
    "${MS_CORE_SOURCE_ROOT}/context/standalone/SequencerEncoderSyncCoordinator.cpp"
    "${MS_CORE_SOURCE_ROOT}/ui/common/GlobalTrackNavigationStripModel.cpp"
    "${MS_CORE_SOURCE_ROOT}/ui/sequencer/StepContentBadgeProjection.cpp")

list(APPEND MS_CORE_NATIVE_SOURCES ${MS_CORE_NATIVE_EXTRA_SOURCES})

set(MS_CORE_PROJECT_FILE_OPEN_CONTROL_SOURCES
    "${MS_CORE_OC_FRAMEWORK_DIR}/src/oc/state/NotificationQueue.cpp"
    "${MS_CORE_OC_NOTE_DIR}/src/oc/note/sequencer/StepSequencerChord.cpp"
    "${MS_CORE_OC_NOTE_DIR}/src/oc/note/sequencer/StepSequencerGraph.cpp"
    "${MS_CORE_OC_NOTE_DIR}/src/oc/note/sequencer/StepSequencerState.cpp")

set(MS_CORE_PROJECT_FILE_CORE_SOURCES
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectChunkMigration.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectFileContainer.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectMigration.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectSnapshotPersistenceCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/ProjectStatePersistenceCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/SequencerPersistenceCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/persistence/SequencerPersistenceEnvelope.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/MacroEditState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/StructureClipboardPastePlan.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/StructureClipboardState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/StructureSelectionState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroAutomationState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/macro/MacroPagesState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/project/ProjectSnapshotLifecycle.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/project/ProjectSlug.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/project/ProjectState.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerContentProjectionOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerContentStepOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerContentViewInternal.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerContentViewOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphAssetCodec.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphChildOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphContentOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphPresetWorkflow.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerGraphPropertyOps.cpp"
    "${MS_CORE_SOURCE_ROOT}/state/sequencer/SequencerHistory.cpp"
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
