#include "persistence/SequencerPersistenceCodec.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceBinaryCodec.hpp"

namespace core::persistence::sequencer_codec {

namespace {

namespace binary = core::persistence::binary_codec;
namespace sequencer = core::state::sequencer;

uint8_t sanitizeLength(uint8_t length) {
    if (length == 0 || length > sequencer::SequencerPatternState::MAX_STEPS) {
        return sequencer::SequencerPatternState::DEFAULT_LENGTH;
    }
    return length;
}

oc::note::sequencer::StepBitMask128 lengthMask(uint8_t length) {
    return oc::note::sequencer::StepBitMask128::prefixMask(length);
}

uint8_t sanitizeStepsPerBeat(uint8_t spb) {
    if (spb == 0) {
        return sequencer::SequencerPatternState::DEFAULT_STEPS_PER_BEAT;
    }
    return spb;
}

uint8_t sanitizeMidiChannel(uint8_t channel) {
    return (channel > 15U)
               ? sequencer::SequencerPatternState::DEFAULT_MIDI_CHANNEL_0BASED
               : channel;
}

uint8_t sanitizeMidi7(uint8_t value) {
    return (value > 127U) ? 127U : value;
}

uint16_t sanitizeGate(uint16_t value) {
    return sequencer::SequencerPatternState::clampGatePercent(value);
}

uint8_t sanitizeProbability(uint8_t value) {
    return sequencer::SequencerPatternState::clampProbability(value);
}

oc::note::sequencer::StepSequencerVariationRanges sanitizeVariationRanges(
    oc::note::sequencer::StepSequencerVariationRanges ranges
) {
    ranges.clamp();
    return ranges;
}

oc::note::sequencer::StepSequencerScaleSettings sanitizeScaleSettings(
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    settings.clamp();
    return settings;
}

oc::note::sequencer::StepSequencerScaleSettings payloadScaleSettings(uint8_t root,
                                                                     uint8_t type,
                                                                     uint8_t mode) {
    return sanitizeScaleSettings({
        .root = root,
        .type = static_cast<oc::note::sequencer::StepSequencerScaleType>(type),
        .mode = static_cast<oc::note::sequencer::StepSequencerScaleConstraintMode>(mode),
    });
}

sequencer::StepProperty sanitizeStepProperty(uint8_t value) {
    if (value > static_cast<uint8_t>(sequencer::StepProperty::PROBABILITY)) {
        return sequencer::StepProperty::NOTE;
    }
    return static_cast<sequencer::StepProperty>(value);
}

uint8_t sanitizeFocusedStep(uint8_t focused, uint8_t length) {
    if (length == 0) return 0;
    return (focused >= length) ? static_cast<uint8_t>(length - 1) : focused;
}

FLASHMEM bool writePattern(binary::Writer& writer,
                           const sequencer::SequencerPatternState& source) {
    const uint8_t length = sanitizeLength(source.length.get());
    const auto variationRanges = sanitizeVariationRanges(source.variationRanges);
    const auto scaleOverride = sanitizeScaleSettings(source.scaleOverride);
    const auto mask = source.enabledMask.get() & lengthMask(length);

    if (!writer.writeU8(length) ||
        !writer.writeU8(sanitizeStepsPerBeat(source.stepsPerBeat.get())) ||
        !writer.writeU8(sanitizeMidiChannel(source.midiChannel.get())) ||
        !writer.writeU8(static_cast<uint8_t>(source.pitchEditMode)) ||
        !writer.writeU8(variationRanges.pitchSemitones) ||
        !writer.writeU8(variationRanges.velocity) ||
        !writer.writeU8(variationRanges.gatePercent) ||
        !writer.writeU8(variationRanges.nudge) ||
        !writer.writeI8(sequencer::SequencerPatternState::clampPatternSwingOffsetPercent(
            source.swingOffsetPercent.get()
        )) ||
        !writer.writeI8(sequencer::SequencerPatternState::clampPatternNudgePercent(
            source.patternNudgePercent.get()
        )) ||
        !writer.writeU8(static_cast<uint8_t>(source.scalePolicy)) ||
        !writer.writeU8(scaleOverride.root) ||
        !writer.writeU8(static_cast<uint8_t>(scaleOverride.type)) ||
        !writer.writeU8(static_cast<uint8_t>(scaleOverride.mode)) ||
        !writer.writeU64(mask.low) ||
        !writer.writeU64(mask.high)) {
        return false;
    }

    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!writer.writeU8(sanitizeMidi7(source.note[i]))) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!writer.writeU8(sanitizeMidi7(source.velocity[i]))) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!writer.writeU16(sanitizeGate(source.gate[i]))) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!writer.writeI8(source.nudge[i])) return false;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        if (!writer.writeU8(sanitizeProbability(source.probability[i]))) return false;
    }
    return true;
}

FLASHMEM bool readPattern(binary::Reader& reader,
                          sequencer::SequencerPatternState& target) {
    uint8_t lengthRaw = 0;
    uint8_t stepsPerBeat = 0;
    uint8_t midiChannel = 0;
    uint8_t pitchEditMode = 0;
    uint8_t variationPitch = 0;
    uint8_t variationVelocity = 0;
    uint8_t variationGate = 0;
    uint8_t variationNudge = 0;
    int8_t swingOffset = 0;
    int8_t patternNudge = 0;
    uint8_t scalePolicy = 0;
    uint8_t scaleRoot = 0;
    uint8_t scaleType = 0;
    uint8_t scaleConstraintMode = 0;
    uint64_t enabledMaskLow = 0;
    uint64_t enabledMaskHigh = 0;

    if (!reader.readU8(lengthRaw) ||
        !reader.readU8(stepsPerBeat) ||
        !reader.readU8(midiChannel) ||
        !reader.readU8(pitchEditMode) ||
        !reader.readU8(variationPitch) ||
        !reader.readU8(variationVelocity) ||
        !reader.readU8(variationGate) ||
        !reader.readU8(variationNudge) ||
        !reader.readI8(swingOffset) ||
        !reader.readI8(patternNudge) ||
        !reader.readU8(scalePolicy) ||
        !reader.readU8(scaleRoot) ||
        !reader.readU8(scaleType) ||
        !reader.readU8(scaleConstraintMode) ||
        !reader.readU64(enabledMaskLow) ||
        !reader.readU64(enabledMaskHigh)) {
        return false;
    }

    const uint8_t length = sanitizeLength(lengthRaw);
    target.length.set(length);
    target.stepsPerBeat.set(sanitizeStepsPerBeat(stepsPerBeat));
    target.midiChannel.set(sanitizeMidiChannel(midiChannel));
    target.setPitchEditMode(sequencer::sanitizePitchEditMode(pitchEditMode));
    target.setPatternVariationRanges(sanitizeVariationRanges({
        .pitchSemitones = variationPitch,
        .velocity = variationVelocity,
        .gatePercent = variationGate,
        .nudge = variationNudge,
    }));
    target.setPatternSwingOffsetPercent(swingOffset);
    target.setPatternNudgePercent(patternNudge);
    target.setPatternScalePolicy(sequencer::sanitizePatternScalePolicy(scalePolicy));
    target.setPatternScaleOverride(payloadScaleSettings(
        scaleRoot,
        scaleType,
        scaleConstraintMode
    ));
    target.enabledMask.set(
        oc::note::sequencer::StepBitMask128{enabledMaskLow, enabledMaskHigh} &
        lengthMask(length)
    );

    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        uint8_t value = 0;
        if (!reader.readU8(value)) return false;
        target.note[i] = sanitizeMidi7(value);
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        uint8_t value = 0;
        if (!reader.readU8(value)) return false;
        target.velocity[i] = sanitizeMidi7(value);
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        uint16_t value = 0;
        if (!reader.readU16(value)) return false;
        target.gate[i] = sanitizeGate(value);
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        int8_t value = 0;
        if (!reader.readI8(value)) return false;
        target.nudge[i] = value;
    }
    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        uint8_t value = 0;
        if (!reader.readU8(value)) return false;
        target.probability[i] = sanitizeProbability(value);
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

}  // namespace

FLASHMEM bool fillPatternPayload(const sequencer::SequencerPatternState& source,
                                 uint8_t* out,
                                 uint16_t capacity) {
    if (capacity != PATTERN_PAYLOAD_SIZE) return false;
    binary::Writer writer(out, capacity);
    return writePattern(writer, source) &&
           writer.ok() &&
           writer.offset() == PATTERN_PAYLOAD_SIZE;
}

FLASHMEM bool applyPatternPayload(const uint8_t* data,
                                  uint16_t size,
                                  sequencer::SequencerPatternState& target) {
    if (size != PATTERN_PAYLOAD_SIZE) return false;
    binary::Reader reader(data, size);
    return readPattern(reader, target) &&
           reader.ok() &&
           reader.offset() == PATTERN_PAYLOAD_SIZE;
}

FLASHMEM bool fillProjectSequencerPayload(const sequencer::SequencerTrackBankState& trackBank,
                                          const sequencer::SequencerState& active,
                                          uint8_t* out,
                                          uint16_t capacity) {
    if (capacity != PROJECT_SEQUENCER_PAYLOAD_SIZE) return false;

    binary::Writer writer(out, capacity);
    const uint8_t activeTrack =
        sequencer::SequencerTrackBankState::clampTrackIndex(trackBank.activeTrackIndex());
    const auto projectScale = trackBank.projectScaleSettings();
    if (!writer.writeU8(activeTrack) ||
        !writer.writeU16(trackBank.currentEnabledMask()) ||
        !writer.writeU16(trackBank.currentMutedMask()) ||
        !writer.writeU8(projectScale.root) ||
        !writer.writeU8(static_cast<uint8_t>(projectScale.type)) ||
        !writer.writeU8(static_cast<uint8_t>(projectScale.mode)) ||
        !writer.writeU8(0)) {
        return false;
    }

    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        const auto& source = (i == activeTrack) ? active.pattern : trackBank.track(i);
        if (!writePattern(writer, source)) return false;
        if (i == activeTrack) {
            if (!writer.writeU8(active.page.get()) ||
                !writer.writeU8(sanitizeFocusedStep(
                    active.focusedStep.get(),
                    sanitizeLength(source.length.get())
                )) ||
                !writer.writeU8(static_cast<uint8_t>(active.activeStepProperty.get())) ||
                !writer.writeU8(0)) {
                return false;
            }
        } else if (!writer.writeZeroes(4)) {
            return false;
        }
    }

    return writer.ok() && writer.offset() == PROJECT_SEQUENCER_PAYLOAD_SIZE;
}

FLASHMEM bool applyProjectSequencerPayload(const uint8_t* data,
                                           uint16_t size,
                                           sequencer::SequencerTrackBankState& trackBank,
                                           sequencer::SequencerState& active) {
    if (size != PROJECT_SEQUENCER_PAYLOAD_SIZE) return false;

    binary::Reader reader(data, size);
    uint8_t activeTrackRaw = 0;
    uint16_t enabledMask = 0;
    uint16_t mutedMask = 0;
    uint8_t projectScaleRoot = 0;
    uint8_t projectScaleType = 0;
    uint8_t projectScaleConstraintMode = 0;
    uint8_t reserved = 0;
    if (!reader.readU8(activeTrackRaw) ||
        !reader.readU16(enabledMask) ||
        !reader.readU16(mutedMask) ||
        !reader.readU8(projectScaleRoot) ||
        !reader.readU8(projectScaleType) ||
        !reader.readU8(projectScaleConstraintMode) ||
        !reader.readU8(reserved)) {
        return false;
    }
    (void)reserved;

    const uint8_t requestedActiveTrack =
        sequencer::SequencerTrackBankState::clampTrackIndex(activeTrackRaw);
    trackBank.reset();
    trackBank.syncSharedTrackState(enabledMask, requestedActiveTrack);
    trackBank.setMutedMask(mutedMask);
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
        (void)trackReserved;

        if (i == activeTrack) {
            activePatternOffset = patternOffset;
            activePage = page;
            activeFocusedStep = focused;
            activeStepProperty = stepProperty;
        }
    }
    if (!reader.ok() || reader.offset() != PROJECT_SEQUENCER_PAYLOAD_SIZE) return false;
    if (!readPatternAt(data, size, activePatternOffset, active.pattern)) return false;

    active.focusedStep.set(
        sanitizeFocusedStep(activeFocusedStep, active.pattern.length.get())
    );
    const uint8_t pageCount = active.activePageCount();
    const uint8_t safePage =
        (pageCount == 0) ? 0 : static_cast<uint8_t>(activePage % pageCount);
    active.page.set(safePage);
    active.activeStepProperty.set(sanitizeStepProperty(activeStepProperty));
    return true;
}

FLASHMEM bool fillSetPayload(const sequencer::SequencerTrackBankState& trackBank,
                             const sequencer::SequencerState& active,
                             uint8_t* out,
                             uint16_t capacity) {
    if (capacity != SET_PAYLOAD_SIZE) return false;

    binary::Writer writer(out, capacity);
    const uint8_t activeTrack =
        sequencer::SequencerTrackBankState::clampTrackIndex(trackBank.activeTrackIndex());
    const auto projectScale = trackBank.projectScaleSettings();
    if (!writer.writeU8(sequencer::SequencerTrackBankState::TRACK_COUNT) ||
        !writer.writeU8(activeTrack) ||
        !writer.writeU16(trackBank.currentEnabledMask()) ||
        !writer.writeU16(trackBank.currentMutedMask()) ||
        !writer.writeU8(projectScale.root) ||
        !writer.writeU8(static_cast<uint8_t>(projectScale.type)) ||
        !writer.writeU8(static_cast<uint8_t>(projectScale.mode)) ||
        !writer.writeU8(0)) {
        return false;
    }

    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        const auto& source = (i == activeTrack) ? active.pattern : trackBank.track(i);
        if (!writePattern(writer, source)) return false;
    }

    return writer.ok() && writer.offset() == SET_PAYLOAD_SIZE;
}

FLASHMEM bool applySetPayload(const uint8_t* data,
                              uint16_t size,
                              sequencer::SequencerTrackBankState& trackBank,
                              sequencer::SequencerState& active) {
    if (size != SET_PAYLOAD_SIZE) return false;

    binary::Reader reader(data, size);
    uint8_t trackCountRaw = 0;
    uint8_t activeTrackRaw = 0;
    uint16_t enabledMask = 0;
    uint16_t mutedMask = 0;
    uint8_t projectScaleRoot = 0;
    uint8_t projectScaleType = 0;
    uint8_t projectScaleConstraintMode = 0;
    uint8_t reserved = 0;
    if (!reader.readU8(trackCountRaw) ||
        !reader.readU8(activeTrackRaw) ||
        !reader.readU16(enabledMask) ||
        !reader.readU16(mutedMask) ||
        !reader.readU8(projectScaleRoot) ||
        !reader.readU8(projectScaleType) ||
        !reader.readU8(projectScaleConstraintMode) ||
        !reader.readU8(reserved)) {
        return false;
    }
    (void)reserved;

    const uint8_t trackCount = static_cast<uint8_t>(std::min<uint16_t>(
        trackCountRaw == 0 ? 1 : trackCountRaw,
        sequencer::SequencerTrackBankState::TRACK_COUNT
    ));
    const uint8_t activeTrack =
        std::min<uint8_t>(activeTrackRaw, static_cast<uint8_t>(trackCount - 1));

    trackBank.reset();
    trackBank.syncSharedTrackState(enabledMask, 0);
    trackBank.setMutedMask(mutedMask);
    trackBank.setProjectScaleSettings(payloadScaleSettings(
        projectScaleRoot,
        projectScaleType,
        projectScaleConstraintMode
    ));

    uint16_t activePatternOffset = 0;
    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        const uint16_t patternOffset = static_cast<uint16_t>(reader.offset());
        if (i < trackCount) {
            if (!readPattern(reader, trackBank.track(i))) return false;
            if (i == activeTrack) activePatternOffset = patternOffset;
        } else if (!reader.skip(PATTERN_PAYLOAD_SIZE)) {
            return false;
        }
    }
    if (!reader.ok() || reader.offset() != SET_PAYLOAD_SIZE) return false;
    if (!readPatternAt(data, size, activePatternOffset, active.pattern)) return false;

    active.focusedStep.set(0);
    active.page.set(0);
    active.activeStepProperty.set(sequencer::StepProperty::NOTE);
    trackBank.syncSharedTrackState(trackBank.currentEnabledMask(), activeTrack);
    return true;
}

}  // namespace core::persistence::sequencer_codec
