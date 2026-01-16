# ==============================================================================
# CMakeLists.txt for MIDI Studio WebAssembly build
# ==============================================================================
# Emscripten build using SDL2 provided by Emscripten (-s USE_SDL=2)
#
# Usage:
#   source /path/to/emsdk/emsdk_env.sh
#   mkdir build && cd build
#   emcmake cmake ..
#   emmake make -j4
# ==============================================================================

cmake_minimum_required(VERSION 3.14)
project(midi_studio_wasm C CXX)

# ==============================================================================
# Verify Emscripten
# ==============================================================================
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This CMakeLists.txt is for Emscripten builds only. Use emcmake cmake ..")
endif()

message(STATUS "Building MIDI Studio for WebAssembly")

# ==============================================================================
# C/C++ Standards
# ==============================================================================
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

# ==============================================================================
# Emscripten compiler flags
# ==============================================================================
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s USE_SDL=2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_SDL=2")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# ==============================================================================
# Project paths
# ==============================================================================
set(SDL_DIR "${CMAKE_SOURCE_DIR}/..")
set(MIDI_STUDIO_ROOT "${SDL_DIR}/..")
set(OPEN_CONTROL_ROOT "${MIDI_STUDIO_ROOT}/../../open-control")

set(CONFIG_DIR "${MIDI_STUDIO_ROOT}/src/config")
set(CORE_SRC "${MIDI_STUDIO_ROOT}/src")

# PlatformIO deps
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
set(HAL_SDL_DIR "${OPEN_CONTROL_ROOT}/hal-sdl")
set(HAL_NET_DIR "${OPEN_CONTROL_ROOT}/hal-net")
set(HAL_MIDI_DIR "${OPEN_CONTROL_ROOT}/hal-midi")
set(UI_LVGL_COMPONENTS_DIR "${OPEN_CONTROL_ROOT}/ui-lvgl-components")

message(STATUS "LVGL: ${LVGL_DIR}")
message(STATUS "SDL Dir: ${SDL_DIR}")

# ==============================================================================
# libremidi (WebMIDI backend for Emscripten)
# ==============================================================================
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/deps")

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

FetchContent_MakeAvailable(libremidi)

# ==============================================================================
# LVGL Configuration (lv_conf.h in sdl/wasm/)
# ==============================================================================
set(LV_BUILD_CONF_DIR "${CMAKE_SOURCE_DIR}" CACHE STRING "Path to lv_conf.h directory" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl)

target_include_directories(lvgl PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

# ==============================================================================
# Source files
# ==============================================================================
file(GLOB_RECURSE FRAMEWORK_SOURCES "${FRAMEWORK_DIR}/src/*.cpp")
file(GLOB_RECURSE UI_LVGL_SOURCES "${UI_LVGL_DIR}/src/*.cpp")
file(GLOB_RECURSE HAL_SDL_SOURCES "${HAL_SDL_DIR}/src/*.cpp")
file(GLOB_RECURSE HAL_MIDI_SOURCES "${HAL_MIDI_DIR}/src/*.cpp")
file(GLOB_RECURSE UI_LVGL_COMPONENTS_SOURCES "${UI_LVGL_COMPONENTS_DIR}/src/*.cpp")

# HAL_NET: Only WebSocketTransport for WASM
set(HAL_NET_WASM_SOURCES "${HAL_NET_DIR}/src/oc/hal/net/WebSocketTransport.cpp")

list(FILTER UI_LVGL_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

# Core sources
file(GLOB_RECURSE CORE_SOURCES "${CORE_SRC}/*.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/config/platform-teensy/.*")

# SDL sources
set(SDL_SOURCES
    ${SDL_DIR}/SdlRunner.cpp
    ${SDL_DIR}/main-wasm.cpp
    ${SDL_DIR}/HwSimulator.cpp
)

# ==============================================================================
# Main executable
# ==============================================================================
add_executable(midi_studio_wasm
    ${SDL_SOURCES}
    ${CORE_SOURCES}
    ${FRAMEWORK_SOURCES}
    ${UI_LVGL_SOURCES}
    ${HAL_SDL_SOURCES}
    ${HAL_NET_WASM_SOURCES}
    ${HAL_MIDI_SOURCES}
    ${UI_LVGL_COMPONENTS_SOURCES}
)

target_include_directories(midi_studio_wasm PRIVATE
    ${SDL_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CONFIG_DIR}
    ${CORE_SRC}
    ${FRAMEWORK_DIR}/src
    ${UI_LVGL_DIR}/src
    ${HAL_SDL_DIR}/src
    ${HAL_NET_DIR}/src
    ${HAL_MIDI_DIR}/src
    ${UI_LVGL_COMPONENTS_DIR}/include
)

target_link_libraries(midi_studio_wasm PRIVATE lvgl libremidi)

target_compile_definitions(midi_studio_wasm PRIVATE
    OC_DESKTOP=1
    OC_LOG=1
    __EMSCRIPTEN__=1
)

# ==============================================================================
# Emscripten linker flags
# ==============================================================================
set(EMSCRIPTEN_LINK_FLAGS
    "-s USE_SDL=2"
    "-lwebsocket.js"
    "-s INITIAL_MEMORY=67108864"
    "-s ALLOW_MEMORY_GROWTH=1"
    "-s MAXIMUM_MEMORY=268435456"
    "-s ALLOW_TABLE_GROWTH=1"
    "-s SINGLE_FILE=0"
    "-s MODULARIZE=0"
    "-s EXPORT_ES6=0"
    "-s INVOKE_RUN=0"
    "-s EXIT_RUNTIME=0"
    "-s ASYNCIFY=1"
    "-s ASYNCIFY_STACK_SIZE=65536"
    "-s EXPORTED_RUNTIME_METHODS=['callMain','HEAPU8','HEAPU16','HEAPU32','HEAP8','HEAP16','HEAP32','HEAPF32','HEAPF64','UTF8ToString','stringToUTF8','lengthBytesUTF8']"
    "-s EXPORTED_FUNCTIONS=['_main','_malloc','_free','_libremidi_devices_poll','_libremidi_devices_input']"
    "-s EXPORT_ALL=1"
    "--shell-file ${CMAKE_SOURCE_DIR}/shell.html"
)

string(REPLACE ";" " " EMSCRIPTEN_LINK_FLAGS_STR "${EMSCRIPTEN_LINK_FLAGS}")

set_target_properties(midi_studio_wasm PROPERTIES
    SUFFIX ".html"
    LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS_STR}"
    RUNTIME_OUTPUT_DIRECTORY "${SDL_DIR}/bin/wasm"
)

message(STATUS "Output: ${SDL_DIR}/bin/wasm/midi_studio_wasm.html")
