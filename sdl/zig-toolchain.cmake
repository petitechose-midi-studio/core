# =============================================================================
# Zig Toolchain for Windows Native Build (Max Performance)
# Portable: Zig is in project's tools/ folder
# =============================================================================

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Zig wrappers location
get_filename_component(TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}/tools" ABSOLUTE)

if(NOT EXISTS "${TOOLS_DIR}/zig/zig.exe")
    message(FATAL_ERROR "Zig not found at: ${TOOLS_DIR}/zig/zig.exe\nRun ./build.sh native to auto-download.")
endif()

message(STATUS "Using Zig from: ${TOOLS_DIR}")

# =============================================================================
# Compiler Setup (using wrapper scripts)
# =============================================================================
set(CMAKE_C_COMPILER "${TOOLS_DIR}/zig-cc.cmd")
set(CMAKE_CXX_COMPILER "${TOOLS_DIR}/zig-cxx.cmd")
set(CMAKE_AR "${TOOLS_DIR}/zig-ar.cmd")
set(CMAKE_C_COMPILER_AR "${TOOLS_DIR}/zig-ar.cmd")
set(CMAKE_CXX_COMPILER_AR "${TOOLS_DIR}/zig-ar.cmd")
set(CMAKE_RANLIB "true")

# =============================================================================
# Skip Compiler Checks (Zig handles everything)
# =============================================================================
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)
set(CMAKE_C_ABI_COMPILED TRUE)
set(CMAKE_CXX_ABI_COMPILED TRUE)

# Force compiler ID to avoid detection
set(CMAKE_C_COMPILER_ID "Clang")
set(CMAKE_CXX_COMPILER_ID "Clang")
set(CMAKE_C_COMPILER_VERSION "18.0")
set(CMAKE_CXX_COMPILER_VERSION "18.0")

# =============================================================================
# Performance Flags
# =============================================================================
# Zig uses its own cache (~/.cache/zig) - very fast incremental builds
# LLD linker is built-in - much faster than GNU ld

set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_DEBUG "-gdwarf -O0" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-gdwarf -O0" CACHE STRING "" FORCE)

# Use maximum parallelism for linking
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "" FORCE)
