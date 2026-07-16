#include "state/project/ProjectModulatorMenuModel.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::project::modulators {

using namespace core::state::modulation;

FLASHMEM SourceDetailLayout sourceDetailLayout(ModulatorKind kind) {
    SourceDetailLayout out{};
    out.append(SourceDetailItem::PREVIEW);
    out.append(SourceDetailItem::ENABLED);
    if (kind == ModulatorKind::LFO) {
        out.append(SourceDetailItem::SHAPE);
        out.append(SourceDetailItem::RATE);
        out.append(SourceDetailItem::TIMING);
        out.append(SourceDetailItem::PHASE);
        out.append(SourceDetailItem::RETRIGGER);
    } else {
        out.append(SourceDetailItem::LENGTH);
        out.append(SourceDetailItem::RANGE);
    }
    out.append(SourceDetailItem::REACH);
    out.append(SourceDetailItem::DESTINATIONS);
    return out;
}

FLASHMEM uint16_t sourceDestinationCount(
    const ProjectModulationState& graph,
    ModulatorId sourceId
) {
    uint16_t count = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].sourceId == sourceId) ++count;
    }
    return count;
}

FLASHMEM uint16_t sourceDestinationCountOnTrack(
    const ProjectModulationState& graph,
    ModulatorId sourceId,
    uint8_t track
) {
    uint16_t count = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.sourceId == sourceId &&
            binding.destination.track == track) {
            ++count;
        }
    }
    return count;
}

FLASHMEM ModulatorReach sourcePartitionReach(
    const ProjectModulationState& graph,
    ModulatorId sourceId,
    uint8_t splitTrack,
    bool selectedPartition
) {
    const ModulationBindingState* first = nullptr;
    bool oneDestination = true;
    uint16_t trackMask = 0U;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.sourceId != sourceId ||
            (binding.destination.track == splitTrack) != selectedPartition) {
            continue;
        }
        if (first == nullptr) {
            first = &binding;
        } else if (binding.destination != first->destination) {
            oneDestination = false;
        }
        trackMask = static_cast<uint16_t>(
            trackMask | (1U << binding.destination.track)
        );
    }
    if (first == nullptr) return {};
    if (oneDestination) {
        return {
            .kind = ModulatorReachKind::MACRO,
            .track = first->destination.track,
            .page = first->destination.page,
            .macro = first->destination.macro,
        };
    }
    return {
        .trackMask = trackMask,
        .kind = ModulatorReachKind::TRACK_SET,
    };
}

FLASHMEM ReachChoiceLayout sourceReachChoiceLayout(
    const ProjectModulationState& graph,
    ModulatorId sourceId
) {
    ReachChoiceLayout out{};
    const uint16_t total = sourceDestinationCount(graph, sourceId);
    out.append({
        .kind = ReachChoiceKind::TIGHTEST,
        .destinationCount = total,
    });
    out.append({
        .kind = ReachChoiceKind::PROJECT,
        .destinationCount = total,
    });
    uint8_t trackCount = 0;
    uint16_t counts[PROJECT_MODULATION_TRACK_COUNT]{};
    for (uint8_t track = 0; track < PROJECT_MODULATION_TRACK_COUNT; ++track) {
        counts[track] = sourceDestinationCountOnTrack(graph, sourceId, track);
        if (counts[track] > 0U) ++trackCount;
    }
    if (trackCount < 2U) return out;
    for (uint8_t track = 0; track < PROJECT_MODULATION_TRACK_COUNT; ++track) {
        if (counts[track] == 0U || counts[track] >= total) continue;
        out.append({
            .kind = ReachChoiceKind::SPLIT_TRACK,
            .track = track,
            .destinationCount = counts[track],
        });
    }
    return out;
}

FLASHMEM ModulatorReach tightestSourceReach(
    const ProjectModulationState& graph,
    ModulatorId sourceId
) {
    const ModulationBindingState* first = nullptr;
    bool oneDestination = true;
    uint16_t trackMask = 0U;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.sourceId != sourceId) continue;
        if (first == nullptr) {
            first = &binding;
        } else if (binding.destination != first->destination) {
            oneDestination = false;
        }
        trackMask = static_cast<uint16_t>(
            trackMask | (1U << binding.destination.track)
        );
    }
    if (first == nullptr) return {};
    if (oneDestination) {
        return {
            .kind = ModulatorReachKind::MACRO,
            .track = first->destination.track,
            .page = first->destination.page,
            .macro = first->destination.macro,
        };
    }
    return {
        .trackMask = trackMask,
        .kind = ModulatorReachKind::TRACK_SET,
    };
}

FLASHMEM const ModulationBindingState* sourceBindingAtOrdinal(
    const ProjectModulationState& graph,
    ModulatorId sourceId,
    uint16_t ordinal
) {
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.sourceId != sourceId) continue;
        if (ordinal == 0U) return &binding;
        --ordinal;
    }
    return nullptr;
}

FLASHMEM ModulationBindingState* sourceBindingAtOrdinal(
    ProjectModulationState& graph,
    ModulatorId sourceId,
    uint16_t ordinal
) {
    return const_cast<ModulationBindingState*>(
        sourceBindingAtOrdinal(
            static_cast<const ProjectModulationState&>(graph),
            sourceId,
            ordinal
        )
    );
}

}  // namespace core::state::project::modulators
