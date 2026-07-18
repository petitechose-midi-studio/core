#include "state/project/ProjectModulatorMenuModel.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::project::modulators {

using namespace core::state::modulation;

FLASHMEM SourceDetailLayout sourceDetailLayout(ModulatorKind kind) {
    SourceDetailLayout out{};
    if (kind == ModulatorKind::LFO) {
        out.append(SourceDetailItem::SHAPE);
        out.append(SourceDetailItem::TIMING);
        out.append(SourceDetailItem::RATE);
    } else if (kind == ModulatorKind::ADSR) {
        out.append(SourceDetailItem::ATTACK);
        out.append(SourceDetailItem::DECAY);
        out.append(SourceDetailItem::SUSTAIN);
        out.append(SourceDetailItem::RELEASE);
        out.append(SourceDetailItem::TRIGGER);
    } else {
        out.append(SourceDetailItem::LENGTH);
        out.append(SourceDetailItem::SOURCE_DOMAIN);
    }
    out.append(SourceDetailItem::OPTIONS);
    out.append(SourceDetailItem::DESTINATIONS);
    return out;
}

FLASHMEM SourceDetailLayout sourceOptionsLayout(ModulatorKind kind) {
    SourceDetailLayout out{};
    if (kind == ModulatorKind::LFO) {
        out.append(SourceDetailItem::PHASE);
        out.append(SourceDetailItem::RETRIGGER);
    } else if (kind == ModulatorKind::ADSR) {
        out.append(SourceDetailItem::TIMING);
        out.append(SourceDetailItem::CURVE);
        out.append(SourceDetailItem::RETRIGGER);
    }
    out.append(SourceDetailItem::RENAME);
    out.append(SourceDetailItem::DESTINATIONS);
    return out;
}

FLASHMEM SourceDetailLayout sourceAuditionLayout(ModulatorKind kind) {
    SourceDetailLayout out{};
    if (kind == ModulatorKind::LFO) {
        out.append(SourceDetailItem::SHAPE);
        out.append(SourceDetailItem::TIMING);
        out.append(SourceDetailItem::RATE);
        out.append(SourceDetailItem::PHASE);
        out.append(SourceDetailItem::RETRIGGER);
    } else if (kind == ModulatorKind::ADSR) {
        out.append(SourceDetailItem::ATTACK);
        out.append(SourceDetailItem::DECAY);
        out.append(SourceDetailItem::SUSTAIN);
        out.append(SourceDetailItem::RELEASE);
        out.append(SourceDetailItem::TRIGGER);
        out.append(SourceDetailItem::OPTIONS);
    } else {
        out.append(SourceDetailItem::LENGTH);
        out.append(SourceDetailItem::SOURCE_DOMAIN);
    }
    out.append(SourceDetailItem::DEPTH);
    return out;
}

FLASHMEM SourceDetailLayout sourceAuditionOptionsLayout(ModulatorKind kind) {
    SourceDetailLayout out{};
    if (kind == ModulatorKind::LFO) {
        out.append(SourceDetailItem::PHASE);
        out.append(SourceDetailItem::RETRIGGER);
    } else if (kind == ModulatorKind::ADSR) {
        out.append(SourceDetailItem::TIMING);
        out.append(SourceDetailItem::CURVE);
        out.append(SourceDetailItem::RETRIGGER);
    }
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
