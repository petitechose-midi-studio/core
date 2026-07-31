#include "SequencerChordEditOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "SequencerChordEditOpsInternal.hpp"
#include "SequencerInputUtils.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::handler::sequencer::chord_edit_ops {
namespace detail {

FLASHMEM Basis contextBasis(bool scaleBased) {
    return scaleBased ? Basis::ScaleDegrees : Basis::ChromaticSemitones;
}

FLASHMEM Spec semanticDefault(bool scaleBased) {
    const auto harmony = oc::note::sequencer::defaultChordHarmony(scaleBased);
    return Spec::semantic(
        harmony,
        oc::note::sequencer::recommendedChordVoiceCount(harmony),
        oc::note::sequencer::StepSequencerChordVoicing::Close,
        0,
        contextBasis(scaleBased)
    );
}

FLASHMEM void alignFormulaContext(Spec& spec, bool scaleBased) {
    spec.setIntervalBasis(contextBasis(scaleBased));
    spec.clamp();
}

FLASHMEM bool commitSpec(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    Spec spec
) {
    spec.clamp();
    const auto nodeId =
        core::state::sequencer::activeContentStepNodeId(sequencer, step);
    const bool changed = core::state::sequencer::setAuthoringNodeChordSpec(
        sequencer,
        nodeId,
        spec
    );
    if (changed) {
        sequencer.invalidateVariationTelemetry();
        core::state::sequencer::notifyStepContentDraftMutation(sequencer);
    }
    return changed;
}

}  // namespace detail

namespace {

namespace input_utils = core::handler::sequencer::input_utils;

using Spec = oc::note::sequencer::StepSequencerChordSpec;

FLASHMEM int8_t signedFromNormalized(
    float normalized,
    int minValue,
    int maxValue
) {
    const int index =
        input_utils::normalizedToInclusiveInt(normalized, maxValue - minValue);
    return static_cast<int8_t>(minValue + index);
}

FLASHMEM bool sameSpec(const Spec& lhs, const Spec& rhs) {
    return oc::note::sequencer::chordSpecsEqual(lhs, rhs);
}

FLASHMEM bool captureNodeSnapshot(
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t nodeId,
    core::state::sequencer::SequencerChordAuthoringSnapshot& snapshot
) {
    snapshot.reset();
    const auto* graph = core::state::sequencer::graphView(
        core::state::sequencer::authoringPattern(sequencer)
    );
    const auto* node = graph ? graph->stepNode(nodeId) : nullptr;
    snapshot.nodeId = nodeId;
    if (node != nullptr) {
        snapshot.modePresent = node->has(
            oc::note::sequencer::STEP_NODE_CHORD_MODE
        );
        snapshot.localPresent = node->has(
            oc::note::sequencer::STEP_NODE_CHORD_LOCAL
        );
        snapshot.mode = node->chordMode;
        snapshot.spec = node->chordSpec;
    }
    const bool draftResolved =
        core::state::sequencer::resolveStepContentDraftChord(
        sequencer,
        nodeId,
        snapshot.modePresent,
        snapshot.localPresent,
        snapshot.mode,
        snapshot.spec
    );
    if (node == nullptr && !draftResolved) return false;
    snapshot.spec.clamp();
    snapshot.valid = true;
    return true;
}

FLASHMEM bool sameSnapshot(
    const core::state::sequencer::SequencerChordAuthoringSnapshot& lhs,
    const core::state::sequencer::SequencerChordAuthoringSnapshot& rhs
) {
    return lhs.valid == rhs.valid &&
           lhs.nodeId == rhs.nodeId &&
           lhs.modePresent == rhs.modePresent &&
           lhs.localPresent == rhs.localPresent &&
           lhs.mode == rhs.mode &&
           sameSpec(lhs.spec, rhs.spec);
}

}  // namespace

using detail::commitSpec;
using detail::contextBasis;
using detail::alignFormulaContext;
using detail::semanticDefault;

FLASHMEM int editFieldCount() {
    return static_cast<int>(
        core::state::sequencer::SequencerChordEditField::COUNT
    );
}

FLASHMEM int modeChoiceCount(bool rootContext) {
    return rootContext ? 2 : 3;
}

FLASHMEM int modeChoiceIndex(
    bool rootContext,
    oc::note::sequencer::StepSequencerChordMode mode
) {
    using ChordMode = oc::note::sequencer::StepSequencerChordMode;
    if (rootContext) return mode == ChordMode::Local ? 1 : 0;
    switch (mode) {
        case ChordMode::Single: return 1;
        case ChordMode::Local: return 2;
        case ChordMode::Inherit:
        default: return 0;
    }
}

FLASHMEM core::state::sequencer::SequencerChordSourceChoice
sourceChoiceForMode(
    bool rootContext,
    oc::note::sequencer::StepSequencerChordMode mode
) {
    using Choice =
        core::state::sequencer::SequencerChordSourceChoice;
    using ChordMode = oc::note::sequencer::StepSequencerChordMode;
    if (mode == ChordMode::Local) return Choice::LOCAL_CHORD;
    if (!rootContext && mode == ChordMode::Inherit) {
        return Choice::PARENT_CHORD;
    }
    return Choice::SINGLE_NOTE;
}

FLASHMEM int sourceChoiceCount(bool rootContext) {
    return rootContext ? 2 : 3;
}

FLASHMEM int sourceChoiceIndex(
    bool rootContext,
    core::state::sequencer::SequencerChordSourceChoice choice
) {
    using Choice =
        core::state::sequencer::SequencerChordSourceChoice;
    if (!rootContext) return static_cast<int>(choice);
    return choice == Choice::LOCAL_CHORD ? 1 : 0;
}

FLASHMEM core::state::sequencer::SequencerChordSourceChoice
sourceChoiceForIndex(bool rootContext, int index) {
    using Choice =
        core::state::sequencer::SequencerChordSourceChoice;
    if (rootContext) {
        return index <= 0 ? Choice::SINGLE_NOTE : Choice::LOCAL_CHORD;
    }
    if (index <= 0) return Choice::PARENT_CHORD;
    if (index == 1) return Choice::SINGLE_NOTE;
    return Choice::LOCAL_CHORD;
}

FLASHMEM int quickChoiceCount(bool rootContext) {
    return modeChoiceCount(rootContext);
}

FLASHMEM int quickChoiceIndex(
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    return modeChoiceIndex(chord.rootContext, chord.mode);
}

FLASHMEM void applyQuickChoice(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    int choice,
    bool scaleBased
) {
    const auto current =
        core::state::sequencer::resolveStepChordUiState(sequencer, step);
    const Spec local = current.valid && current.mode ==
            oc::note::sequencer::StepSequencerChordMode::Local
        ? current.spec
        : semanticDefault(scaleBased);
    (void)applyModeChoice(
        sequencer,
        step,
        choice,
        local,
        scaleBased
    );
}

FLASHMEM bool applyModeChoice(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    int choice,
    Spec specForLocal,
    bool scaleBased
) {
    using ChordMode = oc::note::sequencer::StepSequencerChordMode;

    const bool rootContext =
        core::state::sequencer::isRootContentView(sequencer);
    const auto nodeId =
        core::state::sequencer::activeContentStepNodeId(sequencer, step);
    specForLocal.setIntervalBasis(contextBasis(scaleBased));
    specForLocal.clamp();

    bool changed = false;
    if (rootContext) {
        changed = choice <= 0
            ? core::state::sequencer::clearAuthoringNodeChordState(
                  sequencer,
                  nodeId
              )
            : core::state::sequencer::setAuthoringNodeChordSpec(
                  sequencer,
                  nodeId,
                  specForLocal
              );
    } else if (choice <= 0) {
        changed = core::state::sequencer::clearAuthoringNodeChordState(
            sequencer,
            nodeId
        );
    } else if (choice == 1) {
        changed = core::state::sequencer::setAuthoringNodeChordMode(
            sequencer,
            nodeId,
            ChordMode::Single
        );
    } else {
        changed = core::state::sequencer::setAuthoringNodeChordSpec(
            sequencer,
            nodeId,
            specForLocal
        );
    }

    if (changed) {
        sequencer.invalidateVariationTelemetry();
        core::state::sequencer::notifyStepContentDraftMutation(sequencer);
    }
    return changed;
}

FLASHMEM bool applySourceChoice(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    core::state::sequencer::SequencerChordSourceChoice choice,
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    return applyModeChoice(
        sequencer,
        step,
        sourceChoiceIndex(chord.rootContext, choice),
        chord.spec,
        chord.intervalsUseScaleDegrees
    );
}

FLASHMEM bool createDefaultLocalChord(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    bool scaleBased
) {
    const auto nodeId =
        core::state::sequencer::activeContentStepNodeId(sequencer, step);
    const bool changed =
        core::state::sequencer::setAuthoringNodeChordSpec(
            sequencer,
            nodeId,
            semanticDefault(scaleBased)
        );
    if (changed) {
        sequencer.invalidateVariationTelemetry();
        core::state::sequencer::notifyStepContentDraftMutation(sequencer);
    }
    return changed;
}

FLASHMEM bool captureAuthoringSnapshot(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    core::state::sequencer::SequencerChordAuthoringSnapshot& snapshot
) {
    return captureNodeSnapshot(
        sequencer,
        core::state::sequencer::activeContentStepNodeId(sequencer, step),
        snapshot
    );
}

FLASHMEM bool restoreAuthoringSnapshot(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerChordAuthoringSnapshot& snapshot
) {
    if (!snapshot.valid ||
        snapshot.nodeId ==
            core::state::sequencer::SequencerChordAuthoringSnapshot::
                INVALID_NODE) {
        return false;
    }

    core::state::sequencer::SequencerChordAuthoringSnapshot current{};
    if (captureNodeSnapshot(sequencer, snapshot.nodeId, current) &&
        sameSnapshot(current, snapshot)) {
        return false;
    }

    bool changed =
        core::state::sequencer::clearAuthoringNodeChordState(
            sequencer,
            snapshot.nodeId
        );
    if (snapshot.localPresent) {
        changed =
            core::state::sequencer::setAuthoringNodeChordSpec(
                sequencer,
                snapshot.nodeId,
                snapshot.spec
            ) || changed;
    } else if (snapshot.modePresent) {
        changed =
            core::state::sequencer::setAuthoringNodeChordMode(
                sequencer,
                snapshot.nodeId,
                snapshot.mode
            ) || changed;
    }
    if (changed) {
        sequencer.invalidateVariationTelemetry();
        core::state::sequencer::notifyStepContentDraftMutation(sequencer);
    }
    return changed;
}

FLASHMEM bool applySpecField(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    core::state::sequencer::SequencerChordEditField field,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    float normalized
) {
    using Field = core::state::sequencer::SequencerChordEditField;
    (void)scaleSettings;

    auto spec = chord.spec;
    const bool scaleBased = chord.intervalsUseScaleDegrees;
    switch (field) {
        case Field::SHAPE: {
            const uint8_t count =
                oc::note::sequencer::chordPresetChoiceCount(scaleBased);
            const auto harmony =
                oc::note::sequencer::chordPresetForChoice(
                    static_cast<uint8_t>(
                        input_utils::normalizedToIndex(normalized, count)
                    ),
                    scaleBased
                );
            alignFormulaContext(spec, scaleBased);
            spec.setIntervalBasis(contextBasis(scaleBased));
            spec.setHarmony(harmony);
            spec.setVoices(
                oc::note::sequencer::recommendedChordVoiceCount(harmony)
            );
            if (spec.inversion() >= spec.voices()) {
                spec.setInversion(
                    static_cast<uint8_t>(spec.voices() - 1U)
                );
            }
            break;
        }
        case Field::INVERSION:
            alignFormulaContext(spec, scaleBased);
            spec.setInversion(static_cast<uint8_t>(
                input_utils::normalizedToInclusiveInt(
                    normalized,
                    static_cast<int>(spec.voices()) - 1
                )
            ));
            break;
        case Field::VOICING:
            alignFormulaContext(spec, scaleBased);
            spec.setVoicing(
                static_cast<oc::note::sequencer::StepSequencerChordVoicing>(
                    input_utils::normalizedToInclusiveInt(
                        normalized,
                        static_cast<int>(
                            oc::note::sequencer::StepSequencerChordVoicing::Count
                        ) - 1
                    )
                )
            );
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
        case Field::FORMULA:
        case Field::PITCH_CONTEXT:
        case Field::COUNT:
        default:
            return false;
    }
    return commitSpec(sequencer, step, spec);
}

FLASHMEM bool resetSpecField(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    core::state::sequencer::SequencerChordEditField field,
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    using Field = core::state::sequencer::SequencerChordEditField;

    auto spec = chord.spec;
    const bool scaleBased = chord.intervalsUseScaleDegrees;
    const Spec defaults = semanticDefault(scaleBased);
    switch (field) {
        case Field::SHAPE:
        case Field::FORMULA:
            spec.setIntervalBasis(contextBasis(scaleBased));
            spec.setHarmony(defaults.harmony());
            spec.setVoices(defaults.voices());
            if (spec.inversion() >= spec.voices()) {
                spec.setInversion(static_cast<uint8_t>(spec.voices() - 1U));
            }
            break;
        case Field::INVERSION:
            alignFormulaContext(spec, scaleBased);
            spec.setInversion(0);
            break;
        case Field::VOICING:
            alignFormulaContext(spec, scaleBased);
            spec.setVoicing(
                oc::note::sequencer::StepSequencerChordVoicing::Close
            );
            break;
        case Field::STRUM:
            spec.strum = 0;
            break;
        case Field::VELOCITY_CONTOUR:
            spec.velocityCurve = 0;
            break;
        case Field::PITCH_CONTEXT:
        case Field::COUNT:
        default:
            return false;
    }
    return commitSpec(sequencer, step, spec);
}

FLASHMEM float signedToNormalized(int value, int minValue, int maxValue) {
    const int clamped = std::clamp(value, minValue, maxValue);
    return input_utils::indexToNormalized(
        clamped - minValue,
        (maxValue - minValue) + 1
    );
}

}  // namespace core::handler::sequencer::chord_edit_ops
