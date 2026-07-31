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

FLASHMEM bool clipboardUsage(
    const core::state::MacroAutomationClipboard* clipboard,
    bool trackScope,
    StorageUsage& usage
);

FLASHMEM bool scopeValid(const Scope& scope) {
    return scope.track < macro::TRACK_COUNT &&
           (scope.kind == ScopeKind::TRACK || scope.page < macro::PAGE_COUNT);
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
        if (!modulation::deleteProjectAutomationCurve(
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
        !modulation::deleteProjectAutomationCurve(
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

FLASHMEM bool clipboardEntryPoints(
    const core::state::MacroAutomationClipboard& clipboard,
    const core::state::MacroAutomationClipboardEntry& entry,
    const modulation::ProjectPackedCurvePoint*& points,
    uint16_t& pointCount
) {
    const auto& control = entry.control;
    const uint32_t count =
        static_cast<uint32_t>(control.automation.pointCount) +
        control.recordedShape.pointCount;
    if (count > std::numeric_limits<uint16_t>::max()) return false;
    pointCount = static_cast<uint16_t>(count);
    if (pointCount == 0U) {
        points = nullptr;
        return true;
    }
    const uint16_t offset = control.automation.stored()
        ? control.automation.pointOffset
        : control.recordedShape.pointOffset;
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
        if (!entry.valid || !entry.control.present()) {
            continue;
        }
        const modulation::ProjectPackedCurvePoint* points = nullptr;
        uint16_t pointCount = 0;
        if (!clipboardEntryPoints(*clipboard, entry, points, pointCount)) {
            return false;
        }
        const macro::MacroAutomationSlotAddress address{
            .track = dest.track,
            .page = trackScope ? entry.sourcePage : dest.page,
            .macro = entry.sourceMacro,
        };
        if (!modulation::replaceProjectControlMacroDestinationInDomain(
                domain,
                address,
                entry.control,
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
        const auto curveValid = [&clipboard](
            const modulation::ProjectControlCurvePayload& curve
        ) {
            if (!curve.stored()) return curve.pointCount == 0U;
            return static_cast<uint32_t>(curve.pointOffset) +
                       curve.pointCount <= clipboard->pointPool.used &&
                   modulation::validProjectCurveSpec(
                       curve.spec,
                       clipboard->pointPool.points.data() +
                           curve.pointOffset,
                       curve.pointCount
                   );
        };
        if (!curveValid(entry.control.automation) ||
            !curveValid(entry.control.recordedShape)) {
            return false;
        }
        if (!entry.control.present()) continue;

        for (uint8_t previous = 0; previous < i; ++previous) {
            const auto& candidate = clipboard->entries[previous];
            if (!candidate.valid || candidate.sourceMacro != entry.sourceMacro) continue;
            if (!trackScope || candidate.sourcePage == entry.sourcePage) return false;
        }

        ++usage.slots;
        usage.points += static_cast<uint32_t>(
            entry.control.automation.pointCount
        ) + entry.control.recordedShape.pointCount;
    }
    return true;
}

}  // namespace

FLASHMEM bool clearPagesInDomain(
    modulation::ProjectControlDomainState& domain,
    uint8_t track,
    uint16_t pageMask
) {
    if (track >= macro::TRACK_COUNT || pageMask == 0U) return false;
    return clearProjectSelectionInDomain(
        domain,
        ProjectScopeSelection{
            .kind = ScopeKind::PAGE,
            .mask = pageMask,
            .track = track,
        }
    );
}

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

FLASHMEM bool compactPages(
    modulation::ProjectControlState& control,
    uint8_t track,
    uint16_t retainedPageMask
) {
    return modulation::compactProjectControlPages(
        control,
        track,
        retainedPageMask
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
