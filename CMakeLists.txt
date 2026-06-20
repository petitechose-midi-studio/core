cmake_minimum_required(VERSION 3.29)

project(petitechose_midi_studio_core LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(MS_CORE_OPEN_CONTROL_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../open-control"
    CACHE PATH "Path to the local open-control workspace")

set(MS_CORE_OC_FRAMEWORK_DIR "${MS_CORE_OPEN_CONTROL_ROOT}/framework")
set(MS_CORE_OC_NOTE_DIR "${MS_CORE_OPEN_CONTROL_ROOT}/note")

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

if(NOT TARGET oc_note_native)
    set(OC_NOTE_BUILD_TESTS OFF CACHE BOOL "Build OpenControl note tests" FORCE)
    add_subdirectory(
        "${MS_CORE_OC_NOTE_DIR}"
        "${CMAKE_BINARY_DIR}/open-control-note")
endif()

file(GLOB_RECURSE MS_CORE_NATIVE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/state/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sequencer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/validation/ux/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/handler/common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/handler/macro/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/handler/project/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/handler/sequencer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/handler/settings/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/handler/transport/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/handler/view/*.cpp")

list(APPEND MS_CORE_NATIVE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/context/standalone/SequencerEncoderSyncCoordinator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/common/GlobalTrackNavigationStripModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/sequencer/StepContentBadgeProjection.cpp")

add_library(midi_studio_core_native STATIC ${MS_CORE_NATIVE_SOURCES})
target_compile_definitions(midi_studio_core_native PUBLIC OC_LOG)
target_include_directories(midi_studio_core_native
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(midi_studio_core_native PUBLIC oc_note_native oc_framework_native)

add_executable(ms-core-file-tool
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/ms_core_file_tool/main.cpp")
target_link_libraries(ms-core-file-tool PRIVATE midi_studio_core_native)
target_include_directories(ms-core-file-tool
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(MS_CORE_BUILD_TESTS_DEFAULT OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(MS_CORE_BUILD_TESTS_DEFAULT ON)
endif()

option(
    MS_CORE_BUILD_TESTS
    "Build and register MIDI Studio core unit tests"
    ${MS_CORE_BUILD_TESTS_DEFAULT})

if(MS_CORE_BUILD_TESTS AND BUILD_TESTING)
    file(GLOB MS_CORE_NATIVE_TESTS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_*/test_main.cpp")

    set(MS_CORE_NATIVE_TEST_TARGETS)

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

        add_test(NAME "${test_name}" COMMAND "${test_name}")
        set_tests_properties("${test_name}" PROPERTIES LABELS core)
    endforeach()

    if(PROJECT_IS_TOP_LEVEL)
        add_custom_target(test-native
            COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --parallel --output-on-failure
            DEPENDS ${MS_CORE_NATIVE_TEST_TARGETS}
            USES_TERMINAL)
    endif()
endif()
