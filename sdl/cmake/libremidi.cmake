# ==============================================================================
# libremidi configuration
# ==============================================================================

include(FetchContent)

FetchContent_Declare(libremidi
    GIT_REPOSITORY https://github.com/celtera/libremidi.git
    GIT_TAG v5.4.1
    GIT_SHALLOW TRUE
)

# Common options
set(LIBREMIDI_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(LIBREMIDI_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBREMIDI_TESTS OFF CACHE BOOL "" FORCE)

# Platform-specific backends
if(EMSCRIPTEN)
    # WASM: disable all native backends, use Emscripten WebMIDI
    foreach(_backend ALSA JACK PIPEWIRE UDEV COREMIDI WINMM WINMIDI WINUWP)
        set(LIBREMIDI_NO_${_backend} ON CACHE BOOL "" FORCE)
    endforeach()
elseif(WIN32)
    # Windows: use WinMM only
    set(LIBREMIDI_NO_WINMIDI ON CACHE BOOL "" FORCE)
    set(LIBREMIDI_NO_WINUWP ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(libremidi)

# Note (WASM): The Emscripten WebMIDI backend requires exporting
# `_libremidi_devices_poll` and `_libremidi_devices_input` from the final app.
# We do that at the app link level in `midi-studio/core/sdl/CMakeLists.txt`.
