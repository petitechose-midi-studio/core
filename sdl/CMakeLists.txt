cmake_minimum_required(VERSION 3.16)
project(midi_studio C CXX)

# ==============================================================================
# Platform Detection
# ==============================================================================
if(EMSCRIPTEN)
    set(BUILD_WASM TRUE)
    set(BUILD_NATIVE FALSE)
    message(STATUS "Target: WebAssembly")
else()
    set(BUILD_WASM FALSE)
    set(BUILD_NATIVE TRUE)
    message(STATUS "Target: Native (${CMAKE_SYSTEM_NAME})")
endif()

# ==============================================================================
# Build Settings
# ==============================================================================
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_UNITY_BUILD OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(BUILD_WASM)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s USE_SDL=2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_SDL=2")
endif()

# ==============================================================================
# Paths
# ==============================================================================
set(SDL_DIR "${CMAKE_SOURCE_DIR}")
set(MIDI_STUDIO_ROOT "${SDL_DIR}/..")
set(OPEN_CONTROL_ROOT "${MIDI_STUDIO_ROOT}/../../open-control")
set(CONFIG_DIR "${MIDI_STUDIO_ROOT}/src/config")
set(CORE_SRC "${MIDI_STUDIO_ROOT}/src")

# App config (core or plugin)
if(NOT DEFINED APP_PATH)
    set(APP_PATH "${SDL_DIR}")
endif()
include("${APP_PATH}/app.cmake")
message(STATUS "App: ${APP_ID}")

# Output directory (can be overridden by -DBIN_OUTPUT_DIR=...)
if(NOT DEFINED BIN_OUTPUT_DIR)
    set(BIN_OUTPUT_DIR "${MIDI_STUDIO_ROOT}/bin")
endif()

# PlatformIO deps
if(EXISTS "${MIDI_STUDIO_ROOT}/.pio/libdeps/dev")
    set(PIO_LIBDEPS "${MIDI_STUDIO_ROOT}/.pio/libdeps/dev")
else()
    set(PIO_LIBDEPS "${MIDI_STUDIO_ROOT}/.pio/libdeps/release")
endif()

# Resolve symlinked PIO dependency
function(pio_dep NAME OUT)
    if(EXISTS "${PIO_LIBDEPS}/${NAME}")
        set(${OUT} "${PIO_LIBDEPS}/${NAME}" PARENT_SCOPE)
    elseif(EXISTS "${PIO_LIBDEPS}/${NAME}.pio-link")
        file(READ "${PIO_LIBDEPS}/${NAME}.pio-link" LINK)
        string(REGEX MATCH "symlink://([^\"]+)" _ "${LINK}")
        set(${OUT} "${MIDI_STUDIO_ROOT}/${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
endfunction()

pio_dep(lvgl LVGL_DIR)

# Open Control paths
set(OC_FRAMEWORK "${OPEN_CONTROL_ROOT}/framework/src")
set(OC_UI_LVGL "${OPEN_CONTROL_ROOT}/ui-lvgl/src")
set(OC_UI_COMPONENTS "${OPEN_CONTROL_ROOT}/ui-lvgl-components")
set(OC_HAL_SDL "${OPEN_CONTROL_ROOT}/hal-sdl/src")
set(OC_HAL_NET "${OPEN_CONTROL_ROOT}/hal-net/src")
set(OC_HAL_MIDI "${OPEN_CONTROL_ROOT}/hal-midi/src")

# ==============================================================================
# Dependencies
# ==============================================================================
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/libremidi.cmake)

# SDL2 (native only - WASM uses Emscripten's SDL)
if(BUILD_NATIVE)
    if(WIN32 AND DEFINED SDL2_ROOT)
        # Windows: use bundled SDL2 from tools/windows/
        set(SDL2_INCLUDE "${SDL2_ROOT}/include;${SDL2_ROOT}/include/SDL2")
        add_library(SDL2-static STATIC IMPORTED GLOBAL)
        set_target_properties(SDL2-static PROPERTIES
            IMPORTED_LOCATION "${SDL2_ROOT}/lib/libSDL2.a"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE}")
        set_target_properties(SDL2-static PROPERTIES
            INTERFACE_LINK_LIBRARIES "mingw32;winmm;imm32;ole32;oleaut32;version;uuid;advapi32;setupapi;shell32;dinput8;dxguid;dxerr8;user32;gdi32;hid;opengl32")
        set(SDL2_FOUND TRUE)
        set(SDL2_LIBRARIES SDL2-static)
    elseif(UNIX)
        # Linux/macOS: use system SDL2 (via package manager)
        find_package(SDL2 REQUIRED)
        if(NOT SDL2_FOUND)
            message(FATAL_ERROR "SDL2 not found. Install it with:\n"
                "  Fedora/RHEL:   sudo dnf install SDL2-devel\n"
                "  Ubuntu/Debian: sudo apt install libsdl2-dev\n"
                "  Arch:          sudo pacman -S sdl2\n"
                "  macOS:         brew install sdl2")
        endif()
        set(SDL2_INCLUDE ${SDL2_INCLUDE_DIRS})
    endif()
endif()

# LVGL
if(BUILD_WASM)
    set(LV_CONF_DIR "${SDL_DIR}/wasm")
else()
    set(LV_CONF_DIR "${SDL_DIR}")
endif()
set(LV_BUILD_CONF_DIR "${LV_CONF_DIR}" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl)
target_include_directories(lvgl PUBLIC ${LV_CONF_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
if(BUILD_NATIVE AND DEFINED SDL2_INCLUDE)
    target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE})
endif()

# ==============================================================================
# Sources
# ==============================================================================
file(GLOB_RECURSE SRC_FRAMEWORK "${OC_FRAMEWORK}/*.cpp")
file(GLOB_RECURSE SRC_UI_LVGL "${OC_UI_LVGL}/*.cpp")
file(GLOB_RECURSE SRC_UI_COMPONENTS "${OC_UI_COMPONENTS}/src/*.cpp")
file(GLOB_RECURSE SRC_HAL_SDL "${OC_HAL_SDL}/*.cpp")
file(GLOB_RECURSE SRC_HAL_MIDI "${OC_HAL_MIDI}/*.cpp")

# HAL Net: WASM only uses WebSocket
if(BUILD_WASM)
    set(SRC_HAL_NET "${OC_HAL_NET}/oc/hal/net/WebSocketTransport.cpp")
else()
    file(GLOB_RECURSE SRC_HAL_NET "${OC_HAL_NET}/*.cpp")
endif()

# App sources
file(GLOB_RECURSE SRC_APP "${APP_SRC_DIR}/*.cpp" "${APP_SRC_DIR}/*.c")
list(FILTER SRC_APP EXCLUDE REGEX ".*/main\\.cpp$")
list(FILTER SRC_APP EXCLUDE REGEX ".*/platform-teensy/.*")
list(FILTER SRC_APP EXCLUDE REGEX ".*/name\\.c$")
list(FILTER SRC_UI_LVGL EXCLUDE REGEX ".*/main\\.cpp$")
if(APP_EXTRA_SOURCES)
    list(APPEND SRC_APP ${APP_EXTRA_SOURCES})
endif()

# SDL layer
if(BUILD_WASM)
    set(SRC_SDL ${SDL_DIR}/SdlEnvironment.cpp ${SDL_DIR}/HwSimulator.cpp ${APP_MAIN_WASM})
else()
    set(SRC_SDL ${SDL_DIR}/SdlEnvironment.cpp ${SDL_DIR}/HwSimulator.cpp ${APP_MAIN_NATIVE})
endif()

# ==============================================================================
# Target
# ==============================================================================
add_executable(${APP_EXE_NAME}
    ${SRC_SDL} ${SRC_APP} ${SRC_FRAMEWORK} ${SRC_UI_LVGL}
    ${SRC_HAL_SDL} ${SRC_HAL_NET} ${SRC_HAL_MIDI} ${SRC_UI_COMPONENTS})

target_include_directories(${APP_EXE_NAME} PRIVATE
    ${SDL_DIR} ${LV_CONF_DIR} ${CONFIG_DIR} ${APP_SRC_DIR} ${CORE_SRC}
    ${OC_FRAMEWORK} ${OC_UI_LVGL} ${OC_HAL_SDL} ${OC_HAL_NET} ${OC_HAL_MIDI}
    ${OC_UI_COMPONENTS}/include)

target_compile_definitions(${APP_EXE_NAME} PRIVATE OC_DESKTOP=1 OC_LOG=1)
target_link_libraries(${APP_EXE_NAME} PRIVATE lvgl libremidi)

# ==============================================================================
# Platform-specific
# ==============================================================================
if(BUILD_WASM)
    target_compile_definitions(${APP_EXE_NAME} PRIVATE __EMSCRIPTEN__=1)
    set_target_properties(${APP_EXE_NAME} PROPERTIES
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}/${APP_ID}/wasm"
        LINK_FLAGS "-s USE_SDL=2 -lwebsocket.js \
            -s INITIAL_MEMORY=67108864 -s ALLOW_MEMORY_GROWTH=1 -s MAXIMUM_MEMORY=268435456 \
            -s ASYNCIFY=1 -s ASYNCIFY_STACK_SIZE=65536 -s INVOKE_RUN=0 \
            -s EXPORTED_RUNTIME_METHODS=['callMain','UTF8ToString','stringToUTF8'] \
            -s EXPORTED_FUNCTIONS=['_main','_malloc','_free'] \
            --shell-file ${SDL_DIR}/wasm/shell.html")

elseif(WIN32)
    target_include_directories(${APP_EXE_NAME} PRIVATE ${SDL2_INCLUDE})
    target_link_libraries(${APP_EXE_NAME} PRIVATE SDL2-static winmm imm32 setupapi version dwmapi ws2_32)
    target_link_options(${APP_EXE_NAME} PRIVATE "-mconsole")
    set_target_properties(${APP_EXE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}/${APP_ID}/native")

elseif(UNIX)
    target_include_directories(${APP_EXE_NAME} PRIVATE ${SDL2_INCLUDE})
    target_link_libraries(${APP_EXE_NAME} PRIVATE ${SDL2_LIBRARIES} m pthread)
    set_target_properties(${APP_EXE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BIN_OUTPUT_DIR}/${APP_ID}/native")
endif()
