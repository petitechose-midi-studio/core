#include "persistence/SequencerPersistenceCodec.hpp"

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceBinaryCodec.hpp"

namespace core::persistence::sequencer_codec {

namespace {

namespace binary = core::persistence::binary_codec;
namespace sequencer = core::state::sequencer;

oc::note::sequencer::StepBitMask128 lengthMask(uint8_t length) {
    return oc::note::sequencer::StepBitMask128::prefixMask(length);
}

FLASHMEM bool stepsPerBeatCanonical(uint8_t stepsPerBeat) {
    for (const uint8_t choice : sequencer::PATTERN_STEPS_PER_BEAT_CHOICES) {
        if (choice == stepsPerBeat) return true;
    }
    return false;
}

FLASHMEM bool variationRangesCanonical(
    const oc::note::sequencer::StepSequencerVariationRanges& ranges
) {
    using Ranges = oc::note::sequencer::StepSequencerVariationRanges;
    return ranges.pitchSemitones <= Ranges::MAX_PITCH_SEMITONES &&
           ranges.velocity <= Ranges::MAX_VELOCITY &&
           ranges.gatePercent <= Ranges::MAX_GATE_PERCENT &&
           ranges.nudge <= Ranges::MAX_NUDGE;
}

FLASHMEM bool scaleSettingsCanonical(
    const oc::note::sequencer::StepSequencerScaleSettings& settings
) {
    return settings.root < 12U &&
           static_cast<uint8_t>(settings.type) <= static_cast<uint8_t>(
               oc::note::sequencer::StepSequencerScaleType::WholeTone
           ) &&
           static_cast<uint8_t>(settings.mode) <= static_cast<uint8_t>(
               oc::note::sequencer::StepSequencerScaleConstraintMode::
                   ConstrainDown
           );
}

FLASHMEM bool stepPropertyCanonical(
    sequencer::StepProperty property
) {
    return static_cast<uint8_t>(property) <=
           static_cast<uint8_t>(sequencer::StepProperty::PROBABILITY);
}

FLASHMEM bool enabledMaskCanonical(
    const oc::note::sequencer::StepBitMask128& mask,
    uint8_t length
) {
    const auto available = lengthMask(length);
    return (mask & available) == mask;
}

FLASHMEM oc::note::sequencer::StepSequencerScaleSettings payloadScaleSettings(
    uint8_t root,
    uint8_t type,
    uint8_t mode
) {
    return {
        .root = root,
        .type = static_cast<oc::note::sequencer::StepSequencerScaleType>(type),
        .mode = static_cast<oc::note::sequencer::StepSequencerScaleConstraintMode>(mode),
    };
}

FLASHMEM bool trackSelectionCanonical(
    uint16_t enabledMask,
    uint8_t activeTrack
) {
    return enabledMask != 0U &&
           activeTrack < sequencer::SequencerTrackBankState::TRACK_COUNT &&
           (enabledMask & static_cast<uint16_t>(1U << activeTrack)) != 0U;
}

FLASHMEM bool patternHeaderCanonical(
    uint8_t length,
    uint8_t stepsPerBeat,
    uint8_t pitchEditMode,
    const oc::note::sequencer::StepSequencerVariationRanges& variationRanges,
    int8_t swingOffsetPercent,
    int8_t patternNudgePercent,
    uint8_t scalePolicy,
    const oc::note::sequencer::StepSequencerScaleSettings& scaleOverride,
    const oc::note::sequencer::StepBitMask128& enabledMask
) {
    return length > 0U &&
           length <= sequencer::SequencerPatternState::MAX_STEPS &&
           stepsPerBeatCanonical(stepsPerBeat) &&
           sequencer::validPitchEditMode(pitchEditMode) &&
           variationRangesCanonical(variationRanges) &&
           swingOffsetPercent >=
               sequencer::SequencerPatternState::
                   MIN_PATTERN_SWING_OFFSET_PERCENT &&
           swingOffsetPercent <=
               sequencer::SequencerPatternState::
                   MAX_PATTERN_SWING_OFFSET_PERCENT &&
           patternNudgePercent >=
               sequencer::SequencerPatternState::
                   MIN_PATTERN_NUDGE_PERCENT &&
           patternNudgePercent <=
               sequencer::SequencerPatternState::
                   MAX_PATTERN_NUDGE_PERCENT &&
           scalePolicy <= static_cast<uint8_t>(
               sequencer::SequencerPatternScalePolicy::OVERRIDE
           ) &&
           scaleSettingsCanonical(scaleOverride) &&
           enabledMaskCanonical(enabledMask, length);
}

struct PatternEncodeView {
    uint8_t length = sequencer::SequencerPatternState::DEFAULT_LENGTH;
    uint8_t stepsPerBeat = sequencer::SequencerPatternState::DEFAULT_STEPS_PER_BEAT;
    sequencer::SequencerPitchEditMode pitchEditMode =
        sequencer::SequencerPitchEditMode::FOLLOW_SCALE;
    oc::note::sequencer::StepSequencerVariationRanges variationRanges{};
    int8_t swingOffsetPercent = 0;
    int8_t patternNudgePercent = 0;
    sequencer::SequencerPatternScalePolicy scalePolicy =
        sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT;
    oc::note::sequencer::StepSequencerScaleSettings scaleOverride{};
    oc::note::sequencer::StepBitMask128 enabledMask{};
    const uint8_t* note = nullptr;
    const uint8_t* velocity = nullptr;
    const uint16_t* gate = nullptr;
    const int8_t* nudge = nullptr;
    const uint8_t* probability = nullptr;
};

FLASHMEM PatternEncodeView patternEncodeView(
    const sequencer::SequencerPatternState& source
) {
    return {
        .length = source.length.get(),
        .stepsPerBeat = source.stepsPerBeat.get(),
        .pitchEditMode = source.pitchEditMode,
        .variationRanges = source.variationRanges,
        .swingOffsetPercent = source.swingOffsetPercent.get(),
        .patternNudgePercent = source.patternNudgePercent.get(),
        .scalePolicy = source.scalePolicy,
        .scaleOverride = source.scaleOverride,
        .enabledMask = source.enabledMask.get(),
        .note = &source.note[0],
        .velocity = &source.velocity[0],
        .gate = &source.gate[0],
        .nudge = &source.nudge[0],
        .probability = &source.probability[0],
    };
}

FLASHMEM PatternEncodeView patternEncodeView(
    const sequencer::SequencerPatternSnapshot& source
) {
    return {
        .length = source.length,
        .stepsPerBeat = source.stepsPerBeat,
        .pitchEditMode = source.pitchEditMode,
        .variationRanges = source.variationRanges,
        .swingOffsetPercent = source.swingOffsetPercent,
        .patternNudgePercent = source.patternNudgePercent,
        .scalePolicy = source.scalePolicy,
        .scaleOverride = source.scaleOverride,
        .enabledMask = source.enabledMask,
        .note = source.note.data(),
        .velocity = source.velocity.data(),
        .gate = source.gate.data(),
        .nudge = source.nudge.data(),
        .probability = source.probability.data(),
    };
}

FLASHMEM bool patternCanonical(const PatternEncodeView& source) {
    if (source.note == nullptr ||
        source.velocity == nullptr ||
        source.gate == nullptr ||
        source.nudge == nullptr ||
        source.probability == nullptr ||
        !patternHeaderCanonical(
            source.length,
            source.stepsPerBeat,
            static_cast<uint8_t>(source.pitchEditMode),
            source.variationRanges,
            source.swingOffsetPercent,
            source.patternNudgePercent,
            static_cast<uint8_t>(source.scalePolicy),
            source.scaleOverride,
            source.enabledMask
        )) {
        return false;
    }

    for (uint8_t index = 0U; index < PERSISTED_PATTERN_STEPS; ++index) {
        if (source.note[index] > 127U ||
            source.velocity[index] > 127U ||
            source.gate[index] >
                sequencer::SequencerPatternState::MAX_GATE_PERCENT ||
            source.nudge[index] < -50 ||
            source.nudge[index] > 50 ||
            source.probability[index] > 100U) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool writePattern(binary::Writer& writer, const PatternEncodeView& source) {
    if (!patternCanonical(source) ||
        !writer.writeU8(source.length) ||
        !writer.writeU8(source.stepsPerBeat) ||
        !writer.writeU8(static_cast<uint8_t>(source.pitchEditMode)) ||
        !writer.writeU8(source.variationRanges.pitchSemitones) ||
        !writer.writeU8(source.variationRanges.velocity) ||
        !writer.writeU8(source.variationRanges.gatePercent) ||
        !writer.writeU8(source.variationRanges.nudge) ||
        !writer.writeI8(source.swingOffsetPercent) ||
        !writer.writeI8(source.patternNudgePercent) ||
        !writer.writeU8(static_cast<uint8_t>(source.scalePolicy)) ||
        !writer.writeU8(source.scaleOverride.root) ||
        !writer.writeU8(static_cast<uint8_t>(source.scaleOverride.type)) ||
        !writer.writeU8(static_cast<uint8_t>(source.scaleOverride.mode)) ||
        !writer.writeU64(source.enabledMask.low) ||
        !writer.writeU64(source.enabledMask.high)) {
        return false;
    }

    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!writer.writeU8(source.note[i])) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!writer.writeU8(source.velocity[i])) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!writer.writeU16(source.gate[i])) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!writer.writeI8(source.nudge[i])) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!writer.writeU8(source.probability[i])) return false;
    }
    return true;
}

FLASHMEM bool writeProjectSequencerSnapshotPayload(
    binary::Writer& writer,
    const sequencer::SequencerTrackBankSnapshot& snapshot,
    uint8_t focusedStep,
    sequencer::StepProperty activeStepProperty
) {
    if (!trackSelectionCanonical(snapshot.enabledMask, snapshot.activeTrack) ||
        !scaleSettingsCanonical(snapshot.projectScaleSettings) ||
        !stepPropertyCanonical(activeStepProperty) ||
        focusedStep >= snapshot.tracks[snapshot.activeTrack].length) {
        return false;
    }
    for (const auto& track : snapshot.tracks) {
        if (!patternCanonical(patternEncodeView(track))) return false;
    }

    const uint8_t page = static_cast<uint8_t>(
        focusedStep / sequencer::SequencerPatternState::STEPS_PER_PAGE
    );

    if (!writer.writeU8(snapshot.activeTrack) ||
        !writer.writeU16(snapshot.enabledMask) ||
        // Track Mute belongs exclusively to the Project Track chunk. Retain
        // two zeroed bytes so the fixed payload geometry stays unchanged.
        !writer.writeU16(0U) ||
        !writer.writeU8(snapshot.projectScaleSettings.root) ||
        !writer.writeU8(static_cast<uint8_t>(
            snapshot.projectScaleSettings.type
        )) ||
        !writer.writeU8(static_cast<uint8_t>(
            snapshot.projectScaleSettings.mode
        )) ||
        !writer.writeU8(0)) {
        return false;
    }

    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        const PatternEncodeView source = patternEncodeView(snapshot.tracks[i]);
        if (!writePattern(writer, source)) return false;
        if (i == snapshot.activeTrack) {
            if (!writer.writeU8(page) ||
                !writer.writeU8(focusedStep) ||
                !writer.writeU8(static_cast<uint8_t>(activeStepProperty)) ||
                !writer.writeU8(0)) {
                return false;
            }
        } else if (!writer.writeZeroes(4)) {
            return false;
        }
    }

    return writer.ok() && writer.offset() == PROJECT_SEQUENCER_PAYLOAD_SIZE;
}

struct PatternHeader {
    uint8_t length = 0;
    uint8_t stepsPerBeat = 0;
    uint8_t pitchEditMode = 0;
    oc::note::sequencer::StepSequencerVariationRanges variationRanges{};
    int8_t swingOffset = 0;
    int8_t patternNudge = 0;
    uint8_t scalePolicy = 0;
    oc::note::sequencer::StepSequencerScaleSettings scaleOverride{};
    oc::note::sequencer::StepBitMask128 enabledMask{};
};

FLASHMEM bool readPatternHeader(
    binary::Reader& reader,
    PatternHeader& header
) {
    uint8_t scaleType = 0U;
    uint8_t scaleConstraintMode = 0U;
    if (!reader.readU8(header.length) ||
        !reader.readU8(header.stepsPerBeat) ||
        !reader.readU8(header.pitchEditMode) ||
        !reader.readU8(header.variationRanges.pitchSemitones) ||
        !reader.readU8(header.variationRanges.velocity) ||
        !reader.readU8(header.variationRanges.gatePercent) ||
        !reader.readU8(header.variationRanges.nudge) ||
        !reader.readI8(header.swingOffset) ||
        !reader.readI8(header.patternNudge) ||
        !reader.readU8(header.scalePolicy) ||
        !reader.readU8(header.scaleOverride.root) ||
        !reader.readU8(scaleType) ||
        !reader.readU8(scaleConstraintMode) ||
        !reader.readU64(header.enabledMask.low) ||
        !reader.readU64(header.enabledMask.high)) {
        return false;
    }
    header.scaleOverride.type =
        static_cast<oc::note::sequencer::StepSequencerScaleType>(scaleType);
    header.scaleOverride.mode =
        static_cast<oc::note::sequencer::StepSequencerScaleConstraintMode>(
            scaleConstraintMode
        );
    return patternHeaderCanonical(
        header.length,
        header.stepsPerBeat,
        header.pitchEditMode,
        header.variationRanges,
        header.swingOffset,
        header.patternNudge,
        header.scalePolicy,
        header.scaleOverride,
        header.enabledMask
    );
}

FLASHMEM bool validatePattern(
    binary::Reader& reader,
    uint8_t* lengthOut = nullptr
) {
    PatternHeader header{};
    if (!readPatternHeader(reader, header)) return false;
    if (lengthOut != nullptr) *lengthOut = header.length;

    for (uint8_t index = 0U; index < PERSISTED_PATTERN_STEPS; ++index) {
        uint8_t value = 0U;
        if (!reader.readU8(value) || value > 127U) return false;
    }
    for (uint8_t index = 0U; index < PERSISTED_PATTERN_STEPS; ++index) {
        uint8_t value = 0U;
        if (!reader.readU8(value) || value > 127U) return false;
    }
    for (uint8_t index = 0U; index < PERSISTED_PATTERN_STEPS; ++index) {
        uint16_t value = 0U;
        if (!reader.readU16(value) ||
            value > sequencer::SequencerPatternState::MAX_GATE_PERCENT) {
            return false;
        }
    }
    for (uint8_t index = 0U; index < PERSISTED_PATTERN_STEPS; ++index) {
        int8_t value = 0;
        if (!reader.readI8(value) || value < -50 || value > 50) return false;
    }
    for (uint8_t index = 0U; index < PERSISTED_PATTERN_STEPS; ++index) {
        uint8_t value = 0U;
        if (!reader.readU8(value) || value > 100U) return false;
    }
    return true;
}

FLASHMEM bool readPattern(binary::Reader& reader,
                          sequencer::SequencerPatternState& target) {
    PatternHeader header{};
    if (!readPatternHeader(reader, header)) return false;

    target.setContentLength(header.length);
    target.stepsPerBeat.set(header.stepsPerBeat);
    target.setPitchEditMode(
        static_cast<sequencer::SequencerPitchEditMode>(header.pitchEditMode)
    );
    target.setPatternVariationRanges(header.variationRanges);
    target.setPatternSwingOffsetPercent(header.swingOffset);
    target.setPatternNudgePercent(header.patternNudge);
    target.setPatternScalePolicy(
        static_cast<sequencer::SequencerPatternScalePolicy>(
            header.scalePolicy
        )
    );
    target.setPatternScaleOverride(header.scaleOverride);
    target.enabledMask.set(header.enabledMask);

    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!reader.readU8(target.note[i])) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!reader.readU8(target.velocity[i])) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!reader.readU16(target.gate[i])) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!reader.readI8(target.nudge[i])) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!reader.readU8(target.probability[i])) return false;
    }

    target.bumpStepDataRevision();
    return true;
}

FLASHMEM bool readPatternAt(const uint8_t* data,
                            uint16_t size,
                            uint16_t offset,
                            sequencer::SequencerPatternState& target) {
    if (data == nullptr || offset > size || PATTERN_PAYLOAD_SIZE > size - offset) {
        return false;
    }
    return applyPatternPayload(data + offset, PATTERN_PAYLOAD_SIZE, target);
}

FLASHMEM bool readScaleSettings(
    binary::Reader& reader,
    oc::note::sequencer::StepSequencerScaleSettings& settings
) {
    uint8_t type = 0U;
    uint8_t mode = 0U;
    if (!reader.readU8(settings.root) ||
        !reader.readU8(type) ||
        !reader.readU8(mode)) {
        return false;
    }
    settings.type =
        static_cast<oc::note::sequencer::StepSequencerScaleType>(type);
    settings.mode =
        static_cast<oc::note::sequencer::StepSequencerScaleConstraintMode>(
            mode
        );
    return scaleSettingsCanonical(settings);
}

FLASHMEM bool validateProjectSequencerPayload(
    const uint8_t* data,
    uint16_t size
) {
    if (data == nullptr || size != PROJECT_SEQUENCER_PAYLOAD_SIZE) {
        return false;
    }

    binary::Reader reader(data, size);
    uint8_t activeTrack = 0U;
    uint16_t enabledMask = 0U;
    uint16_t reservedProjectTrackState = 0U;
    oc::note::sequencer::StepSequencerScaleSettings projectScale{};
    uint8_t reserved = 0U;
    if (!reader.readU8(activeTrack) ||
        !reader.readU16(enabledMask) ||
        !reader.readU16(reservedProjectTrackState) ||
        !readScaleSettings(reader, projectScale) ||
        !reader.readU8(reserved) ||
        reservedProjectTrackState != 0U ||
        reserved != 0U ||
        !trackSelectionCanonical(enabledMask, activeTrack)) {
        return false;
    }

    for (uint8_t track = 0U;
         track < sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        uint8_t patternLength = 0U;
        if (!validatePattern(reader, &patternLength)) return false;

        uint8_t page = 0U;
        uint8_t focusedStep = 0U;
        uint8_t stepProperty = 0U;
        uint8_t trackReserved = 0U;
        if (!reader.readU8(page) ||
            !reader.readU8(focusedStep) ||
            !reader.readU8(stepProperty) ||
            !reader.readU8(trackReserved) ||
            trackReserved != 0U) {
            return false;
        }

        if (track == activeTrack) {
            if (focusedStep >= patternLength ||
                page != static_cast<uint8_t>(
                    focusedStep /
                    sequencer::SequencerPatternState::STEPS_PER_PAGE
                ) ||
                !stepPropertyCanonical(
                    static_cast<sequencer::StepProperty>(stepProperty)
                )) {
                return false;
            }
        } else if (page != 0U || focusedStep != 0U ||
                   stepProperty != 0U) {
            return false;
        }
    }
    return reader.ok() &&
           reader.offset() == PROJECT_SEQUENCER_PAYLOAD_SIZE;
}

FLASHMEM bool validateSetPayload(
    const uint8_t* data,
    uint16_t size
) {
    if (data == nullptr || size != SET_PAYLOAD_SIZE) return false;

    binary::Reader reader(data, size);
    uint8_t trackCount = 0U;
    uint8_t activeTrack = 0U;
    uint16_t enabledMask = 0U;
    uint16_t reservedProjectTrackState = 0U;
    oc::note::sequencer::StepSequencerScaleSettings projectScale{};
    uint8_t reserved = 0U;
    if (!reader.readU8(trackCount) ||
        !reader.readU8(activeTrack) ||
        !reader.readU16(enabledMask) ||
        !reader.readU16(reservedProjectTrackState) ||
        !readScaleSettings(reader, projectScale) ||
        !reader.readU8(reserved) ||
        trackCount != sequencer::SequencerTrackBankState::TRACK_COUNT ||
        reservedProjectTrackState != 0U ||
        reserved != 0U ||
        !trackSelectionCanonical(enabledMask, activeTrack)) {
        return false;
    }
    for (uint8_t track = 0U;
         track < sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        if (!validatePattern(reader)) return false;
    }
    return reader.ok() && reader.offset() == SET_PAYLOAD_SIZE;
}

}  // namespace

FLASHMEM bool fillPatternPayload(const sequencer::SequencerPatternState& source,
                                 uint8_t* out,
                                 uint16_t capacity) {
    if (capacity != PATTERN_PAYLOAD_SIZE) return false;
    binary::Writer writer(out, capacity);
    return writePattern(writer, patternEncodeView(source)) &&
           writer.ok() &&
           writer.offset() == PATTERN_PAYLOAD_SIZE;
}

FLASHMEM bool applyPatternPayload(const uint8_t* data,
                                  uint16_t size,
                                  sequencer::SequencerPatternState& target) {
    if (data == nullptr || size != PATTERN_PAYLOAD_SIZE) return false;
    binary::Reader validationReader(data, size);
    if (!validatePattern(validationReader) ||
        !validationReader.ok() ||
        validationReader.offset() != PATTERN_PAYLOAD_SIZE) {
        return false;
    }

    binary::Reader reader(data, size);
    return readPattern(reader, target) &&
           reader.ok() &&
           reader.offset() == PATTERN_PAYLOAD_SIZE;
}

FLASHMEM bool fillProjectSequencerPayload(
    const sequencer::SequencerTrackBankSnapshot& snapshot,
    uint8_t focusedStep,
    sequencer::StepProperty activeStepProperty,
    uint8_t* out,
    uint16_t capacity
) {
    if (capacity != PROJECT_SEQUENCER_PAYLOAD_SIZE) return false;

    binary::Writer writer(out, capacity);
    return writeProjectSequencerSnapshotPayload(
        writer,
        snapshot,
        focusedStep,
        activeStepProperty
    );
}

FLASHMEM bool applyProjectSequencerPayload(const uint8_t* data,
                                           uint16_t size,
                                           sequencer::SequencerTrackBankState& trackBank,
                                           sequencer::SequencerState& active) {
    if (!validateProjectSequencerPayload(data, size)) return false;

    binary::Reader reader(data, size);
    uint8_t activeTrackRaw = 0;
    uint16_t enabledMask = 0;
    uint16_t reservedProjectTrackState = 0;
    uint8_t projectScaleRoot = 0;
    uint8_t projectScaleType = 0;
    uint8_t projectScaleConstraintMode = 0;
    uint8_t reserved = 0;
    if (!reader.readU8(activeTrackRaw) ||
        !reader.readU16(enabledMask) ||
        !reader.readU16(reservedProjectTrackState) ||
        !reader.readU8(projectScaleRoot) ||
        !reader.readU8(projectScaleType) ||
        !reader.readU8(projectScaleConstraintMode) ||
        !reader.readU8(reserved)) {
        return false;
    }

    trackBank.reset();
    trackBank.syncSharedTrackState(enabledMask, activeTrackRaw);
    trackBank.setProjectScaleSettings(payloadScaleSettings(
        projectScaleRoot,
        projectScaleType,
        projectScaleConstraintMode
    ));

    const uint8_t activeTrack = trackBank.activeTrackIndex();
    uint16_t activePatternOffset = 0;
    uint8_t activePage = 0;
    uint8_t activeFocusedStep = 0;
    uint8_t activeStepProperty = static_cast<uint8_t>(sequencer::StepProperty::NOTE);

    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        const uint16_t patternOffset = static_cast<uint16_t>(reader.offset());
        if (!readPattern(reader, trackBank.track(i))) return false;

        uint8_t page = 0;
        uint8_t focused = 0;
        uint8_t stepProperty = 0;
        uint8_t trackReserved = 0;
        if (!reader.readU8(page) ||
            !reader.readU8(focused) ||
            !reader.readU8(stepProperty) ||
            !reader.readU8(trackReserved)) {
            return false;
        }

        if (i == activeTrack) {
            activePatternOffset = patternOffset;
            activePage = page;
            activeFocusedStep = focused;
            activeStepProperty = stepProperty;
        }
    }
    if (!reader.ok() || reader.offset() != PROJECT_SEQUENCER_PAYLOAD_SIZE) return false;
    if (!readPatternAt(data, size, activePatternOffset, active.pattern)) return false;

    active.focusedStep.set(activeFocusedStep);
    active.page.set(activePage);
    active.activeStepProperty.set(
        static_cast<sequencer::StepProperty>(activeStepProperty)
    );
    return true;
}

FLASHMEM bool fillSetPayload(const sequencer::SequencerTrackBankState& trackBank,
                             const sequencer::SequencerState& active,
                             uint8_t* out,
                             uint16_t capacity) {
    if (capacity != SET_PAYLOAD_SIZE) return false;

    const uint8_t activeTrack = trackBank.activeTrackIndex();
    const uint16_t enabledMask = trackBank.currentEnabledMask();
    const auto projectScale = trackBank.projectScaleSettings();
    if (!trackSelectionCanonical(enabledMask, activeTrack) ||
        !scaleSettingsCanonical(projectScale)) {
        return false;
    }
    for (uint8_t track = 0U;
         track < sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const auto& source =
            track == activeTrack ? active.pattern : trackBank.track(track);
        if (!patternCanonical(patternEncodeView(source))) return false;
    }

    binary::Writer writer(out, capacity);
    if (!writer.writeU8(sequencer::SequencerTrackBankState::TRACK_COUNT) ||
        !writer.writeU8(activeTrack) ||
        !writer.writeU16(enabledMask) ||
        // Set assets carry Sequencer topology/content, never Project Track mix.
        !writer.writeU16(0U) ||
        !writer.writeU8(projectScale.root) ||
        !writer.writeU8(static_cast<uint8_t>(projectScale.type)) ||
        !writer.writeU8(static_cast<uint8_t>(projectScale.mode)) ||
        !writer.writeU8(0)) {
        return false;
    }

    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        const auto& source = (i == activeTrack) ? active.pattern : trackBank.track(i);
        if (!writePattern(writer, patternEncodeView(source))) return false;
    }

    return writer.ok() && writer.offset() == SET_PAYLOAD_SIZE;
}

FLASHMEM bool applySetPayload(const uint8_t* data,
                              uint16_t size,
                              sequencer::SequencerTrackBankState& trackBank,
                              sequencer::SequencerState& active) {
    if (!validateSetPayload(data, size)) return false;

    binary::Reader reader(data, size);
    uint8_t trackCountRaw = 0;
    uint8_t activeTrackRaw = 0;
    uint16_t enabledMask = 0;
    uint16_t reservedProjectTrackState = 0;
    uint8_t projectScaleRoot = 0;
    uint8_t projectScaleType = 0;
    uint8_t projectScaleConstraintMode = 0;
    uint8_t reserved = 0;
    if (!reader.readU8(trackCountRaw) ||
        !reader.readU8(activeTrackRaw) ||
        !reader.readU16(enabledMask) ||
        !reader.readU16(reservedProjectTrackState) ||
        !reader.readU8(projectScaleRoot) ||
        !reader.readU8(projectScaleType) ||
        !reader.readU8(projectScaleConstraintMode) ||
        !reader.readU8(reserved)) {
        return false;
    }

    trackBank.reset();
    trackBank.syncSharedTrackState(enabledMask, activeTrackRaw);
    trackBank.setProjectScaleSettings(payloadScaleSettings(
        projectScaleRoot,
        projectScaleType,
        projectScaleConstraintMode
    ));

    uint16_t activePatternOffset = 0;
    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        const uint16_t patternOffset = static_cast<uint16_t>(reader.offset());
        if (!readPattern(reader, trackBank.track(i))) return false;
        if (i == activeTrackRaw) activePatternOffset = patternOffset;
    }
    if (!reader.ok() || reader.offset() != SET_PAYLOAD_SIZE) return false;
    if (!readPatternAt(data, size, activePatternOffset, active.pattern)) return false;

    active.focusedStep.set(0);
    active.page.set(0);
    active.activeStepProperty.set(sequencer::StepProperty::NOTE);
    return true;
}

}  // namespace core::persistence::sequencer_codec
