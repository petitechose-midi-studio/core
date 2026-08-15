#include "state/sequencer/SequencerStepPresetModel.hpp"

#include <cctype>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "config/Timing.hpp"
#include "state/sequencer/SequencerPresetLibraryActionPolicy.hpp"

namespace core::state::sequencer {

namespace contextual = core::state::contextual;

namespace {

constexpr uint32_t FNV_OFFSET = 2166136261U;
constexpr uint32_t FNV_PRIME = 16777619U;

FLASHMEM uint32_t mixByte(uint32_t hash, uint8_t value) {
    return (hash ^ value) * FNV_PRIME;
}

FLASHMEM uint32_t mixU16(uint32_t hash, uint16_t value) {
    hash = mixByte(hash, static_cast<uint8_t>(value & 0xFFU));
    return mixByte(hash, static_cast<uint8_t>((value >> 8U) & 0xFFU));
}

FLASHMEM contextual::ContextEntityRef targetRef(
    const SequencerStepPresetTarget& target
) {
    return {
        .kind = contextual::ContextEntityKind::STEP,
        .track = target.trackIndex,
        .page = contextual::ContextEntityRef::UNUSED_INDEX,
        .item = target.stepIndex,
        .node = target.targetNodeId,
    };
}

FLASHMEM contextual::ContextEntityRef sourceRef(
    const SequencerStepPresetDescriptor& descriptor
) {
    return {
        .kind = contextual::ContextEntityKind::ASSET,
        .track = contextual::ContextEntityRef::UNUSED_INDEX,
        .page = contextual::ContextEntityRef::UNUSED_INDEX,
        .item = static_cast<uint16_t>(
            sequencerStepPresetIdHash(descriptor.technicalId) & 0xFFFFU
        ),
        .node = contextual::ContextEntityRef::UNUSED_INDEX,
    };
}

}  // namespace

FLASHMEM bool sequencerStepPresetInspectionMatches(
    uint32_t expectedGeneration,
    const SequencerStepPresetPreviewKey& expectedPreview,
    const SequencerStepPresetDescriptor& descriptor
) {
    return expectedGeneration != 0 && descriptor.valid &&
           descriptor.generation == expectedGeneration &&
           descriptor.previewKey.targetHash == expectedPreview.targetHash &&
           descriptor.previewKey.projectRevision == expectedPreview.projectRevision &&
           descriptor.previewKey.stateIndex == expectedPreview.stateIndex;
}

FLASHMEM bool sequencerStepPresetCanApply(
    SequencerStepPresetCompatibility compatibility
) {
    return compatibility == SequencerStepPresetCompatibility::READY ||
           compatibility == SequencerStepPresetCompatibility::WARNING_ADAPTED;
}

FLASHMEM bool sequencerStepPresetHasWarning(
    SequencerStepPresetCompatibility compatibility
) {
    return compatibility == SequencerStepPresetCompatibility::WARNING_ADAPTED;
}

FLASHMEM const char* sequencerStepPresetCompatibilityLabel(
    SequencerStepPresetCompatibility compatibility
) {
    switch (compatibility) {
        case SequencerStepPresetCompatibility::READY: return "Ready";
        case SequencerStepPresetCompatibility::WARNING_ADAPTED: return "Adapted";
        case SequencerStepPresetCompatibility::BLOCKED_CONTEXT: return "Wrong context";
        case SequencerStepPresetCompatibility::BLOCKED_PITCH_CONTEXT:
            return "Pitch context";
        case SequencerStepPresetCompatibility::BLOCKED_CAPACITY: return "Graph full";
        case SequencerStepPresetCompatibility::CORRUPT: return "Corrupt";
        case SequencerStepPresetCompatibility::UNSUPPORTED_VERSION: return "Newer version";
        case SequencerStepPresetCompatibility::STORAGE_UNAVAILABLE: return "Storage error";
        case SequencerStepPresetCompatibility::STALE_TARGET: return "Target changed";
        case SequencerStepPresetCompatibility::UNKNOWN:
        default: return "Inspecting";
    }
}

FLASHMEM contextual::ContextActionReason sequencerStepPresetCompatibilityReason(
    SequencerStepPresetCompatibility compatibility
) {
    switch (compatibility) {
        case SequencerStepPresetCompatibility::BLOCKED_CONTEXT:
        case SequencerStepPresetCompatibility::BLOCKED_PITCH_CONTEXT:
            return contextual::ContextActionReason::INCOMPATIBLE;
        case SequencerStepPresetCompatibility::BLOCKED_CAPACITY:
            return contextual::ContextActionReason::CAPACITY;
        case SequencerStepPresetCompatibility::CORRUPT:
            return contextual::ContextActionReason::CORRUPT_ASSET;
        case SequencerStepPresetCompatibility::UNSUPPORTED_VERSION:
            return contextual::ContextActionReason::UNSUPPORTED_VERSION;
        case SequencerStepPresetCompatibility::STORAGE_UNAVAILABLE:
            return contextual::ContextActionReason::STORAGE_UNAVAILABLE;
        case SequencerStepPresetCompatibility::STALE_TARGET:
            return contextual::ContextActionReason::STALE_TARGET;
        case SequencerStepPresetCompatibility::UNKNOWN:
            return contextual::ContextActionReason::PENDING;
        case SequencerStepPresetCompatibility::WARNING_ADAPTED:
            return contextual::ContextActionReason::ADAPTED;
        default:
            return contextual::ContextActionReason::NONE;
    }
}

FLASHMEM uint32_t sequencerStepPresetIdHash(const char* presetId) {
    uint32_t hash = FNV_OFFSET;
    if (presetId == nullptr) return hash;
    for (const auto* cursor = reinterpret_cast<const uint8_t*>(presetId);
         *cursor != 0;
         ++cursor) {
        hash = mixByte(hash, *cursor);
    }
    return hash;
}

FLASHMEM uint32_t sequencerStepPresetTargetHash(
    const SequencerStepPresetTarget& target
) {
    uint32_t hash = FNV_OFFSET;
    hash = mixByte(hash, target.valid ? 1U : 0U);
    hash = mixByte(hash, target.trackIndex);
    hash = mixByte(hash, target.stepIndex);
    hash = mixByte(hash, static_cast<uint8_t>(target.contentContext));
    hash = mixU16(hash, target.ownerNodeId);
    hash = mixU16(hash, target.sequenceId);
    hash = mixU16(hash, target.cycleSetId);
    hash = mixU16(hash, target.targetNodeId);
    hash = mixByte(hash, target.destinationOwnsPitch ? 1U : 0U);
    hash = mixByte(hash, target.destinationNote);
    hash = mixByte(hash, target.drumLaneIndex);
    hash = mixByte(hash, target.drumRootStepIndex);
    return mixByte(hash, target.drumRootSlot);
}

FLASHMEM void sequencerStepPresetSemanticName(
    const char* presetId,
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0) return;
    out[0] = '\0';
    if (presetId == nullptr) return;

    size_t written = 0;
    bool capitalize = true;
    for (size_t i = 0; presetId[i] != '\0' && written + 1U < outSize; ++i) {
        const unsigned char ch = static_cast<unsigned char>(presetId[i]);
        if (ch == '-' || ch == '_') {
            if (written > 0 && out[written - 1U] != ' ') {
                out[written++] = ' ';
            }
            capitalize = true;
            continue;
        }
        out[written++] = static_cast<char>(
            capitalize ? std::toupper(ch) : ch
        );
        capitalize = false;
    }
    while (written > 0 && out[written - 1U] == ' ') --written;
    out[written] = '\0';
}

FLASHMEM contextual::ContextActionSpec buildSequencerStepPresetActionSpec(
    bool saveMode,
    bool selectedNewAsset,
    bool hasFocusedAsset,
    const SequencerStepPresetTarget& target,
    const SequencerStepPresetDescriptor& descriptor
) {
    contextual::ContextActionSpec spec;
    spec.scope = contextual::ContextScope::STEP;
    spec.source = sourceRef(descriptor);
    spec.target = targetRef(target);

    if (preset_library_action_policy::projectSaveAction(
            spec,
            saveMode,
            selectedNewAsset,
            hasFocusedAsset,
            target.valid,
            contextual::ContextActionReason::CONFLICT
        )) {
        return spec;
    }

    const auto compatibilityReason =
        sequencerStepPresetCompatibilityReason(descriptor.compatibility);
    if (!hasFocusedAsset || !descriptor.valid ||
        !sequencerStepPresetCanApply(descriptor.compatibility)) {
        spec.tap.action = contextual::ContextActionId::LOAD;
        spec.tap.impact = contextual::ContextActionImpact::CONSTRUCTIVE;
        spec.tap.availability = contextual::ContextActionAvailability::DISABLED;
        spec.tap.reason = hasFocusedAsset
            ? compatibilityReason
            : contextual::ContextActionReason::NO_ACTION;
        spec.tap.visual = {
            contextual::ContextIconId::LOAD,
            contextual::ContextTone::GREEN,
        };
        return spec;
    }

    if (descriptor.footprint == SequencerStepPresetFootprint::FREE) {
        spec.tap.action = contextual::ContextActionId::LOAD;
        spec.tap.impact = contextual::ContextActionImpact::CONSTRUCTIVE;
        spec.tap.availability = sequencerStepPresetHasWarning(descriptor.compatibility)
            ? contextual::ContextActionAvailability::WARNING
            : contextual::ContextActionAvailability::AVAILABLE;
        spec.tap.reason = compatibilityReason;
        spec.tap.visual = {
            contextual::ContextIconId::LOAD,
            sequencerStepPresetHasWarning(descriptor.compatibility)
                ? contextual::ContextTone::AMBER
                : contextual::ContextTone::GREEN,
        };
        return spec;
    }

    spec.hold.action = contextual::ContextActionId::LOAD;
    spec.hold.impact = contextual::ContextActionImpact::OVERWRITE;
    spec.hold.availability = contextual::ContextActionAvailability::WARNING;
    spec.hold.reason = compatibilityReason;
    spec.hold.visual = {
        contextual::ContextIconId::LOAD,
        contextual::ContextTone::AMBER,
    };
    spec.guard = {
        contextual::ContextGuardKind::HOLD,
        static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS),
    };
    return spec;
}

}  // namespace core::state::sequencer
