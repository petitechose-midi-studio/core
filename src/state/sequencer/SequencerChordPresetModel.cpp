#include "state/sequencer/SequencerChordPresetModel.hpp"

#include <array>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerPresetLibraryActionPolicy.hpp"

namespace core::state::sequencer {
namespace {

constexpr uint32_t FNV_OFFSET = 2166136261U;
constexpr uint32_t FNV_PRIME = 16777619U;

FLASHMEM uint32_t hashByte(uint32_t hash, uint8_t value) {
    return (hash ^ value) * FNV_PRIME;
}

template <typename T>
FLASHMEM uint32_t hashObject(uint32_t hash, const T& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    for (size_t index = 0; index < sizeof(T); ++index) {
        hash = hashByte(hash, bytes[index]);
    }
    return hash;
}

FLASHMEM contextual::ContextEntityRef sourceRef(
    const SequencerChordPresetDescriptor& descriptor
) {
    return {
        .kind = contextual::ContextEntityKind::ASSET,
        .item = descriptor.valid
            ? static_cast<uint16_t>(
                  descriptor.previewKey.assetFingerprint & 0xFFFFU
              )
            : contextual::ContextEntityRef::UNUSED_INDEX,
    };
}

FLASHMEM contextual::ContextEntityRef targetRef(
    const SequencerChordPresetTarget& target
) {
    return {
        .kind = contextual::ContextEntityKind::STEP,
        .item = target.valid
            ? static_cast<uint16_t>(target.stepIndex)
            : contextual::ContextEntityRef::UNUSED_INDEX,
        .node = target.valid
            ? target.nodeId
            : contextual::ContextEntityRef::UNUSED_INDEX,
    };
}

}  // namespace

FLASHMEM bool sequencerChordPresetCanApply(
    SequencerChordPresetCompatibility compatibility
) {
    return compatibility == SequencerChordPresetCompatibility::READY ||
           compatibility ==
               SequencerChordPresetCompatibility::WARNING_ADAPTED;
}

FLASHMEM bool sequencerChordPresetHasWarning(
    SequencerChordPresetCompatibility compatibility
) {
    return compatibility ==
           SequencerChordPresetCompatibility::WARNING_ADAPTED;
}

FLASHMEM const char* sequencerChordPresetCompatibilityLabel(
    SequencerChordPresetCompatibility compatibility
) {
    switch (compatibility) {
        case SequencerChordPresetCompatibility::READY:
            return "Exact";
        case SequencerChordPresetCompatibility::WARNING_ADAPTED:
            return "Adapted";
        case SequencerChordPresetCompatibility::BLOCKED_INCOMPATIBLE:
            return "Incompatible";
        case SequencerChordPresetCompatibility::CORRUPT:
            return "Corrupt";
        case SequencerChordPresetCompatibility::STORAGE_UNAVAILABLE:
            return "Storage unavailable";
        case SequencerChordPresetCompatibility::STALE_TARGET:
            return "Target changed";
        case SequencerChordPresetCompatibility::UNKNOWN:
        default:
            return "Inspecting";
    }
}

FLASHMEM contextual::ContextActionReason
sequencerChordPresetCompatibilityReason(
    SequencerChordPresetCompatibility compatibility
) {
    switch (compatibility) {
        case SequencerChordPresetCompatibility::READY:
            return contextual::ContextActionReason::NONE;
        case SequencerChordPresetCompatibility::WARNING_ADAPTED:
            return contextual::ContextActionReason::ADAPTED;
        case SequencerChordPresetCompatibility::CORRUPT:
            return contextual::ContextActionReason::CORRUPT_ASSET;
        case SequencerChordPresetCompatibility::STORAGE_UNAVAILABLE:
            return contextual::ContextActionReason::STORAGE_UNAVAILABLE;
        case SequencerChordPresetCompatibility::STALE_TARGET:
            return contextual::ContextActionReason::STALE_TARGET;
        case SequencerChordPresetCompatibility::BLOCKED_INCOMPATIBLE:
        case SequencerChordPresetCompatibility::UNKNOWN:
        default:
            return contextual::ContextActionReason::INCOMPATIBLE;
    }
}

FLASHMEM uint32_t sequencerChordPresetTargetHash(
    const SequencerChordPresetTarget& target
) {
    uint32_t hash = FNV_OFFSET;
    hash = hashByte(hash, target.valid ? 1U : 0U);
    hash = hashByte(hash, target.targetUsesScaleDegrees ? 1U : 0U);
    hash = hashByte(hash, target.stepIndex);
    hash = hashObject(hash, target.nodeId);
    hash = hashByte(hash, target.rootNote);
    hash = hashByte(hash, target.velocity);
    hash = hashObject(hash, target.gate);
    hash = hashObject(hash, target.nudge);
    hash = hashByte(hash, target.scale.root);
    hash = hashByte(hash, static_cast<uint8_t>(target.scale.type));
    hash = hashByte(hash, static_cast<uint8_t>(target.scale.mode));
    return hash;
}

FLASHMEM uint32_t sequencerChordPresetAssetFingerprint(
    const oc::note::sequencer::StepSequencerChordPreset& preset
) {
    std::array<
        uint8_t,
        oc::note::sequencer::STEP_SEQUENCER_CHORD_PRESET_ENCODED_SIZE
    > bytes{};
    const auto encoded = oc::note::sequencer::encodeChordPreset(
        preset,
        bytes.data(),
        bytes.size()
    );
    if (!encoded.ok()) return 0U;
    uint32_t hash = FNV_OFFSET;
    for (uint16_t index = 0; index < encoded.bytesWritten; ++index) {
        hash = hashByte(hash, bytes[index]);
    }
    return hash;
}

FLASHMEM contextual::ContextActionSpec
buildSequencerChordPresetActionSpec(
    bool saveMode,
    bool selectedNewAsset,
    bool hasFocusedAsset,
    const SequencerChordPresetTarget& target,
    const SequencerChordPresetDescriptor& descriptor
) {
    contextual::ContextActionSpec spec{};
    spec.scope = contextual::ContextScope::STEP;
    spec.source = sourceRef(descriptor);
    spec.target = targetRef(target);

    if (preset_library_action_policy::projectSaveAction(
            spec,
            saveMode,
            selectedNewAsset,
            hasFocusedAsset,
            target.valid && target.canSave,
            contextual::ContextActionReason::EMPTY_SELECTION
        )) {
        return spec;
    }

    const auto reason = sequencerChordPresetCompatibilityReason(
        descriptor.compatibility
    );
    spec.tap.action = contextual::ContextActionId::LOAD;
    spec.tap.impact = contextual::ContextActionImpact::OVERWRITE;
    spec.tap.availability =
        hasFocusedAsset && target.valid && descriptor.valid &&
        sequencerChordPresetCanApply(descriptor.compatibility)
            ? (sequencerChordPresetHasWarning(descriptor.compatibility)
                   ? contextual::ContextActionAvailability::WARNING
                   : contextual::ContextActionAvailability::AVAILABLE)
            : contextual::ContextActionAvailability::DISABLED;
    spec.tap.reason = hasFocusedAsset ? reason
                                     : contextual::ContextActionReason::NO_ACTION;
    spec.tap.visual = {
        contextual::ContextIconId::LOAD,
        sequencerChordPresetHasWarning(descriptor.compatibility)
            ? contextual::ContextTone::AMBER
            : contextual::ContextTone::GREEN,
    };
    return spec;
}

}  // namespace core::state::sequencer
