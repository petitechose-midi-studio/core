cmake_minimum_required(VERSION 3.14)
project(midi_studio_desktop C CXX)

# ==============================================================================
# C/C++ Standards
# ==============================================================================
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Debug build if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Generate compile_commands.json for IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Debug symbols for GDB
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0")
endif()

# ==============================================================================
# Project paths
# ==============================================================================
set(MIDI_STUDIO_ROOT "${CMAKE_SOURCE_DIR}/..")
set(OPEN_CONTROL_ROOT "${MIDI_STUDIO_ROOT}/../../open-control")

# midi-studio config structure
set(CONFIG_DIR "${MIDI_STUDIO_ROOT}/src/config")
set(CONFIG_NATIVE "${MIDI_STUDIO_ROOT}/src/config/platform-native")
set(CORE_SRC "${MIDI_STUDIO_ROOT}/src")

# PlatformIO deps (from midi-studio/core build)
# Use "dev" environment (or "release" - check which exists)
if(EXISTS "${MIDI_STUDIO_ROOT}/.pio/libdeps/dev")
    set(PIO_ENV_DIR "${MIDI_STUDIO_ROOT}/.pio/libdeps/dev")
elseif(EXISTS "${MIDI_STUDIO_ROOT}/.pio/libdeps/release")
    set(PIO_ENV_DIR "${MIDI_STUDIO_ROOT}/.pio/libdeps/release")
else()
    message(FATAL_ERROR "No PlatformIO environment found. Run 'pio run' in midi-studio/core first.")
endif()

# ==============================================================================
# Resolve PlatformIO dependency
# ==============================================================================
function(resolve_pio_dep DEP_NAME OUT_VAR)
    set(DEP_DIR "${PIO_ENV_DIR}/${DEP_NAME}")
    set(DEP_LINK "${PIO_ENV_DIR}/${DEP_NAME}.pio-link")

    if(EXISTS "${DEP_DIR}" AND IS_DIRECTORY "${DEP_DIR}")
        set(${OUT_VAR} "${DEP_DIR}" PARENT_SCOPE)
        return()
    endif()

    if(EXISTS "${DEP_LINK}")
        file(READ "${DEP_LINK}" LINK_CONTENT)
        string(REGEX MATCH "\"uri\": *\"symlink://([^\"]+)\"" _ "${LINK_CONTENT}")
        if(CMAKE_MATCH_1)
            get_filename_component(RESOLVED_PATH "${MIDI_STUDIO_ROOT}/${CMAKE_MATCH_1}" ABSOLUTE)
            set(${OUT_VAR} "${RESOLVED_PATH}" PARENT_SCOPE)
            return()
        endif()
    endif()

    message(FATAL_ERROR "${DEP_NAME} not found. Run 'pio pkg install' in midi-studio/core first.")
endfunction()

# ==============================================================================
# Dependencies
# ==============================================================================
resolve_pio_dep("lvgl" LVGL_DIR)

set(FRAMEWORK_DIR "${OPEN_CONTROL_ROOT}/framework")
set(UI_LVGL_DIR "${OPEN_CONTROL_ROOT}/ui-lvgl")
set(HAL_DESKTOP_DIR "${OPEN_CONTROL_ROOT}/hal-desktop")
set(UI_LVGL_COMPONENTS_DIR "${OPEN_CONTROL_ROOT}/ui-lvgl-components")

message(STATUS "LVGL: ${LVGL_DIR}")
message(STATUS "Framework: ${FRAMEWORK_DIR}")
message(STATUS "UI-LVGL: ${UI_LVGL_DIR}")
message(STATUS "HAL-Desktop: ${HAL_DESKTOP_DIR}")
message(STATUS "UI-LVGL-Components: ${UI_LVGL_COMPONENTS_DIR}")
message(STATUS "Config Common: ${CONFIG_DIR}")
message(STATUS "Config Native: ${CONFIG_NATIVE}")

# ==============================================================================
# SDL2 (FetchContent)
# ==============================================================================
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/deps")

message(STATUS "Fetching SDL2...")
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.10
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)

if(WIN32)
    set(PKG_CONFIG_EXECUTABLE "" CACHE STRING "" FORCE)
endif()

FetchContent_MakeAvailable(SDL2)
FetchContent_GetProperties(SDL2 SOURCE_DIR SDL2_SOURCE_DIR)
set(SDL2_INCLUDE_DIR "${SDL2_SOURCE_DIR}/include")

# ==============================================================================
# libremidi (FetchContent) - Real MIDI support
# ==============================================================================
message(STATUS "Fetching libremidi...")
FetchContent_Declare(
    libremidi
    GIT_REPOSITORY https://github.com/celtera/libremidi.git
    GIT_TAG        v5.4.1
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

set(LIBREMIDI_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(LIBREMIDI_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBREMIDI_TESTS OFF CACHE BOOL "" FORCE)
set(LIBREMIDI_NO_WINMIDI ON CACHE BOOL "" FORCE)
set(LIBREMIDI_NO_WINUWP ON CACHE BOOL "" FORCE)
set(LIBREMIDI_DOWNLOAD_CPPWINRT OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(libremidi)

# ==============================================================================
# LVGL Configuration
# ==============================================================================
# LVGL requires lv_conf.h to be found during configuration.
# LV_BUILD_CONF_DIR tells LVGL where to find lv_conf.h
# Our config is located in config/platform-native/
set(LV_BUILD_CONF_DIR "${CONFIG_NATIVE}" CACHE STRING "Path to lv_conf.h directory" FORCE)

set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl)

target_include_directories(lvgl PUBLIC
    ${CONFIG_NATIVE}
    ${SDL2_INCLUDE_DIR}
)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

# ==============================================================================
# Open Control Framework sources
# ==============================================================================
file(GLOB_RECURSE FRAMEWORK_SOURCES "${FRAMEWORK_DIR}/src/*.cpp")
file(GLOB_RECURSE UI_LVGL_SOURCES "${UI_LVGL_DIR}/src/*.cpp")
file(GLOB_RECURSE HAL_DESKTOP_SOURCES "${HAL_DESKTOP_DIR}/src/*.cpp")
file(GLOB_RECURSE UI_LVGL_COMPONENTS_SOURCES "${UI_LVGL_COMPONENTS_DIR}/src/*.cpp")

# Exclude Teensy-specific main.cpp files from framework sources
list(FILTER UI_LVGL_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

# ==============================================================================
# midi-studio sources (config/ structure)
# ==============================================================================
# Platform native sources (HwSimulator, etc.)
file(GLOB_RECURSE NATIVE_PLATFORM_SOURCES "${CONFIG_NATIVE}/*.cpp")

# Core business logic
file(GLOB_RECURSE CORE_SOURCES "${CORE_SRC}/*.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/config/platform-teensy/.*")

# ==============================================================================
# Desktop entry point
# ==============================================================================
set(DESKTOP_MAIN
    ${CMAKE_SOURCE_DIR}/main.cpp
)

# ==============================================================================
# Main executable
# ==============================================================================
add_executable(midi_studio_desktop
    ${DESKTOP_MAIN}
    ${NATIVE_PLATFORM_SOURCES}
    ${CORE_SOURCES}
    ${FRAMEWORK_SOURCES}
    ${UI_LVGL_SOURCES}
    ${HAL_DESKTOP_SOURCES}
    ${UI_LVGL_COMPONENTS_SOURCES}
)

target_include_directories(midi_studio_desktop PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CONFIG_DIR}
    ${CONFIG_NATIVE}
    ${CORE_SRC}
    ${FRAMEWORK_DIR}/src
    ${UI_LVGL_DIR}/src
    ${HAL_DESKTOP_DIR}/src
    ${UI_LVGL_COMPONENTS_DIR}/include
    ${SDL2_INCLUDE_DIR}
)

target_link_libraries(midi_studio_desktop PRIVATE
    lvgl
    SDL2::SDL2-static
    libremidi
)

# Define OC_DESKTOP for platform-conditional code (if needed)
target_compile_definitions(midi_studio_desktop PRIVATE OC_DESKTOP=1
    OC_LOG=1
)

# ==============================================================================
# Platform-specific
# ==============================================================================
if(WIN32)
    if(MSVC)
        target_link_options(midi_studio_desktop PRIVATE "/SUBSYSTEM:CONSOLE")
    else()
        target_link_options(midi_studio_desktop PRIVATE "-mconsole")
    endif()
    target_link_libraries(midi_studio_desktop PRIVATE winmm imm32 setupapi version dwmapi)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(midi_studio_desktop PRIVATE m pthread)
endif()

# ==============================================================================
# Output (platform-specific subdirectory)
# ==============================================================================
if(NOT DEFINED PLATFORM_ID)
    set(PLATFORM_ID "default")
endif()

set(BIN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${PLATFORM_ID}")

set_target_properties(midi_studio_desktop PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${BIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BIN_OUTPUT_DIR}"
)

# ==============================================================================
# Build instructions
# ==============================================================================
# Windows (MinGW):
#   mkdir build && cd build
#   cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug ..
#   cmake --build .
#
# Windows (MSVC):
#   mkdir build && cd build
#   cmake -G "Visual Studio 17 2022" ..
#   cmake --build . --config Debug
#
# Linux:
#   mkdir build && cd build
#   cmake -DCMAKE_BUILD_TYPE=Debug ..
#   cmake --build .
