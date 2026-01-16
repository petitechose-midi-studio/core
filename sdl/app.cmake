# =============================================================================
# MIDI Studio Core - App Configuration
# =============================================================================
# This file is included by CMakeLists.txt via -DAPP_PATH
# All paths are relative to CMAKE_CURRENT_LIST_DIR (this file's directory)

set(APP_ID "core")
set(APP_NAME "MIDI Studio")
set(APP_EXE_NAME "midi_studio_core")

# -----------------------------------------------------------------------------
# Source paths (relative to this file)
# -----------------------------------------------------------------------------
set(APP_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../src")
set(APP_MAIN_NATIVE "${CMAKE_CURRENT_LIST_DIR}/main-native.cpp")
set(APP_MAIN_WASM "${CMAKE_CURRENT_LIST_DIR}/main-wasm.cpp")

# -----------------------------------------------------------------------------
# Features
# -----------------------------------------------------------------------------
set(APP_HAS_CORE_STATE ON)      # Uses core::state::CoreState
set(APP_USE_MIDI ON)            # MIDI transport
set(APP_USE_UDP OFF)            # UDP transport for oc-bridge
set(APP_USE_WEBSOCKET OFF)      # WebSocket for WASM

# -----------------------------------------------------------------------------
# Additional include directories (relative to this file)
# -----------------------------------------------------------------------------
set(APP_EXTRA_INCLUDES "")

# -----------------------------------------------------------------------------
# Additional sources (glob patterns relative to APP_SRC_DIR)
# -----------------------------------------------------------------------------
set(APP_EXTRA_SOURCES "")
