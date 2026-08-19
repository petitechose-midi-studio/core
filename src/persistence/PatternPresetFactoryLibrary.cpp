#include "persistence/PatternPresetFactoryLibrary.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::persistence {
namespace {

namespace seq = core::state::sequencer;
namespace codec = core::persistence::sequencer_pattern_preset_codec;

struct FactoryDefinition {
    char id[40]{};
    char name[32]{};
    seq::SequencerTrackKind kind = seq::SequencerTrackKind::INSTRUMENT;
    uint8_t length = 16U;
    uint8_t stepsPerBeat = 4U;
    uint16_t instrumentMask = 0U;
    std::array<uint8_t, 16> notes{};
    std::array<uint16_t, seq::DRUM_DEFAULT_LANE_COUNT> drumMasks{};
    std::array<uint8_t, seq::DRUM_DEFAULT_LANE_COUNT> drumLaneLengths{};
};

const FactoryDefinition FACTORY_PRESETS[] PROGMEM = {
    {
        "factory-drum-breakbeat",
        "Broken beat",
        seq::SequencerTrackKind::DRUM,
        16U,
        4U,
        0U,
        {},
        {0x4489U, 0x1010U, 0xAAAAU, 0x4000U, 0U, 0U, 0U, 0U},
        {},
    },
    {
        "factory-drum-four-floor",
        "Four on the floor",
        seq::SequencerTrackKind::DRUM,
        16U,
        4U,
        0U,
        {},
        {0x1111U, 0x1010U, 0x5555U, 0x8000U, 0U, 0U, 0U, 0U},
        {},
    },
    {
        "factory-drum-half-time",
        "Half time",
        seq::SequencerTrackKind::DRUM,
        16U,
        4U,
        0U,
        {},
        {0x0441U, 0x0100U, 0x5555U, 0x8000U, 0U, 0U, 0U, 0U},
        {},
    },
    {
        "factory-drum-polymeter",
        "Small polymeter",
        seq::SequencerTrackKind::DRUM,
        16U,
        4U,
        0U,
        {},
        {0x0011U, 0x0208U, 0x0015U, 0U, 0U, 0U, 0U, 0U},
        {7U, 11U, 5U, 0U, 0U, 0U, 0U, 0U},
    },
    {
        "factory-instrument-bass-offbeat",
        "Bass offbeat",
        seq::SequencerTrackKind::INSTRUMENT,
        16U,
        4U,
        0x4444U,
        {36U, 36U, 36U, 36U, 36U, 36U, 39U, 39U,
         39U, 39U, 34U, 34U, 34U, 34U, 36U, 36U},
        {},
        {},
    },
    {
        "factory-instrument-euclid-five",
        "Euclid five",
        seq::SequencerTrackKind::INSTRUMENT,
        16U,
        4U,
        0x1249U,
        {60U, 60U, 60U, 67U, 67U, 67U, 63U, 63U,
         63U, 70U, 70U, 70U, 65U, 65U, 65U, 60U},
        {},
        {},
    },
    {
        "factory-instrument-pulse-eighths",
        "Eighth-note pulse",
        seq::SequencerTrackKind::INSTRUMENT,
        16U,
        4U,
        0x5555U,
        {60U, 60U, 60U, 60U, 67U, 67U, 67U, 67U,
         60U, 60U, 60U, 60U, 65U, 65U, 67U, 67U},
        {},
        {},
    },
    {
        "factory-instrument-rising",
        "Rising sequence",
        seq::SequencerTrackKind::INSTRUMENT,
        16U,
        4U,
        0xFFFFU,
        {60U, 62U, 63U, 65U, 67U, 69U, 70U, 72U,
         72U, 70U, 69U, 67U, 65U, 63U, 62U, 60U},
        {},
        {},
    },
};

FLASHMEM const FactoryDefinition* findDefinition(const char* presetId) {
    if (presetId == nullptr) return nullptr;
    for (const auto& definition : FACTORY_PRESETS) {
        if (std::strcmp(definition.id, presetId) == 0) return &definition;
    }
    return nullptr;
}

FLASHMEM void authorInstrument(
    const FactoryDefinition& definition,
    seq::SequencerPatternState& pattern
) {
    pattern.reset();
    pattern.setContentLength(definition.length);
    pattern.stepsPerBeat.set(definition.stepsPerBeat);
    auto enabled = pattern.enabledMask.get();
    for (uint8_t step = 0U; step < definition.length; ++step) {
        const bool active =
            (definition.instrumentMask & (UINT16_C(1) << step)) != 0U;
        enabled.setBit(step, active);
        if (!active) continue;
        const uint8_t velocity = step % 4U == 0U ? 112U : 92U;
        pattern.setStepDataAt(
            step,
            definition.notes[step],
            velocity,
            82U
        );
    }
    pattern.enabledMask.set(enabled);
}

FLASHMEM void authorDrum(
    const FactoryDefinition& definition,
    seq::SequencerPatternState& pattern,
    seq::DrumTrackState& drum
) {
    pattern.reset();
    pattern.setContentLength(definition.length);
    pattern.stepsPerBeat.set(definition.stepsPerBeat);
    drum.reset(seq::DrumKitPreset::GENERAL_MIDI);
    drum.pattern.setDefaults(definition.length, definition.stepsPerBeat);
    for (uint8_t lane = 0U; lane < seq::DRUM_DEFAULT_LANE_COUNT; ++lane) {
        const uint8_t laneLength = definition.drumLaneLengths[lane];
        if (laneLength != 0U) {
            drum.pattern.setLaneTimingCustom(
                lane,
                laneLength,
                definition.stepsPerBeat
            );
        }
        for (uint8_t step = 0U; step < definition.length; ++step) {
            if ((definition.drumMasks[lane] &
                 (UINT16_C(1) << step)) == 0U) {
                continue;
            }
            drum.pattern.setStepEnabled(lane, step, true);
            const uint8_t base = lane == 0U
                ? 112U
                : (lane == 1U ? 106U : 78U);
            drum.pattern.setStepVelocity(
                lane,
                step,
                static_cast<uint8_t>(
                    step % 4U == 0U
                        ? std::min<unsigned>(127U, base + 10U)
                        : base
                )
            );
        }
    }
}

}  // namespace

FLASHMEM uint8_t PatternPresetFactoryLibrary::count(
    seq::SequencerTrackKind trackKind
) {
    uint8_t result = 0U;
    for (const auto& definition : FACTORY_PRESETS) {
        if (definition.kind == trackKind) ++result;
    }
    return result;
}

FLASHMEM bool PatternPresetFactoryLibrary::descriptorAt(
    seq::SequencerTrackKind trackKind,
    uint8_t index,
    PatternPresetFactoryDescriptor& out
) {
    for (const auto& definition : FACTORY_PRESETS) {
        if (definition.kind != trackKind) continue;
        if (index-- != 0U) continue;
        out = {definition.id, definition.name, definition.kind};
        return true;
    }
    out = {};
    return false;
}

FLASHMEM bool PatternPresetFactoryLibrary::describe(
    const char* presetId,
    PatternPresetFactoryDescriptor& out
) {
    const auto* definition = findDefinition(presetId);
    if (definition == nullptr) {
        out = {};
        return false;
    }
    out = {definition->id, definition->name, definition->kind};
    return true;
}

FLASHMEM bool PatternPresetFactoryLibrary::contains(const char* presetId) {
    return findDefinition(presetId) != nullptr;
}

FLASHMEM codec::EncodeResult PatternPresetFactoryLibrary::encode(
    const char* presetId,
    seq::SequencerPatternState& patternScratch,
    seq::DrumTrackState* drumScratch,
    seq::SequencerPatternPresetMetadata& metadataOut,
    uint8_t* out,
    uint16_t capacity
) {
    const auto* definition = findDefinition(presetId);
    if (definition == nullptr ||
        (definition->kind == seq::SequencerTrackKind::DRUM &&
         drumScratch == nullptr) ||
        !seq::setSequencerPatternPresetMetadata(
            metadataOut,
            definition->kind,
            definition->id,
            definition->name
        )) {
        return {
            .status = seq::SequencerPatternPresetStatus::INVALID_ARGUMENT,
        };
    }

    if (definition->kind == seq::SequencerTrackKind::DRUM) {
        authorDrum(*definition, patternScratch, *drumScratch);
    } else {
        authorInstrument(*definition, patternScratch);
    }
    return codec::encode(
        metadataOut,
        patternScratch,
        definition->kind == seq::SequencerTrackKind::DRUM
            ? drumScratch
            : nullptr,
        out,
        capacity
    );
}

}  // namespace core::persistence
