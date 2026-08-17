cmake_minimum_required(VERSION 3.29)

project(petitechose_midi_studio_core LANGUAGES CXX)

include(CTest)

# Firmware and its native contract suite deliberately share GNU C++17.
# Host-only targets opt into portable C++20 where their graph requires it.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(
    MS_CORE_ENABLE_SANITIZERS
    "Instrument native Core boundary tests with AddressSanitizer and UBSan"
    OFF)
option(
    MS_CORE_ENABLE_FUZZING
    "Build the opt-in libFuzzer boundary harnesses"
    OFF)

add_library(ms_core_compile_warnings INTERFACE)
if(MSVC)
    target_compile_options(ms_core_compile_warnings INTERFACE /W4)
else()
    target_compile_options(ms_core_compile_warnings INTERFACE -Wall -Wextra)
endif()

if(MS_CORE_ENABLE_FUZZING AND NOT MS_CORE_ENABLE_SANITIZERS)
    message(FATAL_ERROR
        "MS_CORE_ENABLE_FUZZING requires MS_CORE_ENABLE_SANITIZERS")
endif()

set(MS_CORE_OPEN_CONTROL_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../open-control"
    CACHE PATH "Path to the local open-control workspace")

set(MS_CORE_OC_FRAMEWORK_DIR "${MS_CORE_OPEN_CONTROL_ROOT}/framework")
set(MS_CORE_OC_NOTE_DIR "${MS_CORE_OPEN_CONTROL_ROOT}/note")
set(MS_CORE_OC_HAL_COMMON_DIR "${MS_CORE_OPEN_CONTROL_ROOT}/hal-common")
set(MS_DEVICE_SUPPORT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../device-support"
    CACHE PATH "Path to the MIDI Studio device-support checkout")

set(MS_CORE_NOTIFICATION_QUEUE_CAPACITY 96)

if(NOT EXISTS "${MS_CORE_OC_FRAMEWORK_DIR}/src/oc/Config.hpp")
    message(FATAL_ERROR "open-control framework not found at ${MS_CORE_OC_FRAMEWORK_DIR}")
endif()

if(NOT EXISTS "${MS_CORE_OC_NOTE_DIR}/src/oc/note/clock/ClockConstants.hpp")
    message(FATAL_ERROR "open-control note not found at ${MS_CORE_OC_NOTE_DIR}")
endif()

if(NOT EXISTS "${MS_DEVICE_SUPPORT_DIR}/src/ms/device_support/v1/Version.hpp")
    message(FATAL_ERROR "MIDI Studio device-support not found at ${MS_DEVICE_SUPPORT_DIR}")
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
    PUBLIC
        OC_MAX_PENDING_NOTIFICATIONS=${MS_CORE_NOTIFICATION_QUEUE_CAPACITY}
        # Tests must exercise the product registry, not OpenControl's small
        # library defaults. Otherwise enabling a real feature can silently
        # drop later bindings and manufacture false navigation failures.
        OC_MAX_BUTTONS=48
        OC_MAX_BUTTON_BINDINGS=256
        OC_MAX_ENCODER_BINDINGS=96)

if(NOT TARGET oc_note_native)
    set(OC_NOTE_BUILD_TESTS OFF CACHE BOOL "Build OpenControl note tests" FORCE)
    add_subdirectory(
        "${MS_CORE_OC_NOTE_DIR}"
        "${CMAKE_BINARY_DIR}/open-control-note")
endif()

target_compile_features(oc_framework_native PUBLIC cxx_std_17)
target_compile_features(oc_note_native PUBLIC cxx_std_17)

# The versioned device-support boundary is intentionally independent of the
# full Core native library. Its sole implementation owns the LVGL PSRAM pool;
# public board/config headers are exercised by a separate strict-C++17 test.
add_library(ms_device_support_native STATIC
    "${MS_DEVICE_SUPPORT_DIR}/src/ms/device_support/v1/LvglMemory.cpp")
set_target_properties(ms_device_support_native PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(ms_device_support_native
    PUBLIC "${MS_DEVICE_SUPPORT_DIR}/src"
    PRIVATE "${MS_CORE_OC_FRAMEWORK_DIR}/src")
target_compile_definitions(ms_device_support_native PUBLIC
    MS_DEVICE_SUPPORT_LVGL_MEMORY_POOL_SIZE_BYTES=1048576U)
add_library(ms::device_support_native ALIAS ms_device_support_native)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MsCoreSources.cmake")

add_library(midi_studio_core_native STATIC ${MS_CORE_NATIVE_SOURCES})
target_compile_definitions(midi_studio_core_native PUBLIC OC_LOG)
target_include_directories(midi_studio_core_native
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${MS_DEVICE_SUPPORT_DIR}/src"
    PRIVATE
        # Native Core tests intentionally compile a small set of UI projection
        # sources. Their font/icon headers include LVGL types, supplied by the
        # same deterministic stub used by the individual test executables.
        "${CMAKE_CURRENT_SOURCE_DIR}/test")
target_link_libraries(midi_studio_core_native
    PUBLIC oc_note_native oc_framework_native
    PRIVATE ms_core_compile_warnings)

if(MS_CORE_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR
            "MS_CORE_ENABLE_SANITIZERS requires a Clang or GCC compiler")
    endif()
    if(WIN32 AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "MS_CORE_ENABLE_SANITIZERS requires Clang on Windows")
    endif()

    add_library(ms_core_sanitizers INTERFACE)
    target_compile_options(ms_core_sanitizers INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer)
    target_link_options(ms_core_sanitizers INTERFACE
        -fsanitize=address,undefined)
    if(MS_CORE_ENABLE_FUZZING)
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR
                "MS_CORE_ENABLE_FUZZING requires Clang with libFuzzer")
        endif()
        target_compile_options(ms_core_sanitizers INTERFACE
            -fsanitize=fuzzer-no-link)
    endif()
    if(WIN32)
        # Core boundary fixtures retain large product aggregates on the host
        # stack. ASan redzones need more than Windows' 1 MiB executable default;
        # this is test capacity, not a firmware stack-budget relaxation.
        target_link_options(ms_core_sanitizers INTERFACE
            -Xlinker /STACK:16777216)
    endif()

    target_link_libraries(oc_framework_native PUBLIC ms_core_sanitizers)
    target_link_libraries(oc_note_native PUBLIC ms_core_sanitizers)
    target_link_libraries(midi_studio_core_native PUBLIC ms_core_sanitizers)

    if(WIN32)
        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            message(FATAL_ERROR
                "MS_CORE_ENABLE_SANITIZERS supports 64-bit Windows only")
        endif()
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}"
                    "--print-file-name=clang_rt.asan_dynamic-x86_64.dll"
            OUTPUT_VARIABLE MS_CORE_SANITIZER_RUNTIME
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
        if(NOT EXISTS "${MS_CORE_SANITIZER_RUNTIME}")
            message(FATAL_ERROR
                "Clang AddressSanitizer runtime not found: "
                "${MS_CORE_SANITIZER_RUNTIME}")
        endif()
        configure_file("${MS_CORE_SANITIZER_RUNTIME}"
            "${CMAKE_BINARY_DIR}/clang_rt.asan_dynamic-x86_64.dll"
            COPYONLY)
    endif()
endif()

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

if(MS_CORE_BUILD_TESTS AND BUILD_TESTING AND
   NOT EXISTS "${MS_CORE_OC_HAL_COMMON_DIR}/src/oc/hal/common/embedded/ButtonDef.hpp")
    message(FATAL_ERROR "open-control HAL Common not found at ${MS_CORE_OC_HAL_COMMON_DIR}")
endif()

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
        NAME check_TeensyPostLinkGateV1
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/script/dev/test-teensy-post-link-gate.py")
    set_tests_properties(check_TeensyPostLinkGateV1 PROPERTIES LABELS core)

    add_test(
        NAME check_TeensyDiagnosticsPlacement
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/script/dev/test-teensy-diagnostics-placement.py")
    set_tests_properties(check_TeensyDiagnosticsPlacement PROPERTIES LABELS core)

    file(GLOB MS_CORE_NATIVE_TESTS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_*/test_main.cpp")

    set(MS_CORE_NATIVE_TEST_TARGETS)
    set(MS_CORE_STANDALONE_TESTS
        test_ActiveViewLifecycle
        test_ForegroundCadence
        test_MacroKnobVisualPolicy
        test_ModulatorEnvelopeParameterMapping
        test_ModulatorEnvelopeTiming
        test_ModulatorLfoParameterMapping
        test_PsramSpanTracker
        test_StandaloneSequencerRuntimeGate
        test_StorageQualificationProbe
        test_TimingCadenceContracts
        test_UxReplayTimeline)

    # Each listed contract needs only the same-named implementation below.
    set(MS_CORE_ISOLATED_TEST_SOURCES
        src/persistence/StorageRecoveryMachine.cpp
        src/sequencer/ClockSourceSelector.cpp
        src/sequencer/ExternalClockEstimator.cpp
        src/sequencer/MidiCcResolutionTelemetryExchange.cpp
        src/sequencer/ProjectModulationTriggerQueue.cpp
        src/state/StructureSelectionInteractionPolicy.cpp
        src/state/interaction/TextKeyboardLayout.cpp
        src/state/macro/MacroAutomationDomain.cpp
        src/state/macro/MacroEditMenuModel.cpp
        src/state/macro/MacroInteractionPolicy.cpp
        src/state/project/ProjectSlug.cpp
        src/state/sequencer/SequencerNoteSpelling.cpp
        src/state/shared/MidiCcDestinationResolver.cpp
        src/state/shared/StructureSlotOps.cpp)
    foreach(isolated_test_source IN LISTS MS_CORE_ISOLATED_TEST_SOURCES)
        get_filename_component(isolated_test_name "${isolated_test_source}" NAME_WE)
        set("MS_CORE_ISOLATED_TEST_SOURCE_test_${isolated_test_name}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${isolated_test_source}")
    endforeach()

    add_test(
        NAME test_CoreFileToolInputLimits
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/script/dev/test-core-file-tool-input-limits.py"
                --tool "$<TARGET_FILE:ms-core-file-tool>")
    set_tests_properties(test_CoreFileToolInputLimits PROPERTIES
        LABELS "core;host-tool")
    list(APPEND MS_CORE_NATIVE_TEST_TARGETS ms-core-file-tool)

    set(MS_DEVICE_SUPPORT_CONTRACT_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_DeviceSupportContract/test_main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_DeviceSupportContract/include_version.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_DeviceSupportContract/include_timing.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_DeviceSupportContract/include_input_ids.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_DeviceSupportContract/include_input_config.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_DeviceSupportContract/include_display.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_DeviceSupportContract/include_hardware.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_DeviceSupportContract/include_buffers.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_DeviceSupportContract/include_lvgl_memory.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_DeviceSupportContract/include_lv_conf.cpp")
    list(REMOVE_ITEM MS_CORE_NATIVE_TESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_DeviceSupportContract/test_main.cpp")

    add_executable(test_DeviceSupportContract
        ${MS_DEVICE_SUPPORT_CONTRACT_TEST_SOURCES})
    set_target_properties(test_DeviceSupportContract PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_compile_definitions(test_DeviceSupportContract PRIVATE
        OC_MAX_BUTTONS=48)
    target_include_directories(test_DeviceSupportContract
        BEFORE PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/test/device_support_stubs")
    target_include_directories(test_DeviceSupportContract
        PRIVATE
            "${MS_DEVICE_SUPPORT_DIR}/src"
            "${MS_CORE_OC_FRAMEWORK_DIR}/src"
            "${MS_CORE_OC_HAL_COMMON_DIR}/src")
    target_link_libraries(test_DeviceSupportContract
        PRIVATE ms::device_support_native)
    if(MSVC)
        target_compile_options(test_DeviceSupportContract PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_DeviceSupportContract PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_DeviceSupportContract COMMAND test_DeviceSupportContract)
    set_tests_properties(test_DeviceSupportContract PROPERTIES
        LABELS "core;device-support")
    list(APPEND MS_CORE_NATIVE_TEST_TARGETS test_DeviceSupportContract)

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

    set(MS_CORE_SANITIZER_BOUNDARY_TESTS
        test_AtomicProductFile
        test_FileSystemRpc
        test_SequencerCcLanePersistence)

    foreach(test_source IN LISTS MS_CORE_NATIVE_TESTS)
        get_filename_component(test_dir "${test_source}" DIRECTORY)
        get_filename_component(test_name "${test_dir}" NAME)
        list(APPEND MS_CORE_NATIVE_TEST_TARGETS "${test_name}")

        add_executable("${test_name}" "${test_source}")
        set(isolated_test_source
            "${MS_CORE_ISOLATED_TEST_SOURCE_${test_name}}")
        if(test_name IN_LIST MS_CORE_STANDALONE_TESTS OR isolated_test_source)
            target_include_directories("${test_name}"
                PRIVATE
                    "${MS_DEVICE_SUPPORT_DIR}/src"
                    "${MS_CORE_OC_FRAMEWORK_DIR}/src"
                    "${MS_CORE_OC_NOTE_DIR}/src")
        else()
            target_link_libraries("${test_name}" PRIVATE midi_studio_core_native)
        endif()
        if(isolated_test_source)
            target_sources("${test_name}" PRIVATE "${isolated_test_source}")
            target_link_libraries("${test_name}" PRIVATE ms_core_compile_warnings)
        endif()
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

    if(MS_CORE_ENABLE_SANITIZERS)
        set_tests_properties(${MS_CORE_SANITIZER_BOUNDARY_TESTS} PROPERTIES
            ENVIRONMENT
                "ASAN_OPTIONS=halt_on_error=1:abort_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")

        if(PROJECT_IS_TOP_LEVEL)
            list(JOIN MS_CORE_SANITIZER_BOUNDARY_TESTS "|"
                MS_CORE_SANITIZER_BOUNDARY_REGEX)
            add_custom_target(test-sanitized-boundaries
                COMMAND "${CMAKE_CTEST_COMMAND}"
                        --test-dir "${CMAKE_BINARY_DIR}"
                        --tests-regex "^(${MS_CORE_SANITIZER_BOUNDARY_REGEX})$"
                        --output-on-failure
                DEPENDS ${MS_CORE_SANITIZER_BOUNDARY_TESTS}
                USES_TERMINAL
                VERBATIM)
        endif()
    endif()

    if(PROJECT_IS_TOP_LEVEL)
        add_custom_target(test-native
            COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --parallel --output-on-failure
            DEPENDS ${MS_CORE_NATIVE_TEST_TARGETS}
            USES_TERMINAL)
    endif()
endif()

if(MS_CORE_ENABLE_FUZZING)
    add_executable(fuzz_FileSystemRpc EXCLUDE_FROM_ALL
        "${CMAKE_CURRENT_SOURCE_DIR}/test/fuzz/FileSystemRpcFuzzer.cpp")
    add_executable(fuzz_SequencerPatternEnvelope EXCLUDE_FROM_ALL
        "${CMAKE_CURRENT_SOURCE_DIR}/test/fuzz/SequencerPatternEnvelopeFuzzer.cpp")
    target_link_libraries(fuzz_FileSystemRpc PRIVATE midi_studio_core_native)
    target_link_libraries(fuzz_SequencerPatternEnvelope PRIVATE midi_studio_core_native)
    target_link_options(fuzz_FileSystemRpc PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_SequencerPatternEnvelope PRIVATE -fsanitize=fuzzer)

    if(PROJECT_IS_TOP_LEVEL)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/fuzz-artifacts")
        add_custom_target(fuzz-smoke
            COMMAND "${CMAKE_COMMAND}" -E env
                    "ASAN_OPTIONS=halt_on_error=1:abort_on_error=1"
                    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
                    "$<TARGET_FILE:fuzz_FileSystemRpc>"
                    -runs=2000
                    -max_total_time=5
                    -max_len=32768
                    -dict=${CMAKE_CURRENT_SOURCE_DIR}/test/fuzz/FileSystemRpc.dict
                    -artifact_prefix=${CMAKE_BINARY_DIR}/fuzz-artifacts/
            COMMAND "${CMAKE_COMMAND}" -E env
                    "ASAN_OPTIONS=halt_on_error=1:abort_on_error=1"
                    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
                    "$<TARGET_FILE:fuzz_SequencerPatternEnvelope>"
                    -runs=2000
                    -max_total_time=5
                    -max_len=16445
                    -dict=${CMAKE_CURRENT_SOURCE_DIR}/test/fuzz/SequencerPatternEnvelope.dict
                    -artifact_prefix=${CMAKE_BINARY_DIR}/fuzz-artifacts/
            DEPENDS fuzz_FileSystemRpc fuzz_SequencerPatternEnvelope
            USES_TERMINAL
            VERBATIM)
    endif()
endif()
