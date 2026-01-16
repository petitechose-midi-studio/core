#!/bin/bash
cd "$(dirname "$0")"
ninja -C build/native && exec bin/native/midi_studio_sdl.exe
