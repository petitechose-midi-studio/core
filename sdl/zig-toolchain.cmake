# =============================================================================
# Zig Toolchain for Windows Native Build
# Requires: -DTOOLS_DIR=<path/to/tools>
# =============================================================================

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Ensure TOOLS_DIR is passed to try_compile() calls
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TOOLS_DIR)

# Tools directory - passed via -DTOOLS_DIR=...
if(NOT DEFINED TOOLS_DIR)
    message(FATAL_ERROR "TOOLS_DIR not defined. Pass -DTOOLS_DIR=<path>")
endif()

# Normalize path (convert backslashes to forward slashes)
file(TO_CMAKE_PATH "${TOOLS_DIR}" TOOLS_DIR)

# Derive paths from TOOLS_DIR
set(ZIG_ROOT "${TOOLS_DIR}/zig")
set(ZIG_WRAPPER_DIR "${TOOLS_DIR}/bin")
set(SDL2_ROOT "${TOOLS_DIR}/sdl2")

if(NOT EXISTS "${ZIG_ROOT}/zig.exe")
    message(FATAL_ERROR "Zig not found: ${ZIG_ROOT}/zig.exe\nRun: ms tools sync")
endif()

message(STATUS "Using Zig from: ${ZIG_ROOT}")

# =============================================================================
# Compiler Setup
# =============================================================================
set(CMAKE_C_COMPILER "${ZIG_WRAPPER_DIR}/zig-cc.cmd")
set(CMAKE_CXX_COMPILER "${ZIG_WRAPPER_DIR}/zig-cxx.cmd")
set(CMAKE_AR "${ZIG_WRAPPER_DIR}/zig-ar.cmd")
set(CMAKE_C_COMPILER_AR "${ZIG_WRAPPER_DIR}/zig-ar.cmd")
set(CMAKE_CXX_COMPILER_AR "${ZIG_WRAPPER_DIR}/zig-ar.cmd")
set(CMAKE_RANLIB "${ZIG_WRAPPER_DIR}/zig-ranlib.cmd")
set(CMAKE_C_COMPILER_RANLIB "${ZIG_WRAPPER_DIR}/zig-ranlib.cmd")
set(CMAKE_CXX_COMPILER_RANLIB "${ZIG_WRAPPER_DIR}/zig-ranlib.cmd")

# Use Zig's Windows resource compiler instead of relying on a host-provided
# windres/rc executable. FORCE also replaces stale machine-specific cache values.
set(ZIG_RC_COMPILER "${ZIG_WRAPPER_DIR}/zig-rc.cmd")
if(NOT EXISTS "${ZIG_RC_COMPILER}")
    message(FATAL_ERROR
        "Zig RC wrapper not found: ${ZIG_RC_COMPILER}\nRun: ms sync --tools")
endif()
set(CMAKE_RC_COMPILER "${ZIG_RC_COMPILER}" CACHE FILEPATH
    "Windows resource compiler" FORCE)

# =============================================================================
# Skip Compiler Checks
# =============================================================================
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)
set(CMAKE_C_ABI_COMPILED TRUE)
set(CMAKE_CXX_ABI_COMPILED TRUE)
set(CMAKE_C_COMPILER_ID "Clang")
set(CMAKE_CXX_COMPILER_ID "Clang")
set(CMAKE_C_COMPILER_VERSION "18.0")
set(CMAKE_CXX_COMPILER_VERSION "18.0")

# =============================================================================
# Compiler Features (required for target_compile_features)
# =============================================================================
set(CMAKE_C_COMPILE_FEATURES c_std_11 c_std_17 c_std_23)
set(CMAKE_CXX_COMPILE_FEATURES cxx_std_11 cxx_std_14 cxx_std_17 cxx_std_20 cxx_std_23)

# =============================================================================
# Performance Flags
# =============================================================================
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_DEBUG "-gdwarf -O0" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-gdwarf -O0" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "" FORCE)
