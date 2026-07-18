#include "SequencerChordEditOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "SequencerInputUtils.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"

namespace core::handler::sequencer::chord_edit_ops {

namespace {

namespace input_utils = core::handler::sequencer::input_utils;

FLASHMEM uint8_t voiceCountFromNormalized(float normalized) {
    using Spec = oc::note::sequencer::StepSequencerChordSpec;
    constexpr int minVoices = 2;
    constexpr int maxVoices = Spec::MAX_VOICES;
    const int index = input_utils::normalizedToInclusiveInt(normalized, maxVoices - minVoices);
    return static_cast<uint8_t>(minVoices + index);
}

FLASHMEM int8_t signedFromNormalized(float normalized, int minValue, int maxValue) {
    const int index = input_utils::normalizedToInclusiveInt(normalized, maxValue - minValue);
    return static_cast<int8_t>(minValue + index);
}

FLASHMEM oc::note::sequencer::StepSequencerChordSpec semanticDefault(
    bool scaleConstrained,
    uint8_t voiceCount = 0
) {
    const auto harmony = oc::note::sequencer::defaultChordHarmony(scaleConstrained);
    return oc::note::sequencer::StepSequencerChordSpec::semantic(
        harmony,
        voiceCount == 0
            ? oc::note::sequencer::recommendedChordVoiceCount(harmony)
            : voiceCount
    );
}

FLASHMEM void ensureSemantic(
    oc::note::sequencer::StepSequencerChordSpec& spec,
    bool scaleConstrained
) {
    if (spec.isSemantic()) return;
    const int8_t strum = spec.strum;
    const int8_t velocityContour = spec.velocityCurve;
    spec = semanticDefault(scaleConstrained, spec.voiceCount);
    spec.strum = strum;
    spec.velocityCurve = velocityContour;
}

}  // namespace

FLASHMEM int editFieldCount() {
    return static_cast<int>(core::state::sequencer::SequencerChordEditField::COUNT);
}

FLASHMEM int modeChoiceCount(bool rootContext) {
    return rootContext ? 2 : 3;
}

FLASHMEM int modeChoiceIndex(bool rootContext,
                             oc::note::sequencer::StepSequencerChordMode mode) {
    using oc::note::sequencer::StepSequencerChordMode;
    if (rootContext) {
        return mode == StepSequencerChordMode::Local ? 1 : 0;
    }

    switch (mode) {
        case StepSequencerChordMode::Single:
            return 1;
        case StepSequencerChordMode::Local:
            return 2;
        case StepSequencerChordMode::Inherit:
        default:
            return 0;
    }
}

FLASHMEM int quickChoiceCount(bool rootContext) {
    constexpr int maxVoices = oc::note::sequencer::StepSequencerChordSpec::MAX_VOICES;
    return rootContext ? maxVoices : maxVoices + 1;
}

FLASHMEM int quickChoiceIndex(
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    using oc::note::sequencer::StepSequencerChordMode;

    switch (chord.mode) {
        case StepSequencerChordMode::Inherit:
            return 0;
        case StepSequencerChordMode::Local:
            return chord.rootContext
                ? std::clamp<int>(
                      static_cast<int>(chord.spec.voiceCount) - 1,
                      1,
                      oc::note::sequencer::StepSequencerChordSpec::MAX_VOICES - 1
                  )
                : std::clamp<int>(
                      static_cast<int>(chord.spec.voiceCount),
                      2,
                      oc::note::sequencer::StepSequencerChordSpec::MAX_VOICES
                  );
        case StepSequencerChordMode::Single:
        default:
            return chord.rootContext ? 0 : 1;
    }
}

FLASHMEM void applyQuickChoice(core::state::sequencer::SequencerState& sequencer,
                               uint8_t step,
                               int choice,
                               bool scaleConstrained) {
    using oc::note::sequencer::StepSequencerChordMode;
    using oc::note::sequencer::StepSequencerChordSpec;

    const bool rootContext = core::state::sequencer::isRootContentView(sequencer);
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
    bool changed = false;

    if (choice <= 0) {
        changed = core::state::sequencer::clearNodeChordState(sequencer.pattern, nodeId);
    } else if (!rootContext && choice == 1) {
        changed = core::state::sequencer::setNodeChordMode(
            sequencer.pattern,
            nodeId,
            StepSequencerChordMode::Single
        );
    } else {
        StepSequencerChordSpec spec = semanticDefault(scaleConstrained);
        const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
        const auto* node = graph ? graph->stepNode(nodeId) : nullptr;
        if (node != nullptr &&
            node->has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL)) {
            spec = node->chordSpec;
        }
        spec.voiceCount = static_cast<uint8_t>(
            rootContext
                ? std::clamp<int>(choice + 1, 2, StepSequencerChordSpec::MAX_VOICES)
                : std::clamp<int>(choice, 2, StepSequencerChordSpec::MAX_VOICES)
        );
        changed = core::state::sequencer::setNodeChordSpec(sequencer.pattern, nodeId, spec);
    }

    if (changed) {
        sequencer.invalidateVariationTelemetry();
    }
}

FLASHMEM bool applyModeChoice(core::state::sequencer::SequencerState& sequencer,
                              uint8_t step,
                              int choice,
                              oc::note::sequencer::StepSequencerChordSpec specForLocal,
                              bool scaleConstrained) {
    using oc::note::sequencer::StepSequencerChordMode;

    const bool rootContext = core::state::sequencer::isRootContentView(sequencer);
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    const auto* node = graph ? graph->stepNode(nodeId) : nullptr;
    if (node == nullptr ||
        !node->has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL)) {
        specForLocal = semanticDefault(scaleConstrained);
    }
    specForLocal.clamp();
    bool changed = false;

    if (rootContext) {
        changed = choice <= 0
            ? core::state::sequencer::clearNodeChordState(sequencer.pattern, nodeId)
            : core::state::sequencer::setNodeChordSpec(
                  sequencer.pattern,
                  nodeId,
                  specForLocal
              );
    } else if (choice <= 0) {
        changed = core::state::sequencer::clearNodeChordState(sequencer.pattern, nodeId);
    } else if (choice == 1) {
        changed = core::state::sequencer::setNodeChordMode(
            sequencer.pattern,
            nodeId,
            StepSequencerChordMode::Single
        );
    } else {
        changed = core::state::sequencer::setNodeChordSpec(
            sequencer.pattern,
            nodeId,
            specForLocal
        );
    }

    if (changed) {
        sequencer.invalidateVariationTelemetry();
    }
    return changed;
}

FLASHMEM bool applySpecField(core::state::sequencer::SequencerState& sequencer,
                             uint8_t step,
                             core::state::sequencer::SequencerChordEditField field,
                             oc::note::sequencer::StepSequencerChordSpec spec,
                             bool scaleConstrained,
                             float normalized) {
    using Field = core::state::sequencer::SequencerChordEditField;
    using Spec = oc::note::sequencer::StepSequencerChordSpec;

    switch (field) {
        case Field::HARMONY: {
            const uint8_t count = oc::note::sequencer::chordHarmonyChoiceCount(
                scaleConstrained
            );
            const auto harmony = oc::note::sequencer::chordHarmonyForChoice(
                static_cast<uint8_t>(input_utils::normalizedToIndex(normalized, count)),
                scaleConstrained
            );
            spec.setHarmony(harmony);
            spec.voiceCount = std::max<uint8_t>(
                spec.voiceCount,
                oc::note::sequencer::recommendedChordVoiceCount(harmony)
            );
            break;
        }
        case Field::VOICES:
            spec.voiceCount = voiceCountFromNormalized(normalized);
            if (spec.isSemantic() && spec.inversion() >= spec.voiceCount) {
                spec.setInversion(static_cast<uint8_t>(spec.voiceCount - 1U));
            }
            break;
        case Field::INVERSION:
            ensureSemantic(spec, scaleConstrained);
            spec.setInversion(static_cast<uint8_t>(
                input_utils::normalizedToInclusiveInt(
                    normalized,
                    static_cast<int>(spec.voiceCount) - 1
                )
            ));
            break;
        case Field::VOICING:
            ensureSemantic(spec, scaleConstrained);
            spec.setVoicing(static_cast<oc::note::sequencer::StepSequencerChordVoicing>(
                input_utils::normalizedToInclusiveInt(
                    normalized,
                    static_cast<int>(
                        oc::note::sequencer::StepSequencerChordVoicing::Count
                    ) - 1
                )
            ));
            break;
        case Field::STRUM:
            spec.strum = signedFromNormalized(
                normalized,
                Spec::MIN_STRUM,
                Spec::MAX_STRUM
            );
            break;
        case Field::VELOCITY_CONTOUR:
            spec.velocityCurve = signedFromNormalized(
                normalized,
                Spec::MIN_VELOCITY_CURVE,
                Spec::MAX_VELOCITY_CURVE
            );
            break;
        case Field::MODE:
        case Field::COUNT:
        default:
            return false;
    }

    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
    const bool changed = core::state::sequencer::setNodeChordSpec(
        sequencer.pattern,
        nodeId,
        spec
    );
    if (changed) {
        sequencer.invalidateVariationTelemetry();
    }
    return changed;
}

FLASHMEM bool resetSpecField(core::state::sequencer::SequencerState& sequencer,
                             uint8_t step,
                             core::state::sequencer::SequencerChordEditField field,
                             bool scaleConstrained) {
    using Field = core::state::sequencer::SequencerChordEditField;
    using Spec = oc::note::sequencer::StepSequencerChordSpec;

    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
    if (field == Field::MODE) {
        const bool changed = core::state::sequencer::clearNodeChordState(
            sequencer.pattern,
            nodeId
        );
        if (changed) {
            sequencer.invalidateVariationTelemetry();
        }
        return changed;
    }

    const auto chord = core::state::sequencer::resolveStepChordUiState(sequencer, step);
    if (chord.mode != oc::note::sequencer::StepSequencerChordMode::Local) {
        return false;
    }

    auto spec = chord.spec;
    const Spec defaults = semanticDefault(scaleConstrained);
    switch (field) {
        case Field::HARMONY:
            spec.setHarmony(defaults.harmony());
            spec.voiceCount = std::max<uint8_t>(
                spec.voiceCount,
                oc::note::sequencer::recommendedChordVoiceCount(defaults.harmony())
            );
            break;
        case Field::VOICES:
            spec.voiceCount = defaults.voiceCount;
            if (spec.isSemantic() && spec.inversion() >= spec.voiceCount) {
                spec.setInversion(static_cast<uint8_t>(spec.voiceCount - 1U));
            }
            break;
        case Field::INVERSION:
            ensureSemantic(spec, scaleConstrained);
            spec.setInversion(0);
            break;
        case Field::VOICING:
            ensureSemantic(spec, scaleConstrained);
            spec.setVoicing(oc::note::sequencer::StepSequencerChordVoicing::Close);
            break;
        case Field::STRUM:
            spec.strum = defaults.strum;
            break;
        case Field::VELOCITY_CONTOUR:
            spec.velocityCurve = defaults.velocityCurve;
            break;
        case Field::MODE:
        case Field::COUNT:
        default:
            return false;
    }

    const bool changed = core::state::sequencer::setNodeChordSpec(
        sequencer.pattern,
        nodeId,
        spec
    );
    if (changed) {
        sequencer.invalidateVariationTelemetry();
    }
    return changed;
}

FLASHMEM float voiceCountToNormalized(uint8_t voiceCount) {
    using Spec = oc::note::sequencer::StepSequencerChordSpec;
    constexpr int minVoices = 2;
    constexpr int maxVoices = Spec::MAX_VOICES;
    const int clamped = std::clamp<int>(voiceCount, minVoices, maxVoices);
    return input_utils::indexToNormalized(clamped - minVoices, (maxVoices - minVoices) + 1);
}

FLASHMEM float signedToNormalized(int value, int minValue, int maxValue) {
    const int clamped = std::clamp(value, minValue, maxValue);
    return input_utils::indexToNormalized(clamped - minValue, (maxValue - minValue) + 1);
}

}  // namespace core::handler::sequencer::chord_edit_ops
