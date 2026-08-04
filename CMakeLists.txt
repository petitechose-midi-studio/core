cmake_minimum_required(VERSION 3.29)

project(petitechose_midi_studio_core LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(MS_CORE_OPEN_CONTROL_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../open-control"
    CACHE PATH "Path to the local open-control workspace")

set(MS_CORE_OC_FRAMEWORK_DIR "${MS_CORE_OPEN_CONTROL_ROOT}/framework")
set(MS_CORE_OC_NOTE_DIR "${MS_CORE_OPEN_CONTROL_ROOT}/note")

set(MS_CORE_NOTIFICATION_QUEUE_CAPACITY 96)

if(NOT EXISTS "${MS_CORE_OC_FRAMEWORK_DIR}/src/oc/Config.hpp")
    message(FATAL_ERROR "open-control framework not found at ${MS_CORE_OC_FRAMEWORK_DIR}")
endif()

if(NOT EXISTS "${MS_CORE_OC_NOTE_DIR}/src/oc/note/clock/ClockConstants.hpp")
    message(FATAL_ERROR "open-control note not found at ${MS_CORE_OC_NOTE_DIR}")
endif()

if(NOT TARGET oc_framework_native)
    set(OC_FRAMEWORK_BUILD_TESTS OFF CACHE BOOL "Build OpenControl framework tests" FORCE)
    add_subdirectory(
        "${MS_CORE_OC_FRAMEWORK_DIR}"
        "${CMAKE_BINARY_DIR}/open-control-framework")
endif()

# A full sequencer-bank apply with an active-track transition produces a
# measured 66-entry atomic notification wave. Apply the Core-specific capacity
# directly to the singleton's target, including when a parent project supplied
# that target, and propagate the identical layout to every consumer.
target_compile_definitions(
    oc_framework_native
    PUBLIC OC_MAX_PENDING_NOTIFICATIONS=${MS_CORE_NOTIFICATION_QUEUE_CAPACITY})

if(NOT TARGET oc_note_native)
    set(OC_NOTE_BUILD_TESTS OFF CACHE BOOL "Build OpenControl note tests" FORCE)
    add_subdirectory(
        "${MS_CORE_OC_NOTE_DIR}"
        "${CMAKE_BINARY_DIR}/open-control-note")
endif()

target_compile_features(oc_framework_native PUBLIC cxx_std_20)
target_compile_features(oc_note_native PUBLIC cxx_std_20)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MsCoreSources.cmake")

add_library(midi_studio_core_native STATIC ${MS_CORE_NATIVE_SOURCES})
target_compile_definitions(midi_studio_core_native PUBLIC OC_LOG)
target_include_directories(midi_studio_core_native
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        # Native Core tests intentionally compile a small set of UI projection
        # sources. Their font/icon headers include LVGL types, supplied by the
        # same deterministic stub used by the individual test executables.
        "${CMAKE_CURRENT_SOURCE_DIR}/test")
target_link_libraries(midi_studio_core_native PUBLIC oc_note_native oc_framework_native)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MsCoreProjectFileTool.cmake")
ms_core_add_project_file_tool()

set(MS_CORE_BUILD_TESTS_DEFAULT OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(MS_CORE_BUILD_TESTS_DEFAULT ON)
endif()

option(
    MS_CORE_BUILD_TESTS
    "Build and register MIDI Studio core unit tests"
    ${MS_CORE_BUILD_TESTS_DEFAULT})

if(MS_CORE_BUILD_TESTS AND BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # Native tests can fail one EXTMEM allocation deterministically. The hook
    # is absent from firmware and from non-test native builds.
    target_compile_definitions(
        midi_studio_core_native
        PUBLIC MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION=1)

    add_test(
        NAME check_ArchitectureContracts
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/script/dev/check-architecture-contracts.py")
    set_tests_properties(check_ArchitectureContracts PROPERTIES LABELS core)

    # The 202-case architecture-checker mutation suite is a tooling meta-test,
    # not a product regression test. Keep it explicit so `ms test core` stays
    # interactive; run this target only when the checker itself changes.
    add_custom_target(check-architecture-contracts-self-test
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/script/dev/check-architecture-contracts.py"
                "--self-test"
        USES_TERMINAL)

    add_test(
        NAME check_SequencerHistoryInventory
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/script/dev/check-sequencer-history-inventory.py")
    set_tests_properties(check_SequencerHistoryInventory PROPERTIES LABELS core)

    add_test(
        NAME check_SequencerHistoryInventorySelfTest
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/script/dev/check-sequencer-history-inventory.py"
                "--self-test")
    set_tests_properties(check_SequencerHistoryInventorySelfTest PROPERTIES LABELS core)

    add_test(
        NAME check_TeensyMemoryBudgetParser
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/script/dev/test-teensy-memory-budget.py")
    set_tests_properties(check_TeensyMemoryBudgetParser PROPERTIES LABELS core)

    add_test(
        NAME check_TeensyDiagnosticsPlacement
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/script/dev/test-teensy-diagnostics-placement.py")
    set_tests_properties(check_TeensyDiagnosticsPlacement PROPERTIES LABELS core)

    file(GLOB MS_CORE_NATIVE_TESTS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_*/test_main.cpp")

    set(MS_CORE_NATIVE_TEST_TARGETS)
    set(MS_CORE_PERSISTENCE_IO_TESTS
        test_AtomicProductFile
        test_CoreStatePersistence
        test_FileSystemRpc
        test_ProductFileService
        test_ProjectFileStore
        test_ProjectHandler
        test_ProjectSessionAutosaveService
        test_ProjectSessionRestoreService
        test_ProjectSessionStore
        test_SequencerStepEditHandler
        test_StepPresetFileStore)

    foreach(test_source IN LISTS MS_CORE_NATIVE_TESTS)
        get_filename_component(test_dir "${test_source}" DIRECTORY)
        get_filename_component(test_name "${test_dir}" NAME)
        list(APPEND MS_CORE_NATIVE_TEST_TARGETS "${test_name}")

        add_executable("${test_name}" "${test_source}")
        target_link_libraries("${test_name}" PRIVATE midi_studio_core_native)
        target_include_directories("${test_name}"
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/test"
                "${CMAKE_CURRENT_SOURCE_DIR}/src")
        if(test_name STREQUAL test_StorageQualificationProbe)
            target_sources("${test_name}" PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics/StorageQualificationProbe.cpp")
            target_compile_definitions("${test_name}" PRIVATE
                MS_STORAGE_QUALIFICATION=1
                OC_ENABLE_STATS=1)
        endif()
        if(test_name STREQUAL test_ModulatorSparklineModel)
            target_include_directories("${test_name}"
                PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../ui/src")
        endif()
        if(MSVC)
            target_compile_options("${test_name}" PRIVATE /UNDEBUG)
        else()
            target_compile_options("${test_name}" PRIVATE -UNDEBUG)
        endif()

        add_test(NAME "${test_name}" COMMAND "${test_name}")
        set_tests_properties("${test_name}" PROPERTIES LABELS core)
        if(test_name IN_LIST MS_CORE_PERSISTENCE_IO_TESTS)
            set_tests_properties("${test_name}" PROPERTIES
                RESOURCE_LOCK ms_core_persistence_io)
        endif()
        if(test_name STREQUAL test_CoreStatePersistence)
            set_tests_properties("${test_name}" PROPERTIES RUN_SERIAL TRUE)
        endif()
    endforeach()

    if(PROJECT_IS_TOP_LEVEL)
        add_custom_target(test-native
            COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --parallel --output-on-failure
            DEPENDS ${MS_CORE_NATIVE_TEST_TARGETS}
            USES_TERMINAL)
    endif()
endif()
