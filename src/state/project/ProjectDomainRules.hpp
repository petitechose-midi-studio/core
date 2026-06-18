#pragma once

#include <cstdint>

namespace core::state::project {

inline constexpr float PROJECT_TEMPO_MIN_BPM = 20.0f;
inline constexpr float PROJECT_TEMPO_MAX_BPM = 300.0f;
inline constexpr float PROJECT_TEMPO_DEFAULT_BPM = 120.0f;
inline constexpr int PROJECT_TEMPO_RANGE_STEPS =
    static_cast<int>(PROJECT_TEMPO_MAX_BPM - PROJECT_TEMPO_MIN_BPM) + 1;

inline constexpr uint8_t PROJECT_SWING_DEFAULT_PERCENT = 0;
inline constexpr uint8_t PROJECT_SWING_MAX_PERCENT = 75;
inline constexpr uint8_t PROJECT_SWING_STEPS = PROJECT_SWING_MAX_PERCENT + 1U;

inline constexpr uint8_t PROJECT_RUN_MODE_DEFAULT = 0;
inline constexpr uint8_t PROJECT_RUN_MODE_COUNT = 3;

enum class ProjectStepPasteMode : uint8_t {
    EXTEND = 0,
    PAGE = 1,
    WRAP = 2,
};

inline constexpr uint8_t PROJECT_STEP_PASTE_MODE_COUNT = 3;
inline constexpr ProjectStepPasteMode PROJECT_STEP_PASTE_MODE_DEFAULT =
    ProjectStepPasteMode::EXTEND;

inline constexpr uint8_t PROJECT_MIDI_CHANNEL_COUNT = 16;

constexpr float sanitizeProjectTempoBpm(float tempoBpm) {
    if (tempoBpm < PROJECT_TEMPO_MIN_BPM) return PROJECT_TEMPO_MIN_BPM;
    if (tempoBpm > PROJECT_TEMPO_MAX_BPM) return PROJECT_TEMPO_MAX_BPM;
    return tempoBpm;
}

constexpr int roundedProjectTempoBpm(float tempoBpm) {
    if (tempoBpm < 0.0f) return 0;
    return static_cast<int>(tempoBpm + 0.5f);
}

constexpr uint16_t projectTempoToCentiBpm(float tempoBpm) {
    const float clamped = sanitizeProjectTempoBpm(tempoBpm);
    return static_cast<uint16_t>(clamped * 100.0f + 0.5f);
}

constexpr float projectCentiBpmToTempo(uint16_t centiBpm) {
    return sanitizeProjectTempoBpm(static_cast<float>(centiBpm) / 100.0f);
}

constexpr uint8_t sanitizeProjectSwingPercent(uint8_t swingPercent) {
    return swingPercent > PROJECT_SWING_MAX_PERCENT
        ? PROJECT_SWING_MAX_PERCENT
        : swingPercent;
}

constexpr uint8_t sanitizeProjectRunMode(uint8_t runMode) {
    return static_cast<uint8_t>(runMode % PROJECT_RUN_MODE_COUNT);
}

constexpr ProjectStepPasteMode sanitizeProjectStepPasteMode(ProjectStepPasteMode mode) {
    const auto raw = static_cast<uint8_t>(mode);
    return raw < PROJECT_STEP_PASTE_MODE_COUNT
        ? mode
        : PROJECT_STEP_PASTE_MODE_DEFAULT;
}

constexpr ProjectStepPasteMode sanitizeProjectStepPasteMode(uint8_t mode) {
    return mode < PROJECT_STEP_PASTE_MODE_COUNT
        ? static_cast<ProjectStepPasteMode>(mode)
        : PROJECT_STEP_PASTE_MODE_DEFAULT;
}

constexpr uint8_t sanitizeProjectMidiChannel(uint8_t channel) {
    return static_cast<uint8_t>(channel % PROJECT_MIDI_CHANNEL_COUNT);
}

}  // namespace core::state::project
