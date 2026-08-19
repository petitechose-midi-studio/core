#include "state/sequencer/SequencerPatternPreset.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerPresetLibraryActionPolicy.hpp"

namespace core::state::sequencer {

FLASHMEM bool setSequencerPatternPresetMetadata(
    SequencerPatternPresetMetadata& metadata,
    SequencerTrackKind trackKind,
    const char* technicalId,
    const char* semanticName
) {
    if (trackKind > SequencerTrackKind::DRUM ||
        !validSequencerPresetTechnicalId(technicalId) ||
        !validSequencerPresetSemanticName(semanticName)) {
        return false;
    }

    metadata = {};
    metadata.trackKind = trackKind;
    std::strncpy(
        metadata.technicalId,
        technicalId,
        sizeof(metadata.technicalId) - 1U
    );
    std::strncpy(
        metadata.semanticName,
        semanticName,
        sizeof(metadata.semanticName) - 1U
    );
    return true;
}

FLASHMEM bool sequencerPatternPresetMetadataIsCanonical(
    const SequencerPatternPresetMetadata& metadata
) {
    return metadata.formatVersion ==
               SequencerPatternPresetMetadata::CURRENT_FORMAT_VERSION &&
        metadata.trackKind <= SequencerTrackKind::DRUM &&
        validSequencerPresetTechnicalId(metadata.technicalId) &&
        validSequencerPresetSemanticName(metadata.semanticName);
}

FLASHMEM bool sequencerPatternPresetDrumKitCompatible(
    const DrumTrackState& source,
    const DrumTrackState& destination
) {
    if (source.kit.laneCount != destination.kit.laneCount) return false;
    for (uint8_t lane = 0U; lane < source.kit.laneCount; ++lane) {
        const auto& sourceLane = source.kit.lanes[lane];
        const auto& destinationLane = destination.kit.lanes[lane];
        if (sourceLane.role != destinationLane.role ||
            (sourceLane.role == DrumLaneRole::CUSTOM &&
             sourceLane.midiNote != destinationLane.midiNote)) {
            return false;
        }
    }
    return true;
}

FLASHMEM uint32_t sequencerPatternPresetTargetHash(
    const SequencerPatternPresetTarget& target
) {
    constexpr uint32_t FNV_OFFSET = 2166136261U;
    constexpr uint32_t FNV_PRIME = 16777619U;
    uint32_t hash = FNV_OFFSET;
    const auto append = [&hash](uint8_t value) {
        hash = (hash ^ value) * FNV_PRIME;
    };
    append(target.valid ? 1U : 0U);
    append(target.trackIndex);
    append(static_cast<uint8_t>(target.trackKind));
    for (uint8_t shift = 0U; shift < 32U; shift += 8U) {
        append(static_cast<uint8_t>(target.projectRevision >> shift));
    }
    return hash == 0U ? 1U : hash;
}

FLASHMEM contextual::ContextActionReason
sequencerPatternPresetCompatibilityReason(
    SequencerPatternPresetCompatibility compatibility
) {
    using Reason = contextual::ContextActionReason;
    switch (compatibility) {
        case SequencerPatternPresetCompatibility::READY:
            return Reason::NONE;
        case SequencerPatternPresetCompatibility::STALE_TARGET:
            return Reason::STALE_TARGET;
        case SequencerPatternPresetCompatibility::CORRUPT:
            return Reason::CORRUPT_ASSET;
        case SequencerPatternPresetCompatibility::STORAGE_UNAVAILABLE:
            return Reason::STORAGE_UNAVAILABLE;
        case SequencerPatternPresetCompatibility::WRONG_TRACK_KIND:
        case SequencerPatternPresetCompatibility::INCOMPATIBLE_DRUM_KIT:
        default:
            return Reason::INCOMPATIBLE;
    }
}

FLASHMEM const char* sequencerPatternPresetCompatibilityLabel(
    SequencerPatternPresetCompatibility compatibility
) {
    switch (compatibility) {
        case SequencerPatternPresetCompatibility::READY:
            return "Ready";
        case SequencerPatternPresetCompatibility::WRONG_TRACK_KIND:
            return "Wrong track type";
        case SequencerPatternPresetCompatibility::INCOMPATIBLE_DRUM_KIT:
            return "Incompatible drum kit";
        case SequencerPatternPresetCompatibility::STALE_TARGET:
            return "Target changed";
        case SequencerPatternPresetCompatibility::STORAGE_UNAVAILABLE:
            return "Storage unavailable";
        case SequencerPatternPresetCompatibility::CORRUPT:
        default:
            return "Corrupt preset";
    }
}

FLASHMEM const char* sequencerPatternPresetSourceLabel(
    SequencerPatternPresetSource source
) {
    return source == SequencerPatternPresetSource::FACTORY
        ? "Factory"
        : "User";
}

FLASHMEM const char* sequencerPatternPresetSourceFilterLabel(
    SequencerPatternPresetSourceFilter filter
) {
    switch (filter) {
        case SequencerPatternPresetSourceFilter::FACTORY:
            return "Factory";
        case SequencerPatternPresetSourceFilter::USER:
            return "User";
        case SequencerPatternPresetSourceFilter::ALL:
        default:
            return "All";
    }
}

FLASHMEM contextual::ContextActionSpec
buildSequencerPatternPresetActionSpec(
    bool saveMode,
    bool selectedNewAsset,
    bool hasFocusedAsset,
    const SequencerPatternPresetTarget& target,
    const SequencerPatternPresetDescriptor& descriptor
) {
    contextual::ContextActionSpec spec{};
    spec.scope = contextual::ContextScope::PATTERN;
    spec.source = {
        .kind = contextual::ContextEntityKind::ASSET,
        .item = descriptor.valid
            ? static_cast<uint16_t>(
                  descriptor.previewKey.assetHash & 0xFFFFU
              )
            : contextual::ContextEntityRef::UNUSED_INDEX,
    };
    spec.target = {
        .kind = contextual::ContextEntityKind::PATTERN,
        .track = target.valid
            ? static_cast<uint16_t>(target.trackIndex)
            : contextual::ContextEntityRef::UNUSED_INDEX,
    };

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

    spec.tap.action = contextual::ContextActionId::LOAD;
    spec.tap.impact = contextual::ContextActionImpact::OVERWRITE;
    spec.tap.availability =
        hasFocusedAsset && target.valid && descriptor.valid &&
        sequencerPatternPresetCanApply(descriptor.compatibility)
            ? contextual::ContextActionAvailability::AVAILABLE
            : contextual::ContextActionAvailability::DISABLED;
    spec.tap.reason = hasFocusedAsset
        ? sequencerPatternPresetCompatibilityReason(
              descriptor.compatibility
          )
        : contextual::ContextActionReason::NO_ACTION;
    spec.tap.visual = {
        contextual::ContextIconId::LOAD,
        contextual::ContextTone::GREEN,
    };
    return spec;
}

}  // namespace core::state::sequencer
