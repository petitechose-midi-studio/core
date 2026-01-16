cmake_minimum_required(VERSION 3.16)
project(midi_studio_sdl C CXX)

# ==============================================================================
# Unity Build désactivé (conflits de namespace alias dans le code)
# ==============================================================================
set(CMAKE_UNITY_BUILD OFF)

# ==============================================================================
# C/C++ Standards
# ==============================================================================
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release (faster builds & runtime)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Generate compile_commands.json for IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Debug symbols for GDB
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0")
endif()

# ==============================================================================
# Performance: Precompiled Headers
# ==============================================================================
set(USE_PCH ON CACHE BOOL "Use precompiled headers")

# ==============================================================================
# Cross-compilation: Linux target needs special handling
# This must be set BEFORE any add_subdirectory() calls
# ==============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(_GNU_SOURCE)
    # Zig is stricter than native GCC - allow implicit function declarations
    # (LVGL has some missing #include <string.h> for memset)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error=implicit-function-declaration")
    message(STATUS "Linux target: added _GNU_SOURCE and relaxed implicit-function-declaration")
    
    # Cross-compile from Windows: setup pkg-config for sysroot
    if(CMAKE_HOST_WIN32)
        set(LINUX_SYSROOT "${CMAKE_SOURCE_DIR}/tools/linux-sysroot")
        if(EXISTS "${LINUX_SYSROOT}/usr/include")
            message(STATUS "Configuring pkg-config for Linux sysroot")
            set(ENV{PKG_CONFIG_SYSROOT_DIR} "${LINUX_SYSROOT}")
            set(ENV{PKG_CONFIG_LIBDIR} "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/pkgconfig;${LINUX_SYSROOT}/usr/lib/pkgconfig;${LINUX_SYSROOT}/usr/share/pkgconfig")
            set(ENV{PKG_CONFIG_PATH} "")
            
            # Disable backends that require host tools or complex dependencies
            set(SDL_WAYLAND OFF CACHE BOOL "Disable Wayland (requires wayland-scanner)" FORCE)
            set(SDL_KMSDRM OFF CACHE BOOL "Disable KMSDRM (requires kernel headers)" FORCE)
            set(SDL_DBUS OFF CACHE BOOL "Disable DBus (requires libdbus-1-dev)" FORCE)
            set(SDL_PULSEAUDIO OFF CACHE BOOL "Disable PulseAudio (requires libpulse-dev)" FORCE)
            set(SDL_PIPEWIRE OFF CACHE BOOL "Disable PipeWire (requires libpipewire)" FORCE)
            
            # Force X11 support with STATIC linking (we have .a files, not .so)
            set(SDL_X11 ON CACHE BOOL "Enable X11" FORCE)
            set(SDL_X11_SHARED OFF CACHE BOOL "Link X11 statically" FORCE)
            
            # Tell CMake where to find X11 in our sysroot (static libs)
            set(X11_INCLUDE_DIR "${LINUX_SYSROOT}/usr/include" CACHE PATH "X11 include dir" FORCE)
            set(X11_X11_INCLUDE_PATH "${LINUX_SYSROOT}/usr/include" CACHE PATH "X11 include" FORCE)
            set(X11_FOUND TRUE CACHE BOOL "X11 found" FORCE)
            
            # Static libraries
            set(X11_LIBRARIES "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libX11.a" CACHE FILEPATH "X11 library" FORCE)
            set(X11_X11_LIB "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libX11.a" CACHE FILEPATH "X11 lib" FORCE)
            set(X11_Xext_LIB "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libXext.a" CACHE FILEPATH "Xext lib" FORCE)
            
            # X11 extensions (static)
            set(X11_Xcursor_INCLUDE_PATH "${LINUX_SYSROOT}/usr/include" CACHE PATH "" FORCE)
            set(X11_Xcursor_LIB "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libXcursor.a" CACHE FILEPATH "" FORCE)
            set(X11_Xfixes_INCLUDE_PATH "${LINUX_SYSROOT}/usr/include" CACHE PATH "" FORCE)
            set(X11_Xfixes_LIB "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libXfixes.a" CACHE FILEPATH "" FORCE)
            set(X11_Xi_INCLUDE_PATH "${LINUX_SYSROOT}/usr/include" CACHE PATH "" FORCE)
            set(X11_Xi_LIB "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libXi.a" CACHE FILEPATH "" FORCE)
            set(X11_Xrandr_INCLUDE_PATH "${LINUX_SYSROOT}/usr/include" CACHE PATH "" FORCE)
            set(X11_Xrandr_LIB "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libXrandr.a" CACHE FILEPATH "" FORCE)
            set(X11_Xss_INCLUDE_PATH "${LINUX_SYSROOT}/usr/include" CACHE PATH "" FORCE)
            set(X11_Xss_LIB "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libXss.a" CACHE FILEPATH "" FORCE)
            
            # xcb dependencies (needed by X11)
            set(X11_xcb_LIB "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libxcb.a" CACHE FILEPATH "" FORCE)
            set(X11_Xau_LIB "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libXau.a" CACHE FILEPATH "" FORCE)
            set(X11_Xdmcp_LIB "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libXdmcp.a" CACHE FILEPATH "" FORCE)
            set(X11_Xrender_LIB "${LINUX_SYSROOT}/usr/lib/x86_64-linux-gnu/libXrender.a" CACHE FILEPATH "" FORCE)
        endif()
    endif()
endif()

# ==============================================================================
# Project paths
# ==============================================================================
set(MIDI_STUDIO_ROOT "${CMAKE_SOURCE_DIR}/..")
set(OPEN_CONTROL_ROOT "${MIDI_STUDIO_ROOT}/../../open-control")

# midi-studio paths
set(CONFIG_DIR "${MIDI_STUDIO_ROOT}/src/config")
set(SDL_DIR "${CMAKE_SOURCE_DIR}")  # sdl/ directory (lv_conf.h, HwSimulator, etc.)
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
set(HAL_SDL_DIR "${OPEN_CONTROL_ROOT}/hal-sdl")
set(HAL_NET_DIR "${OPEN_CONTROL_ROOT}/hal-net")
set(HAL_MIDI_DIR "${OPEN_CONTROL_ROOT}/hal-midi")
set(UI_LVGL_COMPONENTS_DIR "${OPEN_CONTROL_ROOT}/ui-lvgl-components")

message(STATUS "LVGL: ${LVGL_DIR}")
message(STATUS "Framework: ${FRAMEWORK_DIR}")
message(STATUS "UI-LVGL: ${UI_LVGL_DIR}")
message(STATUS "HAL-SDL: ${HAL_SDL_DIR}")
message(STATUS "HAL-Net: ${HAL_NET_DIR}")
message(STATUS "HAL-MIDI: ${HAL_MIDI_DIR}")
message(STATUS "UI-LVGL-Components: ${UI_LVGL_COMPONENTS_DIR}")
message(STATUS "Config: ${CONFIG_DIR}")
message(STATUS "SDL Dir: ${SDL_DIR}")

# ==============================================================================
# SDL2 - Pre-built (fast) or FetchContent (fallback)
# ==============================================================================
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/deps")

if(DEFINED SDL2_ROOT AND EXISTS "${SDL2_ROOT}/include/SDL2/SDL.h")
    # ========== PRE-BUILT SDL2 (instant configuration) ==========
    message(STATUS "Using pre-built SDL2: ${SDL2_ROOT}")
    
    # Both paths needed: SDL2/SDL.h for some code, SDL.h for hal-sdl
    set(SDL2_INCLUDE_DIR "${SDL2_ROOT}/include;${SDL2_ROOT}/include/SDL2")
    set(SDL2_LIBRARY "${SDL2_ROOT}/lib/libSDL2.a")
    set(SDL2MAIN_LIBRARY "${SDL2_ROOT}/lib/libSDL2main.a")
    
    # Create imported target for SDL2
    add_library(SDL2-static STATIC IMPORTED GLOBAL)
    set_target_properties(SDL2-static PROPERTIES
        IMPORTED_LOCATION "${SDL2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
    )
    
    # Windows system libraries required by SDL2
    if(WIN32)
        set_target_properties(SDL2-static PROPERTIES
            INTERFACE_LINK_LIBRARIES "mingw32;winmm;imm32;ole32;oleaut32;version;uuid;advapi32;setupapi;shell32;dinput8;dxguid;dxerr8;user32;gdi32;hid;opengl32"
        )
    endif()
    
    set(SDL2_TARGET "SDL2-static")
    
else()
    # ========== FETCHCONTENT FALLBACK ==========
    message(STATUS "SDL2_ROOT not set, using FetchContent (slower)...")
    message(STATUS "  Tip: Run 'build.cmd setup' to download pre-built SDL2")
    
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.32.10
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    
    # Disable unnecessary SDL2 features
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
    set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
    set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
    set(SDL_POWER OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    set(SDL_LOCALE OFF CACHE BOOL "" FORCE)
    set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
    set(SDL_LIBC ON CACHE BOOL "" FORCE)
    
    if(WIN32)
        set(PKG_CONFIG_EXECUTABLE "" CACHE STRING "" FORCE)
        set(SDL_WASAPI OFF CACHE BOOL "" FORCE)
        set(SDL_DIRECTSOUND OFF CACHE BOOL "" FORCE)
    endif()
    
    FetchContent_MakeAvailable(SDL2)
    FetchContent_GetProperties(SDL2 SOURCE_DIR SDL2_SOURCE_DIR)
    set(SDL2_INCLUDE_DIR "${SDL2_SOURCE_DIR}/include")
    set(SDL2_TARGET "SDL2::SDL2-static")
endif()

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
# LVGL requires lv_conf.h - located in sdl/ directory
set(LV_BUILD_CONF_DIR "${SDL_DIR}" CACHE STRING "Path to lv_conf.h directory" FORCE)

set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl)

target_include_directories(lvgl PUBLIC
    ${SDL_DIR}
    ${SDL2_INCLUDE_DIR}
)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

# ==============================================================================
# Open Control Framework sources
# ==============================================================================
file(GLOB_RECURSE FRAMEWORK_SOURCES "${FRAMEWORK_DIR}/src/*.cpp")
file(GLOB_RECURSE UI_LVGL_SOURCES "${UI_LVGL_DIR}/src/*.cpp")
file(GLOB_RECURSE HAL_SDL_SOURCES "${HAL_SDL_DIR}/src/*.cpp")
file(GLOB_RECURSE HAL_NET_SOURCES "${HAL_NET_DIR}/src/*.cpp")
file(GLOB_RECURSE HAL_MIDI_SOURCES "${HAL_MIDI_DIR}/src/*.cpp")
file(GLOB_RECURSE UI_LVGL_COMPONENTS_SOURCES "${UI_LVGL_COMPONENTS_DIR}/src/*.cpp")

# Exclude Teensy-specific main.cpp files from framework sources
list(FILTER UI_LVGL_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

# ==============================================================================
# midi-studio sources
# ==============================================================================
# SDL-specific sources (HwSimulator, etc.)
set(SDL_PLATFORM_SOURCES
    ${SDL_DIR}/HwSimulator.cpp
)

# Core business logic
file(GLOB_RECURSE CORE_SOURCES "${CORE_SRC}/*.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/config/platform-teensy/.*")

# ==============================================================================
# SDL entry point
# ==============================================================================
set(SDL_MAIN
    ${SDL_DIR}/SdlRunner.cpp
    ${SDL_DIR}/main-native.cpp
)

# ==============================================================================
# Main executable
# ==============================================================================
add_executable(midi_studio_sdl
    ${SDL_MAIN}
    ${SDL_PLATFORM_SOURCES}
    ${CORE_SOURCES}
    ${FRAMEWORK_SOURCES}
    ${UI_LVGL_SOURCES}
    ${HAL_SDL_SOURCES}
    ${HAL_NET_SOURCES}
    ${HAL_MIDI_SOURCES}
    ${UI_LVGL_COMPONENTS_SOURCES}
)

target_include_directories(midi_studio_sdl PRIVATE
    ${SDL_DIR}
    ${CONFIG_DIR}
    ${CORE_SRC}
    ${FRAMEWORK_DIR}/src
    ${UI_LVGL_DIR}/src
    ${HAL_SDL_DIR}/src
    ${HAL_NET_DIR}/src
    ${HAL_MIDI_DIR}/src
    ${UI_LVGL_COMPONENTS_DIR}/include
    ${SDL2_INCLUDE_DIR}
)

target_link_libraries(midi_studio_sdl PRIVATE
    lvgl
    ${SDL2_TARGET}
    libremidi
)

target_compile_definitions(midi_studio_sdl PRIVATE
    OC_DESKTOP=1
    OC_LOG=1
)

# ==============================================================================
# Platform-specific (based on target, not host)
# ==============================================================================
# Detect target platform from CMAKE_SYSTEM_NAME (set by toolchain for cross-compile)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(TARGET_WINDOWS TRUE)
    set(TARGET_LINUX FALSE)
    set(TARGET_MACOS FALSE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TARGET_WINDOWS FALSE)
    set(TARGET_LINUX TRUE)
    set(TARGET_MACOS FALSE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(TARGET_WINDOWS FALSE)
    set(TARGET_LINUX FALSE)
    set(TARGET_MACOS TRUE)
endif()

message(STATUS "Target platform: ${CMAKE_SYSTEM_NAME}")

if(TARGET_WINDOWS)
    if(MSVC)
        target_link_options(midi_studio_sdl PRIVATE "/SUBSYSTEM:CONSOLE")
    else()
        target_link_options(midi_studio_sdl PRIVATE "-mconsole")
    endif()
    target_link_libraries(midi_studio_sdl PRIVATE winmm imm32 setupapi version dwmapi ws2_32)
endif()

if(TARGET_LINUX)
    target_link_libraries(midi_studio_sdl PRIVATE m pthread)
endif()

# ==============================================================================
# Output (platform-specific subdirectory)
# ==============================================================================
if(NOT DEFINED PLATFORM_ID)
    set(PLATFORM_ID "default")
endif()

set(BIN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${PLATFORM_ID}")

set_target_properties(midi_studio_sdl PROPERTIES
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
