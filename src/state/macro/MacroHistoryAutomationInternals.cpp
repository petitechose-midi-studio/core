#include "state/macro/MacroHistoryInternals.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
namespace core::state::macro {

namespace history_detail {

FLASHMEM bool sameCurveSpec(
    const core::state::modulation::ProjectCurveSpec& lhs,
    const core::state::modulation::ProjectCurveSpec& rhs
) {
    return lhs.sourceDurationTicks == rhs.sourceDurationTicks &&
           lhs.durationTicks == rhs.durationTicks &&
           lhs.windowOffsetTicks == rhs.windowOffsetTicks &&
           lhs.interpolation == rhs.interpolation &&
           lhs.valueDomain == rhs.valueDomain &&
           lhs.origin == rhs.origin;
}

namespace {

constexpr uint64_t AUTOMATION_POINT_FINGERPRINT_SEED =
    UINT64_C(14695981039346656037);

FLASHMEM uint64_t automationPointFingerprint(
    const core::state::modulation::ProjectCurveArena& arena,
    const core::state::modulation::ProjectCurveRecord& record
) {
    return hashBytes64(
        AUTOMATION_POINT_FINGERPRINT_SEED,
        arena.points.data() + record.pointOffset,
        static_cast<size_t>(record.pointCount) *
            sizeof(core::state::modulation::ProjectPackedCurvePoint)
    );
}

}  // namespace

FLASHMEM bool sameCurveMetadata(
    const core::state::modulation::ProjectControlCurvePayload& lhs,
    const core::state::modulation::ProjectControlCurvePayload& rhs
) {
    return lhs.enabled == rhs.enabled &&
           lhs.pointCount == rhs.pointCount &&
           sameCurveSpec(lhs.spec, rhs.spec);
}

FLASHMEM bool sameCurveMetadata(
    const core::state::modulation::ProjectControlCurveView& lhs,
    const core::state::modulation::ProjectControlCurvePayload& rhs
) {
    return lhs.enabled == rhs.enabled &&
           lhs.pointCount == rhs.pointCount &&
           sameCurveSpec(lhs.spec, rhs.spec);
}

FLASHMEM bool captureAutomationMetadataHistory(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroAutomationMetadataHistoryPayload& out
) {
    out = {};
    if (!macroAutomationAddressValid(address)) return false;
    core::state::modulation::ProjectControlMacroDestinationView view{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages.control,
            address,
            view
        ) || !view.automation.stored() ||
        view.automation.pointCount >
            MACRO_AUTOMATION_RECORDING_MAX_POINTS) {
        return false;
    }
    const auto* record = core::state::modulation::findProjectCurve(
        pages.control.authored.curves,
        view.automation.id
    );
    if (record == nullptr ||
        record->pointCount != view.automation.pointCount ||
        static_cast<uint32_t>(record->pointOffset) + record->pointCount >
            pages.control.authored.curves.pointCount) {
        return false;
    }
    out.before = view.automation.spec;
    out.after = view.automation.spec;
    out.pointFingerprint = automationPointFingerprint(
        pages.control.authored.curves,
        *record
    );
    out.pointCount = record->pointCount;
    out.enabled = view.automation.enabled;
    out.valid = true;
    return true;
}

FLASHMEM bool liveAutomationMetadataMatches(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroAutomationMetadataHistoryPayload& payload,
    bool after,
    bool verifyPoints
) {
    if (!payload.valid || !macroAutomationAddressValid(address) ||
        payload.pointCount == 0U ||
        payload.pointCount > MACRO_AUTOMATION_RECORDING_MAX_POINTS) {
        return false;
    }
    core::state::modulation::ProjectControlMacroDestinationView view{};
    if (!core::state::modulation::readProjectControlMacroDestination(
            pages.control,
            address,
            view
        ) || !view.automation.stored() ||
        view.automation.enabled != payload.enabled ||
        view.automation.pointCount != payload.pointCount ||
        !sameCurveSpec(
            view.automation.spec,
            after ? payload.after : payload.before
        )) {
        return false;
    }
    const auto* record = core::state::modulation::findProjectCurve(
        pages.control.authored.curves,
        view.automation.id
    );
    if (record == nullptr || record->pointCount != payload.pointCount ||
        static_cast<uint32_t>(record->pointOffset) + record->pointCount >
            pages.control.authored.curves.pointCount) {
        return false;
    }
    return !verifyPoints ||
        automationPointFingerprint(pages.control.authored.curves, *record) ==
            payload.pointFingerprint;
}

FLASHMEM bool applyAutomationMetadataHistory(
    MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    const MacroAutomationMetadataHistoryPayload& payload,
    bool after
) {
    if (!liveAutomationMetadataMatches(
            pages,
            address,
            payload,
            !after,
            true
        )) {
        return false;
    }
    const auto& target = after ? payload.after : payload.before;
    const auto& rollback = after ? payload.before : payload.after;
    const auto applied =
        core::state::modulation::setProjectAutomationCurveSpec(
            pages.control.authored.automation,
            pages.control.authored.curves,
            core::state::modulation::projectControlDestination(address),
            target
        );
    if (!applied.changed()) return false;
    pages.control.markAuthoredMutation();
    if (liveAutomationMetadataMatches(
            pages,
            address,
            payload,
            after,
            true
        )) {
        return true;
    }
    const auto restored =
        core::state::modulation::setProjectAutomationCurveSpec(
            pages.control.authored.automation,
            pages.control.authored.curves,
            core::state::modulation::projectControlDestination(address),
            rollback
        );
    if (restored.changed()) pages.control.markAuthoredMutation();
    return false;
}

FLASHMEM bool sameFloatBits(float lhs, float rhs) {
    return std::memcmp(&lhs, &rhs, sizeof(float)) == 0;
}

FLASHMEM bool samePoint(
    const core::state::modulation::ProjectPackedCurvePoint& lhs,
    const core::state::modulation::ProjectPackedCurvePoint& rhs
) {
    return lhs.tick == rhs.tick && lhs.value == rhs.value;
}

FLASHMEM uint16_t snapshotPointCount(const MacroSlotHistorySnapshot& snapshot) {
    return static_cast<uint16_t>(
        snapshot.automationPointCount + snapshot.modulationPointCount
    );
}

FLASHMEM bool snapshotConsistent(const MacroSlotHistorySnapshot& snapshot) {
    if (!macroAutomationAddressValid(snapshot.address)) return false;
    const uint32_t total = static_cast<uint32_t>(snapshot.automationPointCount) +
                           snapshot.modulationPointCount;
    if (total > MACRO_HISTORY_POINT_CAPACITY) return false;
    if (!snapshot.slotPresent) {
        return snapshot.automationPointCount == 0 &&
               snapshot.modulationPointCount == 0 &&
               snapshot.destinationScaleQ15 ==
                   core::state::modulation::
                       PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    }
    if (snapshot.control.automation.stored() !=
            (snapshot.automationPointCount > 0) ||
        snapshot.control.recordedShape.stored() !=
            (snapshot.modulationPointCount > 0) ||
        snapshot.control.automation.pointCount !=
            snapshot.automationPointCount ||
        snapshot.control.recordedShape.pointCount !=
            snapshot.modulationPointCount) {
        return false;
    }
    if (snapshot.control.automation.stored() &&
        snapshot.control.automation.pointOffset != 0U) {
        return false;
    }
    if (snapshot.destinationScaleQ15 !=
            core::state::modulation::
                PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 &&
        snapshot.modulationPointCount == 0U) {
        return false;
    }
    return !snapshot.control.recordedShape.stored() ||
           snapshot.control.recordedShape.pointOffset ==
               snapshot.automationPointCount;
}

FLASHMEM bool automationSnapshotConsistent(
    const MacroAutomationHistorySnapshot& snapshot
) {
    if (!macroAutomationAddressValid(snapshot.address) ||
        snapshot.pointCount > MACRO_AUTOMATION_RECORDING_MAX_POINTS ||
        snapshot.automation.stored() != (snapshot.pointCount > 0U) ||
        snapshot.automation.pointCount != snapshot.pointCount ||
        (snapshot.pointCount > 0U && !snapshot.points) ||
        (snapshot.pointCount > 0U &&
         snapshot.automation.pointOffset != 0U) ||
        (snapshot.pointCount > 0U &&
         snapshot.automation.spec.valueDomain !=
             core::state::modulation::ProjectCurveValueDomain::
                 ABSOLUTE_UNIPOLAR)) {
        return false;
    }
    return true;
}

FLASHMEM bool automationTakePayloadConsistent(
    const MacroAutomationTakeHistoryPayload& payload,
    bool requireTouched
) {
    if (payload.track >= TRACK_COUNT || payload.page >= PAGE_COUNT ||
        payload.candidateMask == 0U ||
        (payload.candidateMask & 0xFF00U) != 0U ||
        (payload.touchedMask & ~payload.candidateMask) != 0U ||
        (requireTouched && payload.touchedMask == 0U)) {
        return false;
    }
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((payload.candidateMask & bit) == 0U) continue;
        const auto& before = payload.before[macro];
        const auto& after = payload.after[macro];
        const MacroAutomationSlotAddress expected{
            .track = payload.track,
            .page = payload.page,
            .macro = macro,
        };
        if (!macroAutomationAddressEquals(before.address, expected) ||
            !macroAutomationAddressEquals(after.address, expected) ||
            !automationSnapshotConsistent(before) || !after.points) {
            return false;
        }
        if ((payload.touchedMask & bit) != 0U &&
            !automationSnapshotConsistent(after)) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool liveAutomationTakeMatches(
    const MacroPagesState& pages,
    const MacroAutomationTakeHistoryPayload& payload,
    bool after
) {
    if (!automationTakePayloadConsistent(payload, true)) return false;
    const auto& snapshots = after ? payload.after : payload.before;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((payload.touchedMask & bit) == 0U) continue;
        if (!liveMacroAutomationMatchesHistorySnapshot(
                pages,
                snapshots[macro]
            )) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool applyAutomationTakeAtomically(
    MacroPagesState& pages,
    const MacroAutomationTakeHistoryPayload& payload,
    bool after
) {
    if (!automationTakePayloadConsistent(payload, true)) return false;
    auto pending = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >();
    if (!pending) return false;
    *pending = pages.control.authored;
    const auto& snapshots = after ? payload.after : payload.before;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((payload.touchedMask & bit) == 0U) continue;
        const auto& snapshot = snapshots[macro];
        if (!core::state::modulation::replaceProjectControlAutomationInDomain(
                *pending,
                snapshot.address,
                snapshot.automation,
                snapshot.pointCount > 0U ? snapshot.points.get() : nullptr,
                snapshot.pointCount
            )) {
            return false;
        }
    }
    if (!core::state::modulation::validProjectModulationDomain(
            pending->modulation,
            pending->curves,
            &pending->automation
        )) {
        return false;
    }
    pages.control.authored = *pending;
    pages.control.markAuthoredMutation();
    return true;
}

FLASHMEM void normalizeCurveOffsets(MacroSlotHistorySnapshot& snapshot) {
    snapshot.control.automation.pointOffset = 0U;
    snapshot.control.recordedShape.pointOffset =
        snapshot.automationPointCount;
}

FLASHMEM bool liveProjectCurveMatches(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ProjectControlCurveView& live,
    const core::state::modulation::ProjectControlCurvePayload& expected,
    const MacroSlotHistorySnapshot& snapshot,
    uint16_t snapshotOffset
) {
    if (!sameCurveMetadata(live, expected)) return false;
    if (!live.stored()) {
        return !expected.stored() &&
               !core::state::modulation::valid(live.id);
    }
    const auto* record = core::state::modulation::findProjectCurve(
        control.authored.curves,
        live.id
    );
    if (record == nullptr || record->pointCount != live.pointCount ||
        static_cast<uint32_t>(record->pointOffset) + record->pointCount >
            control.authored.curves.pointCount ||
        static_cast<uint32_t>(snapshotOffset) + live.pointCount >
            snapshot.points.size()) {
        return false;
    }
    for (uint16_t i = 0; i < live.pointCount; ++i) {
        const auto& point = control.authored.curves.points[
            static_cast<uint16_t>(record->pointOffset + i)
        ];
        if (!samePoint(
                point,
                snapshot.points[static_cast<uint16_t>(snapshotOffset + i)]
            )) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool sameAddress(
    const MacroAutomationSlotAddress& lhs,
    const MacroAutomationSlotAddress& rhs
) {
    return macroAutomationAddressEquals(lhs, rhs);
}

FLASHMEM void readManualOverride(
    const MacroManualOverrideState& overrides,
    const MacroAutomationSlotAddress& address,
    bool& active,
    float& value
) {
    value = 0.0f;
    active = overrides.valueFor(address, value);
}

FLASHMEM bool manualOverrideMatches(
    const MacroManualOverrideState& overrides,
    const MacroAutomationSlotAddress& address,
    bool expectedActive,
    float expectedValue
) {
    float liveValue = 0.0f;
    const bool liveActive = overrides.valueFor(address, liveValue);
    return liveActive == expectedActive &&
           (!expectedActive || sameFloatBits(liveValue, expectedValue));
}

FLASHMEM bool canApplyManualOverride(
    const MacroManualOverrideState& overrides,
    const MacroAutomationSlotAddress& address,
    bool targetActive
) {
    if (!targetActive || overrides.activeFor(address)) return true;
    return overrides.entryCount < MacroManualOverrideState::CAPACITY;
}

FLASHMEM bool applyManualOverride(
    MacroManualOverrideState& overrides,
    const MacroAutomationSlotAddress& address,
    bool targetActive,
    float targetValue
) {
    if (!targetActive) {
        return !overrides.activeFor(address) || overrides.resume(address);
    }
    const auto status = overrides.activate(address, targetValue);
    return status != MacroManualOverrideState::ActivateStatus::INVALID_ADDRESS &&
           status != MacroManualOverrideState::ActivateStatus::CAPACITY_EXHAUSTED;
}

FLASHMEM uint32_t hashBytes(uint32_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 16777619UL;
    }
    return hash;
}

FLASHMEM bool destinationScaleRemovedWithSource(
    const core::state::modulation::ProjectModulationState& graph,
    const core::state::modulation::ModulationDestination& destination,
    core::state::modulation::ModulatorId sourceId
) {
    bool found = false;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        if (binding.sourceId != sourceId) return false;
        found = true;
    }
    return found;
}

FLASHMEM uint32_t unrelatedModulatorHash(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId
) {
    uint32_t hash = 2166136261UL;
    hash = hashBytes(hash, &graph.nextSourceId, sizeof(graph.nextSourceId));
    hash = hashBytes(hash, &graph.nextBindingId, sizeof(graph.nextBindingId));
    for (uint16_t index = 0; index < graph.sourceCount; ++index) {
        if (graph.sources[index].id == sourceId) continue;
        hash = hashBytes(hash, &graph.sources[index], sizeof(graph.sources[index]));
    }
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].sourceId == sourceId) continue;
        hash = hashBytes(
            hash,
            &graph.outputBindings[index],
            sizeof(graph.outputBindings[index])
        );
    }
    for (uint16_t index = 0; index < graph.triggerBindingCount; ++index) {
        if (graph.triggerBindings[index].sourceId == sourceId) continue;
        hash = hashBytes(
            hash,
            &graph.triggerBindings[index],
            sizeof(graph.triggerBindings[index])
        );
    }
    uint16_t scaleCount = 0;
    for (uint16_t index = 0; index < graph.destinationScaleCount; ++index) {
        const auto& scale = graph.destinationScales[index];
        if (destinationScaleRemovedWithSource(
                graph,
                scale.destination,
                sourceId
            )) {
            continue;
        }
        hash = hashBytes(hash, &scale, sizeof(scale));
        ++scaleCount;
    }
    hash = hashBytes(hash, &scaleCount, sizeof(scaleCount));
    return hash;
}

}  // namespace history_detail

}  // namespace core::state::macro
