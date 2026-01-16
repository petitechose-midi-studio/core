// Fix for libremidi WebMIDI backend
// libremidi expects _malloc and _free on Module object
// but modern Emscripten only exports them as globals
Module._malloc = _malloc;
Module._free = _free;
