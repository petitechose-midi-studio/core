#include "handler/macro/MacroStructureAutomationOps.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::handler::macro_structure_automation_ops {

namespace {

namespace macro = core::state::macro;
namespace modulation = core::state::modulation;

enum class ScopeKind : uint8_t {
    PAGE,
    TRACK,
};

struct Scope {
    ScopeKind kind = ScopeKind::PAGE;
    uint8_t track = 0;
    uint8_t page = 0;
};

struct StorageUsage {
    uint16_t slots = 0;
    uint32_t points = 0;
};

struct ProjectScopeSelection {
    ScopeKind kind = ScopeKind::PAGE;
    uint16_t mask = 0;
    uint8_t track = 0;
};

struct ProjectScopeCopy {
    Scope source{};
    Scope dest{};
};

FLASHMEM bool clipboardUsage(
    const core::state::MacroAutomationClipboard* clipboard,
    bool trackScope,
    StorageUsage& usage
);

FLASHMEM bool scopeValid(const Scope& scope) {
    return scope.track < macro::TRACK_COUNT &&
           (scope.kind == ScopeKind::TRACK || scope.page < macro::PAGE_COUNT);
}

FLASHMEM bool contains(
    const Scope& scope,
    const modulation::ModulationDestination& destination
) {
    return destination.kind == modulation::ModulationDestinationKind::MACRO_SLOT &&
           destination.track == scope.track &&
           (scope.kind == ScopeKind::TRACK || destination.page == scope.page);
}

FLASHMEM bool selected(
    const ProjectScopeSelection& selection,
    const modulation::ModulationDestination& destination
) {
    if (destination.kind != modulation::ModulationDestinationKind::MACRO_SLOT) {
        return false;
    }
    if (selection.kind == ScopeKind::TRACK) {
        return (selection.mask & static_cast<uint16_t>(1U << destination.track)) != 0U;
    }
    return destination.track == selection.track &&
           (selection.mask & static_cast<uint16_t>(1U << destination.page)) != 0U;
}

FLASHMEM bool clearProjectSelectionInDomain(
    modulation::ProjectControlDomainState& domain,
    const ProjectScopeSelection& selection
) {
    if (selection.mask == 0U ||
        (selection.kind == ScopeKind::PAGE &&
         selection.track >= macro::TRACK_COUNT)) {
        return false;
    }

    for (uint16_t cursor = 0; cursor < domain.automation.entryCount;) {
        const auto destination = domain.automation.entries[cursor].destination;
        if (!selected(selection, destination)) {
            ++cursor;
            continue;
        }
        if (!modulation::removeProjectAutomationCurve(
                domain.automation,
                domain.curves,
                destination
            ).changed()) {
            return false;
        }
    }

    for (uint16_t cursor = 0; cursor < domain.modulation.outputBindingCount;) {
        const auto binding = domain.modulation.outputBindings[cursor];
        if (!selected(selection, binding.destination)) {
            ++cursor;
            continue;
        }
        if (!modulation::removeProjectModulationBinding(
                domain.modulation,
                binding.id
            ).changed()) {
            return false;
        }
    }

    if (selection.kind == ScopeKind::TRACK) {
        for (uint16_t cursor = 0;
             cursor < domain.modulation.triggerBindingCount;) {
            const auto trigger = domain.modulation.triggerBindings[cursor];
            const bool removedTrackTrigger =
                trigger.trigger.kind == modulation::ModulationTriggerKind::TRACK_NOTE &&
                (selection.mask & static_cast<uint16_t>(
                    1U << trigger.trigger.track
                )) != 0U;
            if (!removedTrackTrigger) {
                ++cursor;
                continue;
            }
            if (!modulation::removeProjectModulationTrigger(
                    domain.modulation,
                    trigger.id
                ).changed()) {
                return false;
            }
        }
    }

    return true;
}

FLASHMEM bool clearProjectDestinationInDomain(
    modulation::ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address
) {
    if (!macro::macroAutomationAddressValid(address)) return false;
    const auto destination = modulation::projectControlDestination(address);
    if (modulation::findProjectAutomationCurve(
            domain.automation,
            destination
        ) != nullptr &&
        !modulation::removeProjectAutomationCurve(
            domain.automation,
            domain.curves,
            destination
        ).changed()) {
        return false;
    }
    for (uint16_t cursor = 0; cursor < domain.modulation.outputBindingCount;) {
        const auto binding = domain.modulation.outputBindings[cursor];
        if (binding.destination != destination) {
            ++cursor;
            continue;
        }
        if (!modulation::removeProjectModulationBinding(
                domain.modulation,
                binding.id
            ).changed()) {
            return false;
        }
    }
    return true;
}

template <typename Mutation>
FLASHMEM bool mutateProjectControl(
    modulation::ProjectControlState& control,
    Mutation&& mutation
) {
    auto pending = core::app::makeExtmemUnique<
        modulation::ProjectControlDomainState
    >();
    if (!pending) return false;
    std::memcpy(pending.get(), &control.authored, sizeof(*pending));
    if (!mutation(*pending) || !modulation::validProjectModulationDomain(
            pending->modulation,
            pending->curves,
            &pending->automation
        )) {
        return false;
    }
    if (std::memcmp(pending.get(), &control.authored, sizeof(*pending)) == 0) {
        return true;
    }
    std::memcpy(&control.authored, pending.get(), sizeof(*pending));
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool duplicateBinding(
    modulation::ProjectControlDomainState& domain,
    const modulation::ModulationBindingState& sourceBinding,
    const modulation::ModulationDestination& destination
) {
    const auto* sourcePtr = modulation::findProjectModulator(
        domain.modulation,
        sourceBinding.sourceId
    );
    if (sourcePtr == nullptr) return false;
    modulation::ModulationBindingDraft draft{};
    draft.sourceId = sourcePtr->id;
    draft.destination = destination;
    draft.amountQ15 = sourceBinding.amountQ15;
    draft.application = sourceBinding.application;
    draft.transfer = sourceBinding.transfer;
    draft.slewMs = sourceBinding.slewMs;
    draft.enabled = (sourceBinding.flags &
        modulation::PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
    return modulation::addProjectModulationBinding(
        domain.modulation,
        draft
    ).changed();
}

FLASHMEM bool scopesOverlap(const Scope& lhs, const Scope& rhs) {
    if (lhs.track != rhs.track) return false;
    return lhs.kind == ScopeKind::TRACK || rhs.kind == ScopeKind::TRACK ||
           lhs.page == rhs.page;
}

FLASHMEM bool duplicateProjectScopesInDomain(
    modulation::ProjectControlDomainState& domain,
    const ProjectScopeCopy* copies,
    uint8_t copyCount
) {
    if (copies == nullptr || copyCount == 0U) return false;
    for (uint8_t index = 0; index < copyCount; ++index) {
        if (!scopeValid(copies[index].source) ||
            !scopeValid(copies[index].dest) ||
            copies[index].source.kind != copies[index].dest.kind ||
            scopesOverlap(copies[index].source, copies[index].dest)) {
            return false;
        }
        for (uint8_t sourceIndex = 0; sourceIndex < copyCount; ++sourceIndex) {
            if (scopesOverlap(copies[index].dest, copies[sourceIndex].source)) {
                return false;
            }
        }
    }

    for (uint8_t index = 0; index < copyCount; ++index) {
        const auto& dest = copies[index].dest;
        ProjectScopeSelection selection{};
        selection.kind = dest.kind;
        selection.track = dest.track;
        selection.mask = dest.kind == ScopeKind::TRACK
            ? static_cast<uint16_t>(1U << dest.track)
            : static_cast<uint16_t>(1U << dest.page);
        if (!clearProjectSelectionInDomain(domain, selection)) return false;
    }

    for (uint8_t copyIndex = 0; copyIndex < copyCount; ++copyIndex) {
        const auto& copy = copies[copyIndex];
        const uint16_t automationCount = domain.automation.entryCount;
        for (uint16_t index = 0; index < automationCount; ++index) {
            const auto sourceDestination =
                domain.automation.entries[index].destination;
            if (!contains(copy.source, sourceDestination)) continue;
            auto destination = sourceDestination;
            destination.track = copy.dest.track;
            if (copy.dest.kind == ScopeKind::PAGE) {
                destination.page = copy.dest.page;
            }
            if (!modulation::duplicateProjectAutomationCurve(
                    domain.automation,
                    domain.curves,
                    sourceDestination,
                    destination
                ).changed()) {
                return false;
            }
        }

        const uint16_t bindingCount = domain.modulation.outputBindingCount;
        for (uint16_t index = 0; index < bindingCount; ++index) {
            const auto binding = domain.modulation.outputBindings[index];
            if (!contains(copy.source, binding.destination)) continue;
            auto destination = binding.destination;
            destination.track = copy.dest.track;
            if (copy.dest.kind == ScopeKind::PAGE) {
                destination.page = copy.dest.page;
            }
            if (!duplicateBinding(domain, binding, destination)) return false;
            const uint16_t scale =
                modulation::projectModulationDestinationScaleQ15(
                    domain.modulation,
                    binding.destination
                );
            if (scale !=
                    modulation::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15) {
                const auto scaled =
                    modulation::setProjectModulationDestinationScale(
                        domain.modulation,
                        destination,
                        scale
                    );
                if (!scaled.changed() &&
                    modulation::projectModulationDestinationScaleQ15(
                        domain.modulation,
                        destination
                    ) != scale) {
                    return false;
                }
            }
        }
    }
    return true;
}

FLASHMEM bool clipboardEntryPoints(
    const core::state::MacroAutomationClipboard& clipboard,
    const core::state::MacroAutomationClipboardEntry& entry,
    const macro::MacroPackedCurvePoint*& points,
    uint16_t& pointCount
) {
    const auto& state = entry.state;
    const uint32_t count = static_cast<uint32_t>(state.automation.pointCount) +
                           state.modulation.pointCount;
    if (count > std::numeric_limits<uint16_t>::max()) return false;
    pointCount = static_cast<uint16_t>(count);
    if (pointCount == 0U) {
        points = nullptr;
        return true;
    }
    const uint16_t offset = macro::macroCurveStored(state.automation)
        ? state.automation.pointOffset
        : state.modulation.pointOffset;
    if (static_cast<uint32_t>(offset) + pointCount >
        clipboard.pointPool.used) {
        return false;
    }
    points = clipboard.pointPool.points.data() + offset;
    return true;
}

FLASHMEM bool replaceProjectScopeFromClipboardInDomain(
    modulation::ProjectControlDomainState& domain,
    const Scope& dest,
    const core::state::MacroAutomationClipboard* clipboard
) {
    StorageUsage incoming{};
    const bool trackScope = dest.kind == ScopeKind::TRACK;
    if (!scopeValid(dest) || !clipboardUsage(clipboard, trackScope, incoming)) {
        return false;
    }
    ProjectScopeSelection selection{};
    selection.kind = dest.kind;
    selection.track = dest.track;
    selection.mask = trackScope
        ? static_cast<uint16_t>(1U << dest.track)
        : static_cast<uint16_t>(1U << dest.page);
    if (!clearProjectSelectionInDomain(domain, selection)) return false;
    if (clipboard == nullptr || !clipboard->valid) return true;

    for (uint8_t index = 0; index < clipboard->count; ++index) {
        const auto& entry = clipboard->entries[index];
        if (!entry.valid || !macro::macroAutomationSlotHasContent(entry.state)) {
            continue;
        }
        const macro::MacroPackedCurvePoint* points = nullptr;
        uint16_t pointCount = 0;
        if (!clipboardEntryPoints(*clipboard, entry, points, pointCount)) {
            return false;
        }
        const macro::MacroAutomationSlotAddress address{
            .track = dest.track,
            .page = trackScope ? entry.sourcePage : dest.page,
            .macro = entry.sourceMacro,
        };
        if (!modulation::replaceProjectControlMacroSlotInDomain(
                domain,
                address,
                entry.state,
                points,
                pointCount
            )) {
            return false;
        }
        if (entry.destinationScaleQ15 !=
                modulation::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15) {
            const auto scaled = modulation::setProjectModulationDestinationScale(
                domain.modulation,
                modulation::projectControlDestination(address),
                entry.destinationScaleQ15
            );
            if (!scaled.changed() &&
                modulation::projectModulationDestinationScaleQ15(
                    domain.modulation,
                    modulation::projectControlDestination(address)
                ) != entry.destinationScaleQ15) {
                return false;
            }
        }
    }
    return true;
}

FLASHMEM bool clipboardUsage(const core::state::MacroAutomationClipboard* clipboard,
                             bool trackScope,
                             StorageUsage& usage) {
    usage = {};
    if (clipboard == nullptr) return true;
    if (clipboard->trackScope != trackScope) return false;
    if (!clipboard->valid) return clipboard->count == 0;
    if (clipboard->count > clipboard->entries.size() ||
        clipboard->pointPool.used > clipboard->pointPool.points.size()) {
        return false;
    }

    const uint8_t count = clipboard->count;
    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = clipboard->entries[i];
        if (!entry.valid || entry.sourceMacro >= macro::MACRO_COUNT) return false;
        if (trackScope && entry.sourcePage >= macro::PAGE_COUNT) return false;
        if (!macro::macroAutomationSlotStateValidForMutation(
                entry.state,
                clipboard->pointPool
            )) {
            return false;
        }
        if (!macro::macroAutomationSlotHasContent(entry.state)) continue;

        for (uint8_t previous = 0; previous < i; ++previous) {
            const auto& candidate = clipboard->entries[previous];
            if (!candidate.valid || candidate.sourceMacro != entry.sourceMacro) continue;
            if (!trackScope || candidate.sourcePage == entry.sourcePage) return false;
        }

        ++usage.slots;
        usage.points += macro::macroAutomationStoredPointCount(
            entry.state,
            clipboard->pointPool
        );
    }
    return true;
}

}  // namespace

FLASHMEM bool clearPages(
    modulation::ProjectControlState& control,
    uint8_t track,
    uint16_t pageMask
) {
    if (track >= macro::TRACK_COUNT || pageMask == 0U) return false;
    return mutateProjectControl(
        control,
        [track, pageMask](modulation::ProjectControlDomainState& domain) {
            return clearProjectSelectionInDomain(
                domain,
                ProjectScopeSelection{
                    .kind = ScopeKind::PAGE,
                    .mask = pageMask,
                    .track = track,
                }
            );
        }
    );
}

FLASHMEM bool clearTracks(
    modulation::ProjectControlState& control,
    uint16_t trackMask
) {
    if (trackMask == 0U) return false;
    return mutateProjectControl(
        control,
        [trackMask](modulation::ProjectControlDomainState& domain) {
            return clearProjectSelectionInDomain(
                domain,
                ProjectScopeSelection{
                    .kind = ScopeKind::TRACK,
                    .mask = trackMask,
                }
            );
        }
    );
}

FLASHMEM bool clearMacroSlot(
    modulation::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
) {
    return mutateProjectControl(
        control,
        [&address](modulation::ProjectControlDomainState& domain) {
            return clearProjectDestinationInDomain(domain, address);
        }
    );
}

FLASHMEM bool duplicatePages(
    modulation::ProjectControlState& control,
    const ProjectControlPageCopy* copies,
    uint8_t copyCount
) {
    if (copies == nullptr || copyCount == 0U || copyCount > macro::PAGE_COUNT) {
        return false;
    }
    std::array<ProjectScopeCopy, macro::PAGE_COUNT> scopes{};
    for (uint8_t index = 0; index < copyCount; ++index) {
        scopes[index] = {
            .source = Scope{
                .kind = ScopeKind::PAGE,
                .track = copies[index].sourceTrack,
                .page = copies[index].sourcePage,
            },
            .dest = Scope{
                .kind = ScopeKind::PAGE,
                .track = copies[index].destTrack,
                .page = copies[index].destPage,
            },
        };
    }
    return mutateProjectControl(
        control,
        [&scopes, copyCount](modulation::ProjectControlDomainState& domain) {
            return duplicateProjectScopesInDomain(
                domain,
                scopes.data(),
                copyCount
            );
        }
    );
}

FLASHMEM bool duplicateTracks(
    modulation::ProjectControlState& control,
    const ProjectControlTrackCopy* copies,
    uint8_t copyCount
) {
    if (copies == nullptr || copyCount == 0U || copyCount > macro::TRACK_COUNT) {
        return false;
    }
    std::array<ProjectScopeCopy, macro::TRACK_COUNT> scopes{};
    for (uint8_t index = 0; index < copyCount; ++index) {
        scopes[index] = {
            .source = Scope{
                .kind = ScopeKind::TRACK,
                .track = copies[index].sourceTrack,
            },
            .dest = Scope{
                .kind = ScopeKind::TRACK,
                .track = copies[index].destTrack,
            },
        };
    }
    return mutateProjectControl(
        control,
        [&scopes, copyCount](modulation::ProjectControlDomainState& domain) {
            return duplicateProjectScopesInDomain(
                domain,
                scopes.data(),
                copyCount
            );
        }
    );
}

FLASHMEM bool replacePageFromClipboard(
    modulation::ProjectControlState& control,
    uint8_t destTrack,
    uint8_t destPage,
    const core::state::MacroAutomationClipboard* clipboard
) {
    return mutateProjectControl(
        control,
        [destTrack, destPage, clipboard](
            modulation::ProjectControlDomainState& domain
        ) {
            return replaceProjectScopeFromClipboardInDomain(
                domain,
                Scope{
                    .kind = ScopeKind::PAGE,
                    .track = destTrack,
                    .page = destPage,
                },
                clipboard
            );
        }
    );
}

FLASHMEM bool replaceTrackFromClipboard(
    modulation::ProjectControlState& control,
    uint8_t destTrack,
    const core::state::MacroAutomationClipboard* clipboard
) {
    return mutateProjectControl(
        control,
        [destTrack, clipboard](modulation::ProjectControlDomainState& domain) {
            return replaceProjectScopeFromClipboardInDomain(
                domain,
                Scope{.kind = ScopeKind::TRACK, .track = destTrack},
                clipboard
            );
        }
    );
}

}  // namespace core::handler::macro_structure_automation_ops
