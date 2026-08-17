#include "state/macro/MacroSlotClipboardPlan.hpp"

#include <algorithm>
#include <bitset>
#include <cmath>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"

namespace core::state::macro {

namespace {

FLASHMEM MacroSlotClipboardPlan disabledPlan(
    MacroSlotClipboardPlan plan,
    core::state::ClipboardTransferReason reason
) {
    plan.availability =
        core::state::ClipboardTransferAvailability::DISABLED;
    plan.reason = reason;
    return plan;
}

FLASHMEM uint8_t existingPageCount(
    const MacroTrackData& track
) {
    int highest = -1;
    for (uint8_t page = 0U; page < PAGE_COUNT; ++page) {
        if (track.isPageEnabled(page)) highest = page;
    }
    return highest < 0
        ? 0U
        : static_cast<uint8_t>(highest + 1);
}

FLASHMEM bool curvePayloadValid(
    const core::state::modulation::ProjectControlCurvePayload& curve,
    const core::state::MacroControlClipboardPointPool& pool
) {
    if (!curve.stored()) return curve.pointCount == 0U;
    if (static_cast<uint32_t>(curve.pointOffset) +
            curve.pointCount >
        pool.used) {
        return false;
    }
    return core::state::modulation::validProjectCurveSpec(
        curve.spec,
        pool.points.data() + curve.pointOffset,
        curve.pointCount
    );
}

FLASHMEM bool clipboardEntryValid(
    const core::state::MacroAutomationClipboardEntry& entry,
    const core::state::MacroControlClipboardPointPool& pool
) {
    const auto& control = entry.control;
    const bool contiguous =
        !control.automation.stored() ||
        !control.recordedShape.stored() ||
        control.recordedShape.pointOffset ==
            static_cast<uint16_t>(
                control.automation.pointOffset +
                control.automation.pointCount
            );
    return entry.valid && entry.sourceMacroActive &&
           entry.sourceCc <= 127U &&
           std::isfinite(entry.sourceStaticValue) &&
           entry.sourceSlotPresent == control.present() &&
           (!control.automation.stored() ||
            control.automation.spec.valueDomain ==
                core::state::modulation::
                    ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR) &&
           (!control.recordedShape.stored() ||
            (control.recordedShape.spec.valueDomain ==
                 core::state::modulation::
                     ProjectCurveValueDomain::BIPOLAR &&
             std::isfinite(control.modulationAmount))) &&
           curvePayloadValid(control.automation, pool) &&
           curvePayloadValid(control.recordedShape, pool) &&
           contiguous &&
           (entry.destinationScaleQ15 ==
                core::state::modulation::
                    PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 ||
            control.recordedShape.stored());
}

struct CapacityCursor {
    uint16_t automationEntries = 0U;
    uint16_t modulatorSources = 0U;
    uint16_t modulationBindings = 0U;
    uint16_t destinationScales = 0U;
    uint16_t curveRecords = 0U;
    uint32_t curvePoints = 0U;
};

FLASHMEM bool applyCapacityDelta(
    CapacityCursor& cursor,
    const MacroPagesState& pages,
    const core::state::modulation::
        ProjectControlMacroDestinationView& target,
    const core::state::MacroAutomationClipboardEntry& source
) {
    using namespace core::state::modulation;

    if (target.automation.stored()) {
        if (cursor.automationEntries == 0U) return false;
        --cursor.automationEntries;
        const auto* curve = findProjectCurve(
            pages.control.authored.curves,
            target.automation.id
        );
        if (curve == nullptr || curve->referenceCount == 0U) {
            return false;
        }
        if (curve->referenceCount == 1U) {
            if (cursor.curveRecords == 0U ||
                cursor.curvePoints < curve->pointCount) {
                return false;
            }
            --cursor.curveRecords;
            cursor.curvePoints -= curve->pointCount;
        }
    }
    if (target.primaryModulation.present()) {
        if (cursor.modulationBindings == 0U) return false;
        --cursor.modulationBindings;
        const auto destination =
            projectControlDestination(target.address);
        if (projectModulationDestinationScaleQ15(
                pages.control.authored.modulation,
                destination
            ) != PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15) {
            if (cursor.destinationScales == 0U) return false;
            --cursor.destinationScales;
        }
    }

    if (!source.sourceSlotPresent) return true;
    const auto& incoming = source.control;
    if (incoming.automation.stored()) {
        if (cursor.automationEntries >=
                PROJECT_AUTOMATION_ENTRY_CAPACITY ||
            cursor.curveRecords >= PROJECT_CURVE_LIVE_CAPACITY ||
            cursor.curveRecords >= PROJECT_CURVE_RECORD_CAPACITY ||
            cursor.curvePoints + incoming.automation.pointCount >
                PROJECT_CURVE_POINT_CAPACITY) {
            return false;
        }
        ++cursor.automationEntries;
        ++cursor.curveRecords;
        cursor.curvePoints += incoming.automation.pointCount;
    }
    if (incoming.recordedShape.stored()) {
        if (cursor.modulatorSources >=
                PROJECT_MODULATOR_CAPACITY ||
            cursor.modulationBindings >=
                PROJECT_MODULATION_BINDING_CAPACITY ||
            cursor.curveRecords >= PROJECT_CURVE_LIVE_CAPACITY ||
            cursor.curveRecords >= PROJECT_CURVE_RECORD_CAPACITY ||
            cursor.curvePoints +
                    incoming.recordedShape.pointCount >
                PROJECT_CURVE_POINT_CAPACITY ||
            (source.destinationScaleQ15 !=
                 PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 &&
             cursor.destinationScales >=
                 PROJECT_MODULATION_DESTINATION_SCALE_CAPACITY)) {
            return false;
        }
        ++cursor.modulatorSources;
        ++cursor.modulationBindings;
        ++cursor.curveRecords;
        cursor.curvePoints +=
            incoming.recordedShape.pointCount;
        if (source.destinationScaleQ15 !=
            PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15) {
            ++cursor.destinationScales;
        }
    }
    return true;
}

FLASHMEM bool assignTargetCcs(
    MacroSlotClipboardPlan& plan,
    const MacroPagesState& pages
) {
    std::bitset<128U> used;
    const auto& track = pages.tracks[plan.targetTrack];
    for (uint8_t page = 0U; page < PAGE_COUNT; ++page) {
        if (!track.isPageEnabled(page)) continue;
        for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
            const uint8_t bit = static_cast<uint8_t>(1U << macro);
            if (!track.pages[page].isMacroActive(macro) ||
                (plan.destinationMasks[page] & bit) != 0U) {
                continue;
            }
            const uint8_t cc = track.pages[page].cc[macro];
            if (cc <= 127U) used.set(cc);
        }
    }

    const uint8_t maxOffset = static_cast<uint8_t>(
        plan.lastTargetLinear - plan.firstTargetLinear
    );
    for (uint16_t attempt = 0U; attempt < 128U; ++attempt) {
        const uint8_t candidate = static_cast<uint8_t>(
            (static_cast<uint16_t>(plan.firstTargetLinear) + attempt) &
            0x007FU
        );
        if (static_cast<uint16_t>(candidate) + maxOffset > 127U) {
            continue;
        }

        bool available = true;
        for (uint8_t index = 0U; index < plan.count; ++index) {
            const auto& entry = plan.entries[index];
            const uint8_t targetCc = static_cast<uint8_t>(
                candidate +
                static_cast<uint8_t>(
                    entry.targetLinear - plan.firstTargetLinear
                )
            );
            if (used[targetCc]) {
                available = false;
                break;
            }
        }
        if (!available) continue;

        for (uint8_t index = 0U; index < plan.count; ++index) {
            auto& entry = plan.entries[index];
            entry.targetCc = static_cast<uint8_t>(
                candidate +
                static_cast<uint8_t>(
                    entry.targetLinear - plan.firstTargetLinear
                )
            );
        }
        return true;
    }
    return false;
}

}  // namespace

FLASHMEM MacroSlotClipboardPlan buildMacroSlotClipboardPlan(
    const core::state::StructureClipboardState& clipboard,
    const MacroPagesState& pages,
    uint8_t targetTrack,
    uint8_t anchorLinear
) {
    MacroSlotClipboardPlan plan;
    plan.clipboardRevision = clipboard.revision.get();
    plan.targetTrack = targetTrack;
    plan.anchorLinear = anchorLinear;

    if (!clipboard.hasMacroSlotSelection() ||
        !clipboard.macroAutomationSet) {
        return disabledPlan(
            plan,
            clipboard.kind.get() ==
                    core::state::StructureClipboardKind::NONE
                ? core::state::ClipboardTransferReason::EMPTY_CLIPBOARD
                : core::state::ClipboardTransferReason::WRONG_PAYLOAD
        );
    }
    if (targetTrack >= TRACK_COUNT ||
        anchorLinear >= MacroSlotClipboardPlan::SLOT_COUNT) {
        return disabledPlan(
            plan,
            core::state::ClipboardTransferReason::OUT_OF_RANGE
        );
    }

    const auto& payload = *clipboard.macroAutomationSet;
    if (!payload.valid || payload.count == 0U ||
        payload.count > payload.entries.size()) {
        return disabledPlan(
            plan,
            core::state::ClipboardTransferReason::INVALID_PAYLOAD
        );
    }
    plan.sourceCount = payload.count;
    plan.existingPageCount = existingPageCount(pages.tracks[targetTrack]);
    plan.allowedPageCount = static_cast<uint8_t>(std::min<uint16_t>(
        PAGE_COUNT,
        static_cast<uint16_t>(plan.existingPageCount) + 1U
    ));

    uint8_t previousSource = MacroSlotClipboardPlan::SLOT_COUNT;
    bool invalidPayload = false;
    bool outOfRange = false;
    bool capacityExhausted = false;
    CapacityCursor capacity{
        .automationEntries =
            pages.control.authored.automation.entryCount,
        .modulatorSources =
            pages.control.authored.modulation.sourceCount,
        .modulationBindings =
            pages.control.authored.modulation.outputBindingCount,
        .destinationScales =
            pages.control.authored.modulation.destinationScaleCount,
        .curveRecords =
            pages.control.authored.curves.recordCount,
        .curvePoints =
            pages.control.authored.curves.pointCount,
    };
    for (uint8_t index = 0U; index < payload.count; ++index) {
        const auto& source = payload.entries[index];
        if (!clipboardEntryValid(source, payload.pointPool) ||
            source.sourcePage >= PAGE_COUNT ||
            source.sourceMacro >= MACRO_COUNT) {
            invalidPayload = true;
            break;
        }
        const uint8_t sourceLinear = static_cast<uint8_t>(
            source.sourcePage * MACRO_COUNT + source.sourceMacro
        );
        if (index > 0U && sourceLinear <= previousSource) {
            invalidPayload = true;
            break;
        }
        previousSource = sourceLinear;
        if (index == 0U) {
            plan.firstSourceLinear = sourceLinear;
            plan.lastSourceLinear = sourceLinear;
            plan.firstTargetLinear = anchorLinear;
        } else {
            plan.lastSourceLinear = sourceLinear;
        }

        const uint16_t targetWide =
            static_cast<uint16_t>(anchorLinear) +
            static_cast<uint16_t>(
                sourceLinear - plan.firstSourceLinear
            );
        if (targetWide >= MacroSlotClipboardPlan::SLOT_COUNT) {
            outOfRange = true;
            continue;
        }
        const uint8_t targetLinear = static_cast<uint8_t>(targetWide);
        const uint8_t targetPage =
            static_cast<uint8_t>(targetLinear / MACRO_COUNT);
        const uint8_t targetMacro =
            static_cast<uint8_t>(targetLinear % MACRO_COUNT);
        if (targetPage >= plan.allowedPageCount) {
            outOfRange = true;
            continue;
        }

        bool overwrite = false;
        core::state::modulation::
            ProjectControlMacroDestinationView target{};
        target.address = {
            .track = targetTrack,
            .page = targetPage,
            .macro = targetMacro,
        };
        if (targetPage < plan.existingPageCount &&
            pages.tracks[targetTrack].isPageEnabled(targetPage)) {
            const MacroAutomationSlotAddress address{
                .track = targetTrack,
                .page = targetPage,
                .macro = targetMacro,
            };
            if (!core::state::modulation::
                    readProjectControlMacroDestination(
                        pages.control,
                        address,
                        target
                    ) ||
                target.mutationAmbiguous() ||
                (target.primaryModulation.present() &&
                 !target.primaryModulation.isRecordedShape())) {
                invalidPayload = true;
                break;
            }
            overwrite =
                pages.pageData(targetTrack, targetPage)
                    .isMacroActive(targetMacro) ||
                target.present();
        }
        if (!applyCapacityDelta(
                capacity,
                pages,
                target,
                source
            )) {
            capacityExhausted = true;
        }

        plan.entries[plan.count++] = MacroSlotClipboardPlanEntry{
            .clipboardIndex = index,
            .sourceLinear = sourceLinear,
            .targetLinear = targetLinear,
            .targetPage = targetPage,
            .targetMacro = targetMacro,
            .overwrite = overwrite,
        };
        const uint8_t bit = static_cast<uint8_t>(1U << targetMacro);
        plan.destinationMasks[targetPage] = static_cast<uint8_t>(
            plan.destinationMasks[targetPage] | bit
        );
        if (overwrite) {
            plan.overwriteMasks[targetPage] = static_cast<uint8_t>(
                plan.overwriteMasks[targetPage] | bit
            );
            ++plan.overwriteCount;
        }
        plan.lastTargetLinear = targetLinear;
    }

    if (invalidPayload) {
        return disabledPlan(
            plan,
            core::state::ClipboardTransferReason::INVALID_PAYLOAD
        );
    }
    if (outOfRange || plan.count != plan.sourceCount) {
        return disabledPlan(
            plan,
            core::state::ClipboardTransferReason::OUT_OF_RANGE
        );
    }
    if (capacityExhausted) {
        return disabledPlan(
            plan,
            core::state::ClipboardTransferReason::CAPACITY
        );
    }
    if (!assignTargetCcs(plan, pages)) {
        return disabledPlan(
            plan,
            core::state::ClipboardTransferReason::CAPACITY
        );
    }

    const uint8_t lastTargetPage = static_cast<uint8_t>(
        plan.lastTargetLinear / MACRO_COUNT
    );
    plan.requiredPageCount = static_cast<uint8_t>(
        std::max<uint16_t>(
            plan.existingPageCount,
            static_cast<uint16_t>(lastTargetPage) + 1U
        )
    );
    for (uint8_t page = plan.existingPageCount;
         page < plan.requiredPageCount;
         ++page) {
        plan.createPageMask = static_cast<uint16_t>(
            plan.createPageMask |
            static_cast<uint16_t>(1U << page)
        );
    }
    plan.availability = plan.overwriteCount > 0U
        ? core::state::ClipboardTransferAvailability::WARNING
        : core::state::ClipboardTransferAvailability::READY;
    plan.reason = core::state::ClipboardTransferReason::NONE;
    return plan;
}

FLASHMEM bool sameMacroSlotClipboardPlan(
    const MacroSlotClipboardPlan& lhs,
    const MacroSlotClipboardPlan& rhs
) {
    if (lhs.clipboardRevision != rhs.clipboardRevision ||
        lhs.sourceCount != rhs.sourceCount ||
        lhs.count != rhs.count ||
        lhs.firstSourceLinear != rhs.firstSourceLinear ||
        lhs.lastSourceLinear != rhs.lastSourceLinear ||
        lhs.targetTrack != rhs.targetTrack ||
        lhs.anchorLinear != rhs.anchorLinear ||
        lhs.firstTargetLinear != rhs.firstTargetLinear ||
        lhs.lastTargetLinear != rhs.lastTargetLinear ||
        lhs.existingPageCount != rhs.existingPageCount ||
        lhs.allowedPageCount != rhs.allowedPageCount ||
        lhs.requiredPageCount != rhs.requiredPageCount ||
        lhs.overwriteCount != rhs.overwriteCount ||
        lhs.createPageMask != rhs.createPageMask ||
        lhs.availability != rhs.availability ||
        lhs.reason != rhs.reason ||
        lhs.destinationMasks != rhs.destinationMasks ||
        lhs.overwriteMasks != rhs.overwriteMasks) {
        return false;
    }
    for (uint8_t index = 0U; index < lhs.count; ++index) {
        const auto& left = lhs.entries[index];
        const auto& right = rhs.entries[index];
        if (left.clipboardIndex != right.clipboardIndex ||
            left.sourceLinear != right.sourceLinear ||
            left.targetLinear != right.targetLinear ||
            left.targetPage != right.targetPage ||
            left.targetMacro != right.targetMacro ||
            left.targetCc != right.targetCc ||
            left.overwrite != right.overwrite) {
            return false;
        }
    }
    return true;
}

}  // namespace core::state::macro
