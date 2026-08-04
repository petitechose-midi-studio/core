#include "state/project/ProjectState.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectSlug.hpp"

namespace core::state::project {

namespace {

template <size_t Size>
FLASHMEM void assignText(std::array<char, Size>& target, const char* source) {
    target.fill('\0');
    if (!source || Size == 0) return;
    std::strncpy(target.data(), source, Size - 1);
    target[Size - 1] = '\0';
}

}  // namespace

FLASHMEM void ProjectMetadata::reset() {
    assignText(id, "");
    assignText(name, DEFAULT_UNSAVED_PROJECT_SLUG);
    modifiedCounter = 0;
    dirty = false;
    hasSavedIdentity = false;
}

FLASHMEM void ProjectTransportState::reset() {
    tempoBpm = DEFAULT_TEMPO_BPM;
    swingPercent = DEFAULT_SWING_PERCENT;
    runMode = DEFAULT_RUN_MODE;
}

FLASHMEM void ProjectMusicalContext::reset() {
    scale = core::state::sequencer::defaultProjectScaleSettings();
    scale.clamp();
    patternsInheritScale = true;
    clipsInheritScale = true;
}

FLASHMEM void ProjectEditingState::reset() {
    stepPasteMode = PROJECT_STEP_PASTE_MODE_DEFAULT;
    ccLaneDefaultControllers = PROJECT_CC_LANE_DEFAULT_CONTROLLERS;
}

FLASHMEM ProjectState::ProjectState() {
    reset();
}

FLASHMEM void ProjectState::reset() {
    metadata.reset();
    transport.reset();
    musical.reset();
    editing.reset();
}

}  // namespace core::state::project
