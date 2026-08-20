#pragma once

#include <cstddef>
#include <cstdint>

namespace core::state::sequencer {

inline constexpr size_t SEQUENCER_PRESET_TECHNICAL_ID_SIZE = 55U;
inline constexpr size_t SEQUENCER_PRESET_SEMANTIC_NAME_SIZE = 32U;

bool validSequencerPresetTechnicalId(const char* technicalId);
bool validSequencerPresetSemanticName(const char* semanticName);
uint32_t sequencerPresetIdHash(const char* presetId);
void sequencerPresetSemanticName(
    const char* presetId,
    char* out,
    size_t outSize
);

}  // namespace core::state::sequencer
