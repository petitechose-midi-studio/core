#include "SequencerChordFormulaEditOps.hpp"

#include <algorithm>
#include <array>

#include <config/PlatformCompat.hpp>

#include "SequencerChordEditOpsInternal.hpp"
#include "SequencerInputUtils.hpp"

namespace core::handler::sequencer::chord_edit_ops {
namespace {

namespace input_utils = core::handler::sequencer::input_utils;

using Harmony = oc::note::sequencer::StepSequencerChordHarmony;
using Spec = oc::note::sequencer::StepSequencerChordSpec;

FLASHMEM bool findScaleDegree(
    uint8_t root,
    uint8_t targetPitchClass,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    uint8_t& degree
) {
    for (uint8_t candidate = 0;
         candidate <= Spec::MAX_CUSTOM_INTERVAL;
         ++candidate) {
        const uint8_t note = oc::note::sequencer::moveByScaleDegrees(
            root,
            static_cast<int8_t>(candidate),
            scaleSettings
        );
        if ((note % 12U) == targetPitchClass) {
            degree = candidate;
            return true;
        }
    }
    return false;
}

FLASHMEM bool copyPreviewPitchClassesToCustom(
    Spec& spec,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    bool scaleBased,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    const auto& analysis = chord.preview.analysis;
    if (!chord.preview.valid ||
        analysis.pitchClassCount < 2 ||
        analysis.pitchClassCount > Spec::MAX_CUSTOM_VOICES) {
        return false;
    }

    std::array<uint8_t, Spec::MAX_CUSTOM_VOICES> intervals{};
    uint8_t intervalCount = 0;
    const uint8_t rootPitchClass =
        static_cast<uint8_t>(chord.preview.rootNote % 12U);
    for (uint8_t interval = 0; interval < 12U; ++interval) {
        const uint8_t target =
            static_cast<uint8_t>((rootPitchClass + interval) % 12U);
        const bool present = std::any_of(
            analysis.pitchClasses.begin(),
            analysis.pitchClasses.begin() + analysis.pitchClassCount,
            [target](uint8_t pitchClass) { return pitchClass == target; }
        );
        if (!present) continue;
        intervals[intervalCount++] = interval;
    }
    if (intervalCount < 2 || intervals[0] != 0) return false;

    uint8_t previous = 0;
    for (uint8_t i = 1; i < intervalCount; ++i) {
        uint8_t interval = intervals[i];
        if (scaleBased) {
            const uint8_t targetPitchClass = static_cast<uint8_t>(
                (rootPitchClass + interval) % 12U
            );
            if (!findScaleDegree(
                    chord.preview.rootNote,
                    targetPitchClass,
                    scaleSettings,
                    interval
                )) {
                return false;
            }
        }
        if (interval <= previous ||
            interval > Spec::MAX_CUSTOM_INTERVAL) {
            return false;
        }
        intervals[i] = interval;
        previous = interval;
    }

    const int8_t strum = spec.strum;
    const int8_t velocityContour = spec.velocityCurve;
    spec = Spec::semantic(
        Harmony::Custom,
        intervalCount,
        oc::note::sequencer::StepSequencerChordVoicing::Close,
        0,
        detail::contextBasis(scaleBased)
    );
    spec.strum = strum;
    spec.velocityCurve = velocityContour;
    spec.setCustomIntervals(intervals);
    return true;
}

FLASHMEM bool convertSemanticFormulaToCustom(
    Spec& spec,
    bool scaleBased
) {
    const auto formula =
        oc::note::sequencer::resolveChordFormula(spec, scaleBased);
    if (!formula.valid || formula.count < 2) return false;

    const uint8_t count = std::clamp<uint8_t>(
        formula.count,
        2U,
        Spec::MAX_CUSTOM_VOICES
    );
    std::array<uint8_t, Spec::MAX_CUSTOM_VOICES> intervals{};
    for (uint8_t i = 1; i < count; ++i) {
        intervals[i] = static_cast<uint8_t>(std::clamp<int16_t>(
            formula.intervals[i],
            1,
            Spec::MAX_CUSTOM_INTERVAL
        ));
    }
    auto converted = Spec::semantic(
        Harmony::Custom,
        count,
        spec.voicing(),
        std::min<uint8_t>(spec.inversion(), count - 1U),
        detail::contextBasis(scaleBased)
    );
    converted.strum = spec.strum;
    converted.velocityCurve = spec.velocityCurve;
    converted.setCustomIntervals(intervals);
    spec = converted;
    return true;
}

FLASHMEM void convertToCustom(
    Spec& spec,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    bool scaleBased,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (spec.isCustom()) {
        spec.setIntervalBasis(detail::contextBasis(scaleBased));
        spec.clamp();
        return;
    }
    if (convertSemanticFormulaToCustom(spec, scaleBased)) return;
    if (copyPreviewPitchClassesToCustom(
            spec,
            chord,
            scaleBased,
            scaleSettings
        )) {
        return;
    }
    spec = Spec::semantic(
        Harmony::Custom,
        3,
        oc::note::sequencer::StepSequencerChordVoicing::Close,
        0,
        detail::contextBasis(scaleBased)
    );
}

FLASHMEM Spec formulaEditingSpec(
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    auto spec = chord.spec;
    if (spec.isCustom()) {
        spec.setIntervalBasis(
            detail::contextBasis(chord.intervalsUseScaleDegrees)
        );
        return spec;
    }
    if (convertSemanticFormulaToCustom(
            spec,
            chord.intervalsUseScaleDegrees
        )) {
        return spec;
    }
    return Spec::semantic(
        Harmony::Custom,
        3,
        oc::note::sequencer::StepSequencerChordVoicing::Close,
        0,
        detail::contextBasis(chord.intervalsUseScaleDegrees)
    );
}

FLASHMEM void customIntervalBounds(
    const Spec& spec,
    uint8_t voiceIndex,
    uint8_t& minimum,
    uint8_t& maximum
) {
    minimum =
        static_cast<uint8_t>(spec.customInterval(voiceIndex - 1U) + 1U);
    maximum = Spec::MAX_CUSTOM_INTERVAL;
    if (voiceIndex + 1U < spec.voices()) {
        const uint8_t next = spec.customInterval(voiceIndex + 1U);
        if (next > 0) maximum = static_cast<uint8_t>(next - 1U);
    }
    if (minimum > maximum) minimum = maximum;
}

FLASHMEM uint8_t customDefaultInterval(
    uint8_t voiceIndex,
    bool scaleBased
) {
    constexpr uint8_t SCALE_DEFAULTS[] = {
        0U, 2U, 4U, 6U, 8U, 10U, 12U, 14U,
    };
    constexpr uint8_t CHROMATIC_DEFAULTS[] = {
        0U, 3U, 7U, 10U, 14U, 15U, 19U, 22U,
    };
    return scaleBased
        ? SCALE_DEFAULTS[voiceIndex]
        : CHROMATIC_DEFAULTS[voiceIndex];
}

FLASHMEM std::array<uint8_t, Spec::MAX_CUSTOM_VOICES>
captureCustomIntervals(const Spec& spec) {
    std::array<uint8_t, Spec::MAX_CUSTOM_VOICES> intervals{};
    for (uint8_t voice = 1U;
         voice < spec.voices() && voice < intervals.size();
         ++voice) {
        intervals[voice] = spec.customInterval(voice);
    }
    return intervals;
}

}  // namespace

FLASHMEM bool applyFormulaVoice(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    uint8_t voiceIndex,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    float normalized
) {
    if (voiceIndex == 0 ||
        voiceIndex >= Spec::MAX_CUSTOM_VOICES) {
        return false;
    }

    auto spec = chord.spec;
    convertToCustom(
        spec,
        chord,
        chord.intervalsUseScaleDegrees,
        scaleSettings
    );

    if (voiceIndex >= spec.voices()) return false;

    uint8_t minimum = 0;
    uint8_t maximum = 0;
    customIntervalBounds(spec, voiceIndex, minimum, maximum);
    const int choice = input_utils::normalizedToIndex(
        normalized,
        static_cast<int>((maximum - minimum) + 1U)
    );
    auto intervals = captureCustomIntervals(spec);
    intervals[voiceIndex] = static_cast<uint8_t>(minimum + choice);
    spec.setCustomIntervals(intervals);
    spec = oc::note::sequencer::canonicalizeChordSpec(
        spec,
        chord.intervalsUseScaleDegrees
    );
    return detail::commitSpec(sequencer, step, spec);
}

FLASHMEM bool addFormulaVoice(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    auto spec = chord.spec;
    convertToCustom(
        spec,
        chord,
        chord.intervalsUseScaleDegrees,
        scaleSettings
    );
    const uint8_t count = spec.voices();
    if (count >= Spec::MAX_CUSTOM_VOICES) return false;

    const uint8_t previous = spec.customInterval(count - 1U);
    if (previous >= Spec::MAX_CUSTOM_INTERVAL) return false;

    auto intervals = captureCustomIntervals(spec);
    intervals[count] = static_cast<uint8_t>(previous + 1U);
    spec.setVoices(static_cast<uint8_t>(count + 1U));
    spec.setCustomIntervals(intervals);
    spec = oc::note::sequencer::canonicalizeChordSpec(
        spec,
        chord.intervalsUseScaleDegrees
    );
    return detail::commitSpec(sequencer, step, spec);
}

FLASHMEM bool applyFormulaVoiceRemoveIntent(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    uint8_t voiceIndex,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (voiceIndex == 0 ||
        voiceIndex >= Spec::MAX_CUSTOM_VOICES) {
        return false;
    }

    auto spec = chord.spec;
    convertToCustom(
        spec,
        chord,
        chord.intervalsUseScaleDegrees,
        scaleSettings
    );
    if (voiceIndex >= spec.voices()) return false;

    if (voiceIndex > 1U) {
        const uint8_t oldCount = spec.voices();
        auto intervals = captureCustomIntervals(spec);
        for (uint8_t voice = voiceIndex;
             voice + 1U < oldCount;
             ++voice) {
            intervals[voice] = intervals[voice + 1U];
        }
        intervals[oldCount - 1U] = 0U;
        spec.setVoices(static_cast<uint8_t>(oldCount - 1U));
        spec.setCustomIntervals(intervals);
        if (spec.inversion() >= spec.voices()) {
            spec.setInversion(static_cast<uint8_t>(spec.voices() - 1U));
        }
        spec = oc::note::sequencer::canonicalizeChordSpec(
            spec,
            chord.intervalsUseScaleDegrees
        );
        return detail::commitSpec(sequencer, step, spec);
    }

    uint8_t minimum = 0;
    uint8_t maximum = 0;
    customIntervalBounds(spec, voiceIndex, minimum, maximum);
    auto intervals = captureCustomIntervals(spec);
    intervals[voiceIndex] = std::clamp<uint8_t>(
        customDefaultInterval(
            voiceIndex,
            chord.intervalsUseScaleDegrees
        ),
        minimum,
        maximum
    );
    spec.setCustomIntervals(intervals);
    spec = oc::note::sequencer::canonicalizeChordSpec(
        spec,
        chord.intervalsUseScaleDegrees
    );
    return detail::commitSpec(sequencer, step, spec);
}

FLASHMEM uint8_t formulaVoiceCount(
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    return formulaEditingSpec(chord).voices();
}

FLASHMEM bool formulaAddAvailable(
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    const auto spec = formulaEditingSpec(chord);
    const uint8_t count = spec.voices();
    return count < Spec::MAX_CUSTOM_VOICES &&
           spec.customInterval(count - 1U) < Spec::MAX_CUSTOM_INTERVAL;
}

FLASHMEM float formulaVoiceToNormalized(
    const core::state::sequencer::SequencerStepChordUiState& chord,
    uint8_t voiceIndex
) {
    if (voiceIndex == 0 ||
        voiceIndex >= Spec::MAX_CUSTOM_VOICES) {
        return 0.0f;
    }

    const auto spec = formulaEditingSpec(chord);
    if (voiceIndex >= spec.voices()) return 0.0f;
    uint8_t minimum = 0;
    uint8_t maximum = 0;
    customIntervalBounds(spec, voiceIndex, minimum, maximum);
    const int count = static_cast<int>((maximum - minimum) + 1U);
    const int index =
        static_cast<int>(spec.customInterval(voiceIndex) - minimum);
    return input_utils::indexToNormalized(index, count);
}

FLASHMEM uint8_t formulaVoiceChoiceCount(
    const core::state::sequencer::SequencerStepChordUiState& chord,
    uint8_t voiceIndex
) {
    if (voiceIndex == 0 ||
        voiceIndex >= Spec::MAX_CUSTOM_VOICES) {
        return 1;
    }
    const auto spec = formulaEditingSpec(chord);
    if (voiceIndex >= spec.voices()) return 1;
    uint8_t minimum = 0;
    uint8_t maximum = 0;
    customIntervalBounds(spec, voiceIndex, minimum, maximum);
    return static_cast<uint8_t>(
        (maximum - minimum) + 1U
    );
}

}  // namespace core::handler::sequencer::chord_edit_ops
