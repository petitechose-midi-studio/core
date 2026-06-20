function(ms_core_add_project_file_tool)
    add_library(ms_core_project_file_open_control_native STATIC
        "${MS_CORE_OC_FRAMEWORK_DIR}/src/oc/state/NotificationQueue.cpp"
        "${MS_CORE_OC_NOTE_DIR}/src/oc/note/sequencer/StepSequencerGraph.cpp"
        "${MS_CORE_OC_NOTE_DIR}/src/oc/note/sequencer/StepSequencerState.cpp")
    target_include_directories(ms_core_project_file_open_control_native
        PUBLIC
            "${MS_CORE_OC_FRAMEWORK_DIR}/src"
            "${MS_CORE_OC_NOTE_DIR}/src")
    target_compile_features(ms_core_project_file_open_control_native PUBLIC cxx_std_20)

    add_library(ms_core_project_file_native STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/ProjectChunkMigration.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/ProjectFileContainer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/ProjectMigration.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/ProjectSnapshotPersistenceCodec.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/ProjectStatePersistenceCodec.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/SequencerPersistenceCodec.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/SequencerPersistenceEnvelope.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/MacroEditState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/StructureClipboardPastePlan.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/StructureClipboardState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/StructureSelectionState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/macro/MacroPagesState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/project/ProjectSnapshotLifecycle.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/project/ProjectSlug.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/project/ProjectState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerContentProjectionOps.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerContentStepOps.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerContentViewInternal.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerContentViewOps.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerGraphChildOps.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerGraphContentOps.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerGraphOps.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerGraphPropertyOps.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerHistory.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerSnapshotOps.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerStructureHistory.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerTrackBankOps.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerTrackBankState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/sequencer/SequencerUiState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/state/shared/StructureSlotOps.cpp")
    target_compile_definitions(ms_core_project_file_native PUBLIC OC_LOG)
    target_include_directories(ms_core_project_file_native
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(ms_core_project_file_native
        PUBLIC
            ms_core_project_file_open_control_native)

    add_executable(ms-core-file-tool
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/ms_core_file_tool/main.cpp")
    target_link_libraries(ms-core-file-tool PRIVATE ms_core_project_file_native)
    target_include_directories(ms-core-file-tool
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
endfunction()
