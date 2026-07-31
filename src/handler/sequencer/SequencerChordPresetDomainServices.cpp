#include "handler/sequencer/SequencerChordPresetDomainServices.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "persistence/ProductFileService.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerChordProjectionWorkspace.hpp"
#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::handler {
namespace {

using Asset = oc::note::sequencer::StepSequencerChordPreset;
using Basis = oc::note::sequencer::StepSequencerChordIntervalBasis;
using ChordMode = oc::note::sequencer::StepSequencerChordMode;
using Compatibility =
    core::state::sequencer::SequencerChordPresetCompatibility;
using Descriptor =
    core::state::sequencer::SequencerChordPresetDescriptor;
using Harmony = oc::note::sequencer::StepSequencerChordHarmony;
using Spec = oc::note::sequencer::StepSequencerChordSpec;
using Target = core::state::sequencer::SequencerChordPresetTarget;
using Voicing = oc::note::sequencer::StepSequencerChordVoicing;

FLASHMEM SequencerChordPresetStatus statusFromFileError(
    oc::type::ErrorCode error
) {
    switch (error) {
        case oc::type::ErrorCode::STORAGE_CORRUPT:
            return SequencerChordPresetStatus::CORRUPT;
        case oc::type::ErrorCode::INVALID_STATE:
        case oc::type::ErrorCode::HARDWARE_INIT_FAILED:
        case oc::type::ErrorCode::HARDWARE_NOT_FOUND:
            return SequencerChordPresetStatus::STORAGE_UNAVAILABLE;
        default:
            return SequencerChordPresetStatus::FAILED;
    }
}

template <size_t N>
FLASHMEM void copyText(char (&out)[N], const char* text) {
    std::strncpy(out, text ? text : "", N - 1U);
    out[N - 1U] = '\0';
}

FLASHMEM bool sameScale(
    oc::note::sequencer::StepSequencerScaleSettings lhs,
    oc::note::sequencer::StepSequencerScaleSettings rhs
) {
    lhs.clamp();
    rhs.clamp();
    return lhs.root == rhs.root &&
           lhs.type == rhs.type &&
           lhs.mode == rhs.mode;
}

FLASHMEM oc::note::sequencer::StepSequencerScaleSettings
effectiveScale(const core::state::CoreState& state) {
    return core::state::sequencer::resolveEffectiveScaleSettings(
        state.sequencerTracks.projectScaleSettings(),
        core::state::sequencer::authoringPattern(
            state.sequencer
        ).scalePolicy,
        core::state::sequencer::authoringPattern(
            state.sequencer
        ).scaleOverride
    );
}

FLASHMEM const char* shapeLabel(Harmony harmony) {
    switch (harmony) {
        case Harmony::DiatonicTriad: return "Triad";
        case Harmony::DiatonicSeventh: return "Seventh";
        case Harmony::Suspended: return "Suspended";
        case Harmony::Quartal: return "Quartal";
        case Harmony::Major: return "Major";
        case Harmony::Minor: return "Minor";
        case Harmony::Diminished: return "Diminished";
        case Harmony::Augmented: return "Augmented";
        case Harmony::Sus2: return "Sus 2";
        case Harmony::Sus4: return "Sus 4";
        case Harmony::Dominant7: return "Dominant 7";
        case Harmony::Major7: return "Major 7";
        case Harmony::Minor7: return "Minor 7";
        case Harmony::Custom:
        case Harmony::Count:
        default:
            return "Custom";
    }
}

FLASHMEM const char* voicingLabel(Voicing voicing) {
    switch (voicing) {
        case Voicing::Open: return "Open";
        case Voicing::Wide: return "Wide";
        case Voicing::Close:
        default:
            return "Close";
    }
}

FLASHMEM void suggestedSemanticName(
    const Target& target,
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0U) return;
    const auto& formula = target.captureFormula;
    const uint8_t voices = formula.voices();
    if (target.sourceShapeHint != Harmony::Custom) {
        std::snprintf(
            out,
            outSize,
            "%s \xC2\xB7 %uv \xC2\xB7 %s",
            shapeLabel(target.sourceShapeHint),
            static_cast<unsigned>(voices),
            voicingLabel(formula.voicing())
        );
        return;
    }

    size_t written = 0U;
    for (uint8_t voice = 0; voice < voices; ++voice) {
        const int count = std::snprintf(
            out + written,
            outSize - written,
            voice == 0U ? "%u" : "-%u",
            static_cast<unsigned>(formula.customInterval(voice))
        );
        if (count <= 0 ||
            static_cast<size_t>(count) >= outSize - written) {
            break;
        }
        written += static_cast<size_t>(count);
    }
    if (written + 1U < outSize) {
        std::snprintf(
            out + written,
            outSize - written,
            " \xC2\xB7 %uv",
            static_cast<unsigned>(voices)
        );
    }
}

FLASHMEM bool makeExplicitProjection(
    const oc::note::sequencer::StepSequencerChordProjection& projection,
    bool targetUsesScaleDegrees,
    Spec& out
) {
    if (!projection.valid) return false;
    return oc::note::sequencer::makeExplicitChordPresetFormula(
        projection.spec,
        targetUsesScaleDegrees,
        out
    );
}

FLASHMEM Compatibility compatibilityForProjection(
    const oc::note::sequencer::StepSequencerChordProjection& projection
) {
    if (!projection.valid) {
        return Compatibility::BLOCKED_INCOMPATIBLE;
    }
    if (projection.adapted || projection.directionLimited ||
        projection.rangeLimited || !projection.exact) {
        return Compatibility::WARNING_ADAPTED;
    }
    return Compatibility::READY;
}

FLASHMEM bool inspectTarget(
    const core::state::CoreState& state,
    Target& target
) {
    const auto& sequencer = state.sequencer;
    if (!sequencer.stepEdit.visible.get() ||
        !sequencer.stepEdit.chordEditor.active.get() ||
        !sequencer.stepContentDraft.active.get()) {
        return false;
    }

    target.stepIndex = sequencer.stepEdit.stepIndex.get();
    target.nodeId =
        core::state::sequencer::activeContentStepNodeId(
            sequencer,
            target.stepIndex
        );
    target.scale = effectiveScale(state);
    const auto projection =
        core::state::sequencer::resolveActiveContentStepProjection(
            sequencer,
            target.stepIndex,
            target.scale
        );
    if (!projection.valid ||
        target.nodeId == Target::INVALID_NODE ||
        projection.nodeId != target.nodeId) {
        return false;
    }

    auto chord =
        core::state::sequencer::resolveStepChordUiState(
            sequencer,
            target.stepIndex
        );
    core::state::sequencer::resolveStepChordPreview(
        chord,
        projection,
        target.scale
    );
    target.rootNote = projection.note;
    target.velocity = projection.velocity;
    target.gate = projection.gate;
    target.nudge = projection.nudge;
    target.targetUsesScaleDegrees =
        core::state::sequencer::pitchContextUsesScaleDegrees(
            core::state::sequencer::authoringPattern(
                state.sequencer
            ).pitchEditMode,
            target.scale
        );
    target.sourceShapeHint = chord.preview.valid
        ? chord.preview.harmony
        : chord.spec.harmony();
    target.canSave =
        chord.preview.valid &&
        chord.preview.source !=
            oc::note::sequencer::StepSequencerChordSource::Single &&
        oc::note::sequencer::makeExplicitChordPresetFormula(
            chord.spec,
            target.targetUsesScaleDegrees,
            target.captureFormula
        );
    target.valid = true;
    return true;
}

}  // namespace

FLASHMEM SequencerChordPresetDomainServices::
SequencerChordPresetDomainServices(
    core::state::CoreState& state,
    core::persistence::ProductFileService& files
) : state_(&state),
    files_(&files) {}

FLASHMEM SequencerChordPresetDomainServices
SequencerChordPresetDomainServices::fromCoreState(
    core::state::CoreState& state,
    core::persistence::ProductFileService& files
) {
    return {state, files};
}

FLASHMEM SequencerChordPresetListResult
SequencerChordPresetDomainServices::listPresetsPage(
    Entry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    core::persistence::ChordPresetFilePageDirection direction
) const {
    OC_PERF_SCOPE(perfList, "persistence.chord-preset.list-page");
    SequencerChordPresetListResult result{};
    if (files_ == nullptr) {
        result.status =
            SequencerChordPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    core::persistence::ChordPresetFileStore store(*files_);
    const auto listed = store.listPage(
        entries,
        capacity,
        anchorExclusive,
        direction
    );
    if (!listed) {
        result.status = statusFromFileError(listed.error().code);
        result.fileError = listed.error().code;
        return result;
    }
    result.count = listed.value().count;
    result.truncated = listed.value().truncated;
    result.hasPrevious = listed.value().hasPrevious;
    result.hasNext = listed.value().hasNext;
    result.totalCount = listed.value().totalCount;
    OC_PERF_UNITS(perfList, result.totalCount, result.count);
    return result;
}

FLASHMEM SequencerChordPresetActionResult
SequencerChordPresetDomainServices::nextPresetId(
    char* out,
    size_t outSize
) const {
    SequencerChordPresetActionResult result{};
    if (files_ == nullptr) {
        result.status =
            SequencerChordPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    core::persistence::ChordPresetFileStore store(*files_);
    const auto next = store.nextPresetId(out, outSize);
    if (!next) {
        result.status = statusFromFileError(next.error().code);
        result.fileError = next.error().code;
        return result;
    }
    copyText(result.presetId, out);
    return result;
}

FLASHMEM Target
SequencerChordPresetDomainServices::captureTarget() const {
    Target target{};
    if (state_ != nullptr) (void)inspectTarget(*state_, target);
    return target;
}

FLASHMEM bool SequencerChordPresetDomainServices::targetMatches(
    const Target& target
) const {
    if (state_ == nullptr || !target.valid) return false;
    Target current{};
    if (!inspectTarget(*state_, current)) return false;
    return current.valid &&
           current.stepIndex == target.stepIndex &&
           current.nodeId == target.nodeId &&
           current.rootNote == target.rootNote &&
           current.targetUsesScaleDegrees ==
               target.targetUsesScaleDegrees &&
           sameScale(current.scale, target.scale);
}

FLASHMEM SequencerChordPresetInspectResult
SequencerChordPresetDomainServices::inspectPreset(
    const char* presetId,
    const Target& target,
    uint32_t generation
) const {
    OC_PERF_SCOPE(perfInspect, "persistence.chord-preset.inspect");
    SequencerChordPresetInspectResult result{};
    auto& descriptor = result.descriptor;
    // valid means that the descriptor represents the requested inspection,
    // not that the asset is loadable. Compatibility remains authoritative
    // for apply eligibility and supports an explicit error Detail.
    descriptor.valid = true;
    descriptor.generation = generation;
    copyText(descriptor.technicalId, presetId);

    if (state_ == nullptr || files_ == nullptr) {
        result.status =
            SequencerChordPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        descriptor.compatibility =
            Compatibility::STORAGE_UNAVAILABLE;
        return result;
    }
    if (!targetMatches(target)) {
        result.status = SequencerChordPresetStatus::STALE_TARGET;
        descriptor.compatibility = Compatibility::STALE_TARGET;
        return result;
    }

    core::persistence::ChordPresetFileStore store(*files_);
    Asset asset{};
    const auto loaded = store.load(presetId, asset);
    if (!loaded) {
        result.status = statusFromFileError(loaded.error().code);
        result.fileError = loaded.error().code;
        descriptor.compatibility =
            result.status ==
                    SequencerChordPresetStatus::STORAGE_UNAVAILABLE
                ? Compatibility::STORAGE_UNAVAILABLE
                : Compatibility::CORRUPT;
        return result;
    }

    copyText(descriptor.technicalId, asset.technicalId);
    copyText(descriptor.semanticName, asset.semanticName);
    OC_PERF_UNITS(perfInspect, asset.formula.voices(), 0U);
    descriptor.sourceShapeHint = asset.sourceShapeHint;
    descriptor.sourceFormula = asset.formula;
    descriptor.sourceBasis = asset.formula.intervalBasis();
    descriptor.targetBasis = target.targetUsesScaleDegrees
        ? Basis::ScaleDegrees
        : Basis::ChromaticSemitones;
    descriptor.previewKey = {
        .assetFingerprint =
            core::state::sequencer::
                sequencerChordPresetAssetFingerprint(asset),
        .targetHash =
            core::state::sequencer::
                sequencerChordPresetTargetHash(target),
    };
    descriptor.projection =
        oc::note::sequencer::projectChordSpec(
            asset.formula,
            asset.sourceScale,
            target.scale,
            asset.sourceRootPitchClass,
            target.rootNote,
            asset.formula.intervalBasis() == Basis::ScaleDegrees,
            target.targetUsesScaleDegrees,
            core::state::sequencer::
                sharedSequencerChordProjectionWorkspace()
        );
    descriptor.compatibility = compatibilityForProjection(
        descriptor.projection
    );
    if (!makeExplicitProjection(
            descriptor.projection,
            target.targetUsesScaleDegrees,
            descriptor.projectedFormula
        )) {
        result.status = SequencerChordPresetStatus::INCOMPATIBLE;
        descriptor.compatibility =
            Compatibility::BLOCKED_INCOMPATIBLE;
        return result;
    }

    descriptor.resolution =
        oc::note::sequencer::resolveStepChord(
            {
                .note = target.rootNote,
                .velocity = target.velocity,
                .gate = target.gate,
                .nudge = target.nudge,
            },
            target.scale,
            {
                .mode = ChordMode::Local,
                .local = descriptor.projectedFormula,
            },
            {},
            std::max<uint16_t>(1U, target.gate),
            target.targetUsesScaleDegrees
        );
    if (descriptor.resolution.count !=
        descriptor.projectedFormula.voices()) {
        result.status = SequencerChordPresetStatus::INCOMPATIBLE;
        descriptor.compatibility =
            Compatibility::BLOCKED_INCOMPATIBLE;
        return result;
    }
    result.status = SequencerChordPresetStatus::OK;
    return result;
}

FLASHMEM SequencerChordPresetActionResult
SequencerChordPresetDomainServices::savePreset(
    const char* presetId,
    const Target& target,
    bool allowOverwrite
) const {
    SequencerChordPresetActionResult result{};
    copyText(result.presetId, presetId);
    if (state_ == nullptr || files_ == nullptr) {
        result.status =
            SequencerChordPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    if (!targetMatches(target)) {
        result.status = SequencerChordPresetStatus::STALE_TARGET;
        return result;
    }
    Target current = captureTarget();
    if (!current.valid || !current.canSave) {
        result.status = SequencerChordPresetStatus::EMPTY;
        return result;
    }

    core::persistence::ChordPresetFileStore store(*files_);
    const auto existing = store.exists(presetId);
    if (!existing) {
        result.status = statusFromFileError(existing.error().code);
        result.fileError = existing.error().code;
        return result;
    }
    if (existing.value() && !allowOverwrite) {
        result.status = SequencerChordPresetStatus::COLLISION;
        return result;
    }

    char semanticName[Asset::SEMANTIC_NAME_SIZE]{};
    suggestedSemanticName(
        current,
        semanticName,
        sizeof(semanticName)
    );
    if (existing.value()) {
        Asset previous{};
        if (store.load(presetId, previous)) {
            copyText(semanticName, previous.semanticName);
        }
    }

    Asset asset{};
    asset.reset();
    asset.valid = true;
    if (!oc::note::sequencer::setChordPresetMetadata(
            asset,
            presetId,
            semanticName
        )) {
        result.status = SequencerChordPresetStatus::FAILED;
        return result;
    }
    asset.formula = current.captureFormula;
    asset.sourceShapeHint = current.sourceShapeHint;
    if (!oc::note::sequencer::setChordPresetSourceContext(
            asset,
            current.scale,
            static_cast<uint8_t>(current.rootNote % 12U)
        )) {
        result.status = SequencerChordPresetStatus::FAILED;
        return result;
    }

    const auto saved = store.save(asset);
    if (!saved) {
        result.status = statusFromFileError(saved.error().code);
        result.fileError = saved.error().code;
        return result;
    }
    result.bytes = static_cast<uint16_t>(saved.value().bytesWritten);
    copyText(result.presetId, saved.value().presetId);
    return result;
}

FLASHMEM SequencerChordPresetActionResult
SequencerChordPresetDomainServices::applyPreset(
    const char* presetId,
    const Target& target,
    const core::state::sequencer::SequencerChordPresetPreviewKey&
        expectedPreview
) const {
    SequencerChordPresetActionResult result{};
    copyText(result.presetId, presetId);
    if (state_ == nullptr || files_ == nullptr) {
        result.status =
            SequencerChordPresetStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    const auto inspected = inspectPreset(
        presetId,
        target,
        0U
    );
    if (inspected.descriptor.previewKey != expectedPreview ||
        !core::state::sequencer::sequencerChordPresetCanApply(
            inspected.descriptor.compatibility
        )) {
        result.status =
            inspected.status == SequencerChordPresetStatus::OK
                ? SequencerChordPresetStatus::STALE_TARGET
                : inspected.status;
        result.fileError = inspected.fileError;
        return result;
    }
    if (!targetMatches(target)) {
        result.status = SequencerChordPresetStatus::STALE_TARGET;
        return result;
    }

    result.changed =
        core::state::sequencer::setAuthoringNodeChordSpec(
            state_->sequencer,
            target.nodeId,
            inspected.descriptor.projectedFormula
        );
    if (result.changed) {
        state_->sequencer.invalidateVariationTelemetry();
        core::state::sequencer::notifyStepContentDraftMutation(
            state_->sequencer
        );
    }
    result.bytes =
        oc::note::sequencer::STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE;
    return result;
}

}  // namespace core::handler
