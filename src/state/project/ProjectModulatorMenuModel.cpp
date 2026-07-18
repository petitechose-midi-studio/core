#include "state/project/ProjectModulatorMenuModel.hpp"

#include <config/PlatformCompat.hpp>

#include "state/macro/MacroWorkflow.hpp"

namespace core::state::project::modulators {

using namespace core::state::modulation;

namespace {

FLASHMEM int nextContiguousIndex(uint16_t mask, uint8_t count) {
    int highest = -1;
    for (uint8_t index = 0U; index < count; ++index) {
        if ((mask & static_cast<uint16_t>(1U << index)) != 0U) {
            highest = index;
        }
    }
    const int next = highest + 1;
    return next < count ? next : -1;
}

FLASHMEM uint8_t enabledOrdinal(uint16_t mask, uint8_t target) {
    uint8_t ordinal = 0U;
    for (uint8_t index = 0U; index < target; ++index) {
        if ((mask & static_cast<uint16_t>(1U << index)) != 0U) ++ordinal;
    }
    return ordinal;
}

}  // namespace

FLASHMEM uint16_t destinationPickerRowCount(
    const core::state::macro::MacroPagesState& pages,
    const core::state::project::ProjectNavigationState& navigation
) {
    using Level = core::state::project::ModulatorDestinationPickerLevel;
    if (navigation.destinationPickerLevel == Level::MACRO) {
        return core::state::macro::MACRO_COUNT;
    }
    if (navigation.destinationPickerLevel == Level::TRACK) {
        uint16_t count = 0U;
        const uint16_t mask = pages.currentTrackEnabledMask();
        for (uint8_t index = 0U; index < core::state::macro::TRACK_COUNT; ++index) {
            if ((mask & static_cast<uint16_t>(1U << index)) != 0U) ++count;
        }
        if (nextContiguousIndex(mask, core::state::macro::TRACK_COUNT) >= 0) {
            ++count;
        }
        if (navigation.creatingModulatorSource) ++count;
        return count;
    }

    const uint8_t track = navigation.destinationPickerTrack;
    if (track >= core::state::macro::TRACK_COUNT) return 0U;
    if (!pages.isTrackEnabled(track)) return 1U;
    uint16_t count = 0U;
    const uint16_t mask = pages.tracks[track].enabledPageMask;
    for (uint8_t index = 0U; index < core::state::macro::PAGE_COUNT; ++index) {
        if ((mask & static_cast<uint16_t>(1U << index)) != 0U) ++count;
    }
    if (nextContiguousIndex(mask, core::state::macro::PAGE_COUNT) >= 0) ++count;
    return count;
}

FLASHMEM DestinationPickerRowTarget destinationPickerTargetAtRow(
    const core::state::macro::MacroPagesState& pages,
    const core::state::project::ProjectNavigationState& navigation,
    uint16_t row
) {
    using Level = core::state::project::ModulatorDestinationPickerLevel;
    DestinationPickerRowTarget out{};
    if (navigation.destinationPickerLevel == Level::MACRO) {
        if (row >= core::state::macro::MACRO_COUNT) return out;
        out.kind = DestinationPickerRowKind::MACRO;
        out.index = static_cast<uint8_t>(row);
        const auto plan = core::state::macro::MacroWorkflow::
            planDestinationActivation(
                pages,
                {
                    navigation.destinationPickerTrack,
                    navigation.destinationPickerPage,
                    out.index,
                }
            );
        out.create = plan.valid && plan.changesTopology();
        out.valid = plan.valid;
        return out;
    }

    if (navigation.destinationPickerLevel == Level::TRACK) {
        const uint16_t mask = pages.currentTrackEnabledMask();
        for (uint8_t index = 0U; index < core::state::macro::TRACK_COUNT; ++index) {
            if ((mask & static_cast<uint16_t>(1U << index)) == 0U) continue;
            if (row == 0U) {
                return {DestinationPickerRowKind::TRACK, index, false, true};
            }
            --row;
        }
        const int next = nextContiguousIndex(mask, core::state::macro::TRACK_COUNT);
        if (next >= 0) {
            if (row == 0U) {
                return {
                    DestinationPickerRowKind::TRACK,
                    static_cast<uint8_t>(next),
                    true,
                    true,
                };
            }
            --row;
        }
        if (navigation.creatingModulatorSource && row == 0U) {
            return {
                DestinationPickerRowKind::KEEP_UNASSIGNED,
                0U,
                false,
                true,
            };
        }
        return out;
    }

    const uint8_t track = navigation.destinationPickerTrack;
    if (track >= core::state::macro::TRACK_COUNT) return out;
    if (!pages.isTrackEnabled(track)) {
        return row == 0U
            ? DestinationPickerRowTarget{
                  DestinationPickerRowKind::PAGE,
                  0U,
                  true,
                  true,
              }
            : out;
    }
    const uint16_t mask = pages.tracks[track].enabledPageMask;
    for (uint8_t index = 0U; index < core::state::macro::PAGE_COUNT; ++index) {
        if ((mask & static_cast<uint16_t>(1U << index)) == 0U) continue;
        if (row == 0U) {
            return {DestinationPickerRowKind::PAGE, index, false, true};
        }
        --row;
    }
    const int next = nextContiguousIndex(mask, core::state::macro::PAGE_COUNT);
    if (next >= 0 && row == 0U) {
        return {
            DestinationPickerRowKind::PAGE,
            static_cast<uint8_t>(next),
            true,
            true,
        };
    }
    return out;
}

FLASHMEM uint8_t destinationPickerTrackRow(
    const core::state::macro::MacroPagesState& pages,
    uint8_t track
) {
    if (track >= core::state::macro::TRACK_COUNT) return 0U;
    const uint16_t mask = pages.currentTrackEnabledMask();
    if ((mask & static_cast<uint16_t>(1U << track)) != 0U) {
        return enabledOrdinal(mask, track);
    }
    uint8_t count = 0U;
    for (uint8_t index = 0U; index < core::state::macro::TRACK_COUNT; ++index) {
        if ((mask & static_cast<uint16_t>(1U << index)) != 0U) ++count;
    }
    return count;
}

FLASHMEM uint8_t destinationPickerPageRow(
    const core::state::macro::MacroPagesState& pages,
    uint8_t track,
    uint8_t page
) {
    if (track >= core::state::macro::TRACK_COUNT ||
        page >= core::state::macro::PAGE_COUNT ||
        !pages.isTrackEnabled(track)) {
        return 0U;
    }
    const uint16_t mask = pages.tracks[track].enabledPageMask;
    if ((mask & static_cast<uint16_t>(1U << page)) != 0U) {
        return enabledOrdinal(mask, page);
    }
    uint8_t count = 0U;
    for (uint8_t index = 0U; index < core::state::macro::PAGE_COUNT; ++index) {
        if ((mask & static_cast<uint16_t>(1U << index)) != 0U) ++count;
    }
    return count;
}

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
