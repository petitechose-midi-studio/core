#include "state/modulation/ProjectControlMacroOps.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/modulation/ProjectControlMacroOpsInternal.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::modulation {

using namespace project_control_macro_detail;

namespace {

using PackedScratch = std::array<
    ProjectPackedCurvePoint,
    macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS
>;

}  // namespace

FLASHMEM ModulationDestination projectControlDestination(
    const macro::MacroAutomationSlotAddress& address
) {
    return {
        ModulationDestinationKind::MACRO_SLOT,
        address.track,
        address.page,
        address.macro,
    };
}

FLASHMEM bool readProjectControlMacroDestination(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    ProjectControlMacroDestinationView& out
) {
    return readDomainMacroSlot(control.authored, address, out);
}

FLASHMEM ModulationBindingId projectControlFocusedModulationBinding(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
) {
    if (!validAddress(address)) return {};
    const auto destination = projectControlDestination(address);
    auto& graph = control.authored.modulation;
    auto* entry = focusEntryFor(control.focus, destination);
    if (entry != nullptr) {
        const auto* binding = bindingById(graph, entry->bindingId);
        if (binding != nullptr && binding->destination == destination) {
            entry->stamp = nextFocusStamp(control.focus);
            return entry->bindingId;
        }
        *entry = {};
    }

    uint16_t count = 0;
    const auto* first = firstBindingForDestination(graph, destination, count);
    if (first == nullptr) return {};
    auto& next = allocateFocusEntry(control.focus, destination);
    next.bindingId = first->id;
    next.stamp = nextFocusStamp(control.focus);
    return next.bindingId;
}

FLASHMEM bool setProjectControlFocusedModulationBinding(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    ModulationBindingId bindingId
) {
    if (!validAddress(address) || !valid(bindingId)) return false;
    const auto destination = projectControlDestination(address);
    const auto* binding = bindingById(control.authored.modulation, bindingId);
    if (binding == nullptr || binding->destination != destination) return false;
    auto& entry = allocateFocusEntry(control.focus, destination);
    const bool changed = entry.bindingId != bindingId;
    entry.bindingId = bindingId;
    entry.stamp = nextFocusStamp(control.focus);
    return changed;
}

FLASHMEM bool replaceProjectControlMacroDestinationInDomain(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlMacroDestinationPayload& sourceState,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
) {
    return validAddress(address) && replaceSlotInDomain(
        domain,
        address,
        sourceState,
        sourcePoints,
        sourcePointCount
    );
}

FLASHMEM bool setProjectControlAutomationEnabled(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    bool enabled
) {
    if (!validAddress(address)) return false;
    const auto result = setProjectAutomationEnabled(
        control.authored.automation,
        projectControlDestination(address),
        enabled
    );
    if (!result.changed()) return false;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool setProjectControlModulationEnabled(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    bool enabled
) {
    ProjectControlMacroDestinationView view{};
    if (!readProjectControlMacroDestination(control, address, view) ||
        !view.primaryModulation.present() || view.mutationAmbiguous()) {
        return false;
    }
    auto* binding = bindingById(
        control.authored.modulation,
        view.primaryModulation.bindingId
    );
    if (binding == nullptr || findProjectModulator(
            control.authored.modulation,
            view.primaryModulation.sourceId
        ) == nullptr) return false;
    bool changed = false;
    const uint8_t bindingFlags = enabled
        ? PROJECT_MODULATION_BINDING_FLAG_ENABLED
        : 0U;
    if (binding->flags != bindingFlags) {
        binding->flags = bindingFlags;
        changed = true;
    }
    if (!changed) return false;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool setProjectControlModulationAmount(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    float amount
) {
    ProjectControlMacroDestinationView view{};
    if (!std::isfinite(amount) ||
        !readProjectControlMacroDestination(control, address, view) ||
        !view.primaryModulation.present() || view.mutationAmbiguous()) {
        return false;
    }
    auto* binding = bindingById(
        control.authored.modulation,
        view.primaryModulation.bindingId
    );
    if (binding == nullptr) return false;
    const long rounded = std::lround(std::clamp(amount, -1.0f, 1.0f) * 32767.0f);
    const int16_t packed = static_cast<int16_t>(
        std::clamp<long>(rounded, -32767L, 32767L)
    );
    if (binding->amountQ15 == packed) return false;
    binding->amountQ15 = packed;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool clearProjectControlAutomation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
) {
    if (!validAddress(address)) return false;
    const auto removed = removeProjectAutomationCurve(
        control.authored.automation,
        control.authored.curves,
        projectControlDestination(address)
    );
    if (!removed.changed()) return false;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool clearProjectControlModulation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
) {
    ProjectControlMacroDestinationView view{};
    if (!readProjectControlMacroDestination(control, address, view) ||
        !removePrimaryModulation(control.authored, view)) {
        return false;
    }
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool compactProjectControlPagesInDomain(
    ProjectControlDomainState& domain,
    uint8_t track,
    uint16_t retainedPageMask
) {
    if (track >= macro::TRACK_COUNT || retainedPageMask == 0U) return false;

    const auto removed = [track, retainedPageMask](
        const ModulationDestination& destination
    ) {
        return destination.kind == ModulationDestinationKind::MACRO_SLOT &&
            destination.track == track &&
            (retainedPageMask & static_cast<uint16_t>(
                1U << destination.page
            )) == 0U;
    };

    for (uint16_t cursor = 0U; cursor < domain.automation.entryCount;) {
        const auto destination = domain.automation.entries[cursor].destination;
        if (!removed(destination)) {
            ++cursor;
            continue;
        }
        if (!removeProjectAutomationCurve(
                domain.automation,
                domain.curves,
                destination
            ).changed()) {
            return false;
        }
    }
    for (uint16_t cursor = 0U;
         cursor < domain.modulation.outputBindingCount;) {
        const auto binding = domain.modulation.outputBindings[cursor];
        if (!removed(binding.destination)) {
            ++cursor;
            continue;
        }
        if (!removeProjectModulationBinding(
                domain.modulation,
                binding.id
            ).changed()) {
            return false;
        }
    }

    const auto compactDestination = [track, retainedPageMask](
        ModulationDestination& destination
    ) {
        if (destination.kind != ModulationDestinationKind::MACRO_SLOT ||
            destination.track != track) {
            return;
        }
        uint8_t compactedPage = 0U;
        for (uint8_t page = 0U; page < destination.page; ++page) {
            if ((retainedPageMask & static_cast<uint16_t>(1U << page)) != 0U) {
                ++compactedPage;
            }
        }
        destination.page = compactedPage;
    };
    for (uint16_t index = 0U; index < domain.automation.entryCount; ++index) {
        compactDestination(domain.automation.entries[index].destination);
    }
    for (uint16_t index = 0U;
         index < domain.modulation.outputBindingCount;
         ++index) {
        compactDestination(domain.modulation.outputBindings[index].destination);
    }
    for (uint16_t index = 0U;
         index < domain.modulation.destinationScaleCount;
         ++index) {
        compactDestination(domain.modulation.destinationScales[index].destination);
    }
    return validProjectModulationDomain(
        domain.modulation,
        domain.curves,
        &domain.automation
    );
}

FLASHMEM bool compactProjectControlPages(
    ProjectControlState& control,
    uint8_t track,
    uint16_t retainedPageMask
) {
    auto pending = core::app::makeExtmemUnique<ProjectControlDomainState>();
    if (!pending) return false;
    *pending = control.authored;
    if (!compactProjectControlPagesInDomain(
            *pending,
            track,
            retainedPageMask
        )) {
        return false;
    }
    if (std::memcmp(pending.get(), &control.authored, sizeof(*pending)) == 0) {
        return true;
    }
    control.authored = *pending;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool assignProjectControlAutomation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationLane& lane
) {
    if (!validAddress(address) || !lane.active || lane.pointCount == 0U ||
        lane.pointCount > macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS) {
        return false;
    }
    auto scratch = core::app::makeExtmemUnique<PackedScratch>();
    if (!scratch) return false;
    const uint16_t durationTicks = macro::macroAutomationTicksFromBeats(
        lane.durationBeats
    );
    uint16_t written = 0;
    for (uint16_t index = 0; index < lane.pointCount; ++index) {
        const float beat = std::isfinite(lane.points[index].beat)
            ? std::max(lane.points[index].beat, 0.0f)
            : 0.0f;
        const uint16_t tick = static_cast<uint16_t>(std::clamp<long>(
            std::lround(
                beat * static_cast<float>(PROJECT_CONTROL_TICKS_PER_BEAT)
            ),
            0L,
            durationTicks
        ));
        const int16_t value = macro::macroAutomationPackValue(
            lane.points[index].value,
            false
        );
        if (written > 0U && (*scratch)[written - 1U].tick == tick) {
            (*scratch)[written - 1U].value = value;
            continue;
        }
        (*scratch)[written++] = {tick, value};
    }
    if (written == 0U) return false;
    ProjectCurveSpec spec{};
    spec.sourceDurationTicks = std::max<uint16_t>(durationTicks, 1U);
    spec.durationTicks = std::max<uint16_t>(durationTicks, 1U);
    spec.valueDomain = ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
    spec.origin = ProjectCurveOrigin::NATIVE;
    const auto result = setProjectAutomationCurve(
        control.authored.automation,
        control.authored.curves,
        projectControlDestination(address),
        spec,
        scratch->data(),
        written,
        true
    );
    if (result.status == ProjectModulationStatus::NO_CHANGE) return true;
    if (!result.changed()) {
        return false;
    }
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool captureProjectControlMacroDestination(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    ProjectControlMacroDestinationPayload& outState,
    ProjectPackedCurvePoint* outPoints,
    uint16_t pointCapacity,
    uint16_t& automationPointCount,
    uint16_t& modulationPointCount
) {
    outState = {};
    automationPointCount = 0;
    modulationPointCount = 0;
    ProjectControlMacroDestinationView view{};
    if (!readProjectControlMacroDestination(control, address, view) ||
        view.mutationAmbiguous() ||
        (view.primaryModulation.present() &&
         !view.primaryModulation.isRecordedShape())) {
        return false;
    }
    const uint32_t required = static_cast<uint32_t>(
        view.automation.pointCount
    ) + view.primaryModulation.recordedShape.pointCount;
    if (required > pointCapacity || (required > 0U && outPoints == nullptr)) {
        return false;
    }
    outState.automation = {
        .spec = view.automation.spec,
        .pointOffset = 0U,
        .pointCount = view.automation.pointCount,
        .enabled = view.automation.enabled,
    };
    outState.recordedShape = {
        .spec = view.primaryModulation.recordedShape.spec,
        .pointOffset = view.automation.pointCount,
        .pointCount = view.primaryModulation.recordedShape.pointCount,
        .enabled = view.primaryModulation.recordedShape.enabled,
    };
    outState.modulationAmount = view.primaryModulation.amount;
    automationPointCount = view.automation.pointCount;
    modulationPointCount = view.primaryModulation.recordedShape.pointCount;
    if (automationPointCount > 0U) {
        for (uint16_t index = 0; index < automationPointCount; ++index) {
            const auto& point = control.authored.curves.points[
                view.automation.pointOffset + index
            ];
            outPoints[index] = point;
        }
    }
    if (modulationPointCount > 0U) {
        for (uint16_t index = 0; index < modulationPointCount; ++index) {
            const auto& point = control.authored.curves.points[
                view.primaryModulation.recordedShape.pointOffset + index
            ];
            outPoints[automationPointCount + index] = point;
        }
    }
    return true;
}

FLASHMEM bool replaceProjectControlMacroDestination(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlMacroDestinationPayload& sourceState,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
) {
    if (!validAddress(address)) return false;
    auto pending = core::app::makeExtmemUnique<ProjectControlDomainState>();
    if (!pending) return false;
    *pending = control.authored;
    if (!replaceSlotInDomain(
            *pending,
            address,
            sourceState,
            sourcePoints,
            sourcePointCount
        ) || !validProjectModulationDomain(
            pending->modulation,
            pending->curves,
            &pending->automation
        )) {
        return false;
    }
    control.authored = *pending;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool replaceProjectControlAutomation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlCurvePayload& source,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
) {
    if (!validAddress(address)) {
        return false;
    }
    if (!source.stored()) {
        ProjectControlMacroDestinationView current{};
        if (!readProjectControlMacroDestination(control, address, current)) {
            return false;
        }
        return !current.automation.stored() ||
               clearProjectControlAutomation(control, address);
    }
    if (source.pointCount == 0U || source.pointCount > sourcePointCount ||
        sourcePoints == nullptr ||
        source.spec.valueDomain !=
            ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR ||
        !validProjectCurveSpec(source.spec, sourcePoints, source.pointCount)) {
        return false;
    }
    const auto result = setProjectAutomationCurve(
        control.authored.automation,
        control.authored.curves,
        projectControlDestination(address),
        source.spec,
        sourcePoints,
        source.pointCount,
        source.enabled
    );
    if (result.status == ProjectModulationStatus::NO_CHANGE) return true;
    if (!result.changed()) return false;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool replaceProjectControlAutomationInDomain(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlCurvePayload& source,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
) {
    if (!validAddress(address)) return false;
    const auto destination = projectControlDestination(address);
    if (!source.stored()) {
        const auto removed = removeProjectAutomationCurve(
            domain.automation,
            domain.curves,
            destination
        );
        return removed.changed() ||
               findProjectAutomationCurve(domain.automation, destination) == nullptr;
    }
    if (source.pointCount == 0U || source.pointCount > sourcePointCount ||
        sourcePoints == nullptr ||
        source.spec.valueDomain !=
            ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR ||
        !validProjectCurveSpec(source.spec, sourcePoints, source.pointCount)) {
        return false;
    }
    const auto result = setProjectAutomationCurve(
        domain.automation,
        domain.curves,
        destination,
        source.spec,
        sourcePoints,
        source.pointCount,
        source.enabled
    );
    return result.changed() || result.status == ProjectModulationStatus::NO_CHANGE;
}

FLASHMEM bool replaceProjectControlRecordedShape(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlCurvePayload& source,
    float amount,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
) {
    if (!validAddress(address) || !std::isfinite(amount)) {
        return false;
    }
    ProjectControlMacroDestinationView current{};
    if (!readProjectControlMacroDestination(control, address, current) ||
        current.mutationAmbiguous()) {
        return false;
    }
    if (!source.stored()) {
        return !current.primaryModulation.present() ||
               clearProjectControlModulation(control, address);
    }
    if (source.pointCount == 0U || source.pointCount > sourcePointCount ||
        sourcePoints == nullptr ||
        source.spec.valueDomain != ProjectCurveValueDomain::BIPOLAR ||
        !validProjectCurveSpec(source.spec, sourcePoints, source.pointCount)) {
        return false;
    }

    auto pending = core::app::makeExtmemUnique<
        ProjectControlDomainState
    >();
    if (!pending) return false;
    *pending = control.authored;
    ProjectControlMacroDestinationView pendingView{};
    if (!readDomainMacroSlot(*pending, address, pendingView) ||
        pendingView.mutationAmbiguous()) {
        return false;
    }
    if (pendingView.primaryModulation.present() &&
        !removePrimaryModulation(*pending, pendingView)) {
        return false;
    }
    if (!appendRecordedShape(
            *pending,
            address,
            source,
            amount,
            sourcePoints
        ) || !validProjectModulationDomain(
            pending->modulation,
            pending->curves,
            &pending->automation
        )) {
        return false;
    }
    control.authored = *pending;
    control.markAuthoredMutation();
    return true;
}

}  // namespace core::state::modulation
