#include "persistence/SequencerPersistenceCodec.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

namespace core::persistence::sequencer_codec {

namespace {

uint8_t sanitizeLength(uint8_t length) {
    if (length == 0 || length > state::sequencer::SequencerPatternState::MAX_STEPS) {
        return state::sequencer::SequencerPatternState::DEFAULT_LENGTH;
    }
    return length;
}

oc::note::sequencer::StepBitMask128 lengthMask(uint8_t length) {
    return oc::note::sequencer::StepBitMask128::prefixMask(length);
}

uint8_t sanitizeStepsPerBeat(uint8_t spb) {
    if (spb == 0) {
        return state::sequencer::SequencerPatternState::DEFAULT_STEPS_PER_BEAT;
    }
    return spb;
}

uint8_t sanitizeMidiChannel(uint8_t channel) {
    return (channel > 15U)
               ? state::sequencer::SequencerPatternState::DEFAULT_MIDI_CHANNEL_0BASED
               : channel;
}

uint8_t sanitizeMidi7(uint8_t value) {
    return (value > 127U) ? 127U : value;
}

uint16_t sanitizeGate(uint16_t value) {
    return state::sequencer::SequencerPatternState::clampGatePercent(value);
}

uint8_t sanitizeProbability(uint8_t value) {
    return state::sequencer::SequencerPatternState::clampProbability(value);
}

oc::note::sequencer::StepSequencerVariationRanges sanitizeVariationRanges(
    oc::note::sequencer::StepSequencerVariationRanges ranges
) {
    ranges.clamp();
    return ranges;
}

oc::note::sequencer::StepSequencerVariationRanges payloadVariationRanges(
    const PatternPayload& payload
) {
    return sanitizeVariationRanges({
        .pitchSemitones = payload.variationPitchSemitones,
        .velocity = payload.variationVelocity,
        .gatePercent = payload.variationGatePercent,
        .nudge = payload.variationNudge,
    });
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

state::sequencer::StepProperty sanitizeStepProperty(uint8_t value) {
    if (value > static_cast<uint8_t>(state::sequencer::StepProperty::PROBABILITY)) {
        return state::sequencer::StepProperty::NOTE;
    }
    return static_cast<state::sequencer::StepProperty>(value);
}

uint8_t sanitizeFocusedStep(uint8_t focused, uint8_t length) {
    if (length == 0) return 0;
    return (focused >= length) ? static_cast<uint8_t>(length - 1) : focused;
}

}  // namespace

FLASHMEM void fillPatternPayload(const state::sequencer::SequencerPatternState& source, PatternPayload& out) {
    const uint8_t length = sanitizeLength(source.length.get());
    out.length = length;
    out.stepsPerBeat = sanitizeStepsPerBeat(source.stepsPerBeat.get());
    out.midiChannel = sanitizeMidiChannel(source.midiChannel.get());
    out.pitchEditMode = static_cast<uint8_t>(source.pitchEditMode);
    const auto variationRanges = sanitizeVariationRanges(source.variationRanges);
    out.variationPitchSemitones = variationRanges.pitchSemitones;
    out.variationVelocity = variationRanges.velocity;
    out.variationGatePercent = variationRanges.gatePercent;
    out.variationNudge = variationRanges.nudge;
    const auto scaleOverride = sanitizeScaleSettings(source.scaleOverride);
    out.scalePolicy = static_cast<uint8_t>(source.scalePolicy);
    out.scaleRoot = scaleOverride.root;
    out.scaleType = static_cast<uint8_t>(scaleOverride.type);
    out.scaleConstraintMode = static_cast<uint8_t>(scaleOverride.mode);
    const auto mask = source.enabledMask.get() & lengthMask(length);
    out.enabledMaskLow = mask.low;
    out.enabledMaskHigh = mask.high;

    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        out.note[i] = sanitizeMidi7(source.note[i]);
        out.velocity[i] = sanitizeMidi7(source.velocity[i]);
        out.gate[i] = sanitizeGate(source.gate[i]);
        out.nudge[i] = source.nudge[i];
        out.probability[i] = sanitizeProbability(source.probability[i]);
    }
}

FLASHMEM void applyPatternPayload(const PatternPayload& payload, state::sequencer::SequencerPatternState& target) {
    const uint8_t length = sanitizeLength(payload.length);
    target.length.set(length);
    target.stepsPerBeat.set(sanitizeStepsPerBeat(payload.stepsPerBeat));
    target.midiChannel.set(sanitizeMidiChannel(payload.midiChannel));
    target.setPitchEditMode(state::sequencer::sanitizePitchEditMode(payload.pitchEditMode));
    target.setPatternVariationRanges(payloadVariationRanges(payload));
    target.setPatternScalePolicy(state::sequencer::sanitizePatternScalePolicy(payload.scalePolicy));
    target.setPatternScaleOverride(payloadScaleSettings(
        payload.scaleRoot,
        payload.scaleType,
        payload.scaleConstraintMode
    ));
    target.enabledMask.set(
        oc::note::sequencer::StepBitMask128{payload.enabledMaskLow, payload.enabledMaskHigh} &
        lengthMask(length)
    );

    for (uint8_t i = 0; i < PERSISTED_PATTERN_STEPS; ++i) {
        target.note[i] = sanitizeMidi7(payload.note[i]);
        target.velocity[i] = sanitizeMidi7(payload.velocity[i]);
        target.gate[i] = sanitizeGate(payload.gate[i]);
        target.nudge[i] = payload.nudge[i];
        target.probability[i] = sanitizeProbability(payload.probability[i]);
    }

    target.bumpStepDataRevision();
}

FLASHMEM void fillWorkspacePayload(const state::sequencer::SequencerTrackBankState& trackBank,
                                   const state::sequencer::SequencerState& active,
                                   WorkspacePayload& out) {
    const uint8_t activeTrack =
        state::sequencer::SequencerTrackBankState::clampTrackIndex(trackBank.activeTrackIndex());
    const auto projectScale = trackBank.projectScaleSettings();
    out.projectScaleRoot = projectScale.root;
    out.projectScaleType = static_cast<uint8_t>(projectScale.type);
    out.projectScaleConstraintMode = static_cast<uint8_t>(projectScale.mode);

    for (uint8_t i = 0; i < state::sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        const auto& source = (i == activeTrack) ? active.pattern : trackBank.track(i);
        fillPatternPayload(source, out.tracks[i].pattern);
        if (i == activeTrack) {
            out.tracks[i].focusedStep =
                sanitizeFocusedStep(active.focusedStep.get(), out.tracks[i].pattern.length);
            out.tracks[i].page = active.page.get();
            out.tracks[i].activeStepProperty = static_cast<uint8_t>(active.activeStepProperty.get());
        }
    }
}

FLASHMEM void applyWorkspacePayload(const WorkspacePayload& payload,
                                    state::sequencer::SequencerTrackBankState& trackBank,
                                    state::sequencer::SequencerState& active) {
    uint16_t enabledMask = 0x0001;
    uint8_t activeTrack = 0;
    trackBank.captureSharedTrackState(enabledMask, activeTrack);
    trackBank.reset();
    trackBank.syncSharedTrackState(enabledMask, activeTrack);
    trackBank.setProjectScaleSettings(payloadScaleSettings(
        payload.projectScaleRoot,
        payload.projectScaleType,
        payload.projectScaleConstraintMode
    ));

    for (uint8_t i = 0; i < state::sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        applyPatternPayload(payload.tracks[i].pattern, trackBank.track(i));
    }

    applyPatternPayload(payload.tracks[activeTrack].pattern, active.pattern);
    const uint8_t focused =
        sanitizeFocusedStep(payload.tracks[activeTrack].focusedStep, active.pattern.length.get());
    active.focusedStep.set(focused);
    const uint8_t pageCount = active.activePageCount();
    const uint8_t safePage =
        (pageCount == 0) ? 0 : static_cast<uint8_t>(payload.tracks[activeTrack].page % pageCount);
    active.page.set(safePage);
    active.activeStepProperty.set(
        sanitizeStepProperty(payload.tracks[activeTrack].activeStepProperty)
    );
}

FLASHMEM void fillSetPayload(const state::sequencer::SequencerTrackBankState& trackBank,
                             const state::sequencer::SequencerState& active,
                             SetPayload& out) {
    const uint8_t activeTrack =
        state::sequencer::SequencerTrackBankState::clampTrackIndex(trackBank.activeTrackIndex());
    out.trackCount = state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    out.activeTrack = activeTrack;
    out.enabledMask = trackBank.currentEnabledMask();
    const auto projectScale = trackBank.projectScaleSettings();
    out.projectScaleRoot = projectScale.root;
    out.projectScaleType = static_cast<uint8_t>(projectScale.type);
    out.projectScaleConstraintMode = static_cast<uint8_t>(projectScale.mode);

    for (uint8_t i = 0; i < state::sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        const auto& source = (i == activeTrack) ? active.pattern : trackBank.track(i);
        fillPatternPayload(source, out.tracks[i]);
    }
}

FLASHMEM void applySetPayload(const SetPayload& payload,
                              state::sequencer::SequencerTrackBankState& trackBank,
                              state::sequencer::SequencerState& active) {
    trackBank.reset();
    trackBank.syncSharedTrackState(payload.enabledMask, 0);
    trackBank.setProjectScaleSettings(payloadScaleSettings(
        payload.projectScaleRoot,
        payload.projectScaleType,
        payload.projectScaleConstraintMode
    ));

    const uint8_t trackCount = static_cast<uint8_t>(std::min<uint16_t>(
        payload.trackCount == 0 ? 1 : payload.trackCount,
        state::sequencer::SequencerTrackBankState::TRACK_COUNT
    ));

    for (uint8_t i = 0; i < trackCount; ++i) {
        applyPatternPayload(payload.tracks[i], trackBank.track(i));
    }

    const uint8_t activeTrack =
        std::min<uint8_t>(payload.activeTrack, static_cast<uint8_t>(trackCount - 1));
    applyPatternPayload(payload.tracks[activeTrack], active.pattern);
    active.focusedStep.set(0);
    active.page.set(0);
    active.activeStepProperty.set(state::sequencer::StepProperty::NOTE);
    trackBank.syncSharedTrackState(trackBank.currentEnabledMask(), activeTrack);
}

}  // namespace core::persistence::sequencer_codec
