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

if(WIN32)
    # Temporary local override for libremidi WinMM realtime parsing.
    # Keep this in midi-studio only (no fork / no vendored lib changes).
    set(_libremidi_winmm_header "${libremidi_SOURCE_DIR}/include/libremidi/backends/winmm/midi_in.hpp")
    if(NOT EXISTS "${_libremidi_winmm_header}")
        message(FATAL_ERROR "libremidi WinMM header not found: ${_libremidi_winmm_header}")
    endif()

    file(READ "${_libremidi_winmm_header}" _libremidi_winmm_src)
    if(NOT _libremidi_winmm_src MATCHES "status == 0xFA")
        string(REPLACE
            "else if (status == 0xF8)\n      return 1;\n    else if (status == 0xFE)\n      return 1;"
            "else if (status == 0xF8)\n      return 1;\n    else if (status == 0xFA)\n      return 1;\n    else if (status == 0xFB)\n      return 1;\n    else if (status == 0xFC)\n      return 1;\n    else if (status == 0xFE)\n      return 1;"
            _libremidi_winmm_patched
            "${_libremidi_winmm_src}"
        )

        if(_libremidi_winmm_patched STREQUAL _libremidi_winmm_src)
            message(FATAL_ERROR "Unable to patch libremidi WinMM realtime parsing (upstream layout changed).")
        endif()

        file(WRITE "${_libremidi_winmm_header}" "${_libremidi_winmm_patched}")
        message(STATUS "Patched libremidi WinMM realtime parsing (FA/FB/FC)")
    endif()
endif()

# Note (WASM): The Emscripten WebMIDI backend requires exporting
# `_libremidi_devices_poll` and `_libremidi_devices_input` from the final app.
# We do that at the app link level in `midi-studio/core/sdl/CMakeLists.txt`.
