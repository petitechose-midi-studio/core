#include "state/macro/MacroHistoryInternals.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
namespace core::state::macro {

using namespace history_detail;

FLASHMEM bool MacroHistoryService::undo(
    MacroPagesState& pages,
    MacroAutomationSlotAddress* appliedAddress,
    MacroManualOverrideState* manualOverrides,
    core::state::project::ProjectTrackState* projectTracks
) {
    return replay_(
        core::state::project::ProjectHistoryDirection::Undo,
        pages,
        appliedAddress,
        manualOverrides,
        projectTracks
    );
}

FLASHMEM bool MacroHistoryService::replay_(
    core::state::project::ProjectHistoryDirection direction,
    MacroPagesState& pages,
    MacroAutomationSlotAddress* appliedAddress,
    MacroManualOverrideState* manualOverrides,
    core::state::project::ProjectTrackState* projectTracks
) {
    const bool redo =
        direction == core::state::project::ProjectHistoryDirection::Redo;
    endCoalescing();
    if (pendingModulatorSlot_() != nullptr) return false;
    auto& sourceStack = redo ? redo_ : undo_;
    auto& sourceCount = redo ? redo_count_ : undo_count_;
    if (sourceCount == 0U) return false;
    auto& change = sourceStack[sourceCount - 1U];
    if (!change) return false;
    const uintptr_t projectHistoryIdentity =
        reinterpret_cast<uintptr_t>(change.get());
    if (change->kind == MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT ||
        change->kind == MacroHistoryActionKind::CREATE_PROJECT_MODULATOR) {
        if (redo) {
            if (!creationBeforeMatches(
                    pages,
                    change->address,
                    change->modulator
                )) {
                return false;
            }
            restoreCreationAfter(pages, change->address, change->modulator);
        } else {
            if (!creationIdentityMatches(
                    pages,
                    change->address,
                    change->modulator,
                    true
                )) {
                return false;
            }
            restoreCreationBefore(
                pages,
                change->address,
                change->modulator,
                false
            );
        }
    } else if (change->modulatorSplit) {
        const bool restored = redo
            ? restoreSplitAfter(pages, *change->modulatorSplit)
            : restoreSplitBefore(pages, *change->modulatorSplit);
        if (!restored) {
            return false;
        }
    } else if (change->modulatorDelete) {
        if (redo) {
            if (!deleteBeforeMatches(pages, *change->modulatorDelete) ||
                !core::state::modulation::deleteProjectModulator(
                     pages.control.authored.modulation,
                     pages.control.authored.curves,
                     change->modulatorDelete->source.id
                 ).changed()) {
                return false;
            }
            pages.control.markAuthoredMutation();
        } else if (!restoreDeletedModulator(
                       pages,
                       *change->modulatorDelete
                   )) {
            return false;
        }
    } else if (change->triggerEdit.valid) {
        const auto& expected = redo
            ? change->triggerEdit.before
            : change->triggerEdit.after;
        const auto& target = redo
            ? change->triggerEdit.after
            : change->triggerEdit.before;
        auto* trigger =
            core::state::modulation::findProjectModulationTriggerForSource(
                pages.control.authored.modulation,
                expected.sourceId
            );
        if (trigger == nullptr || !sameObjectBits(*trigger, expected)) {
            return false;
        }
        *trigger = target;
        pages.control.markAuthoredMutation();
    } else if (change->recordedShapeEdit) {
        if (!applyRecordedShapeEdit(
                pages,
                *change->recordedShapeEdit,
                redo
            )) {
            return false;
        }
    } else if (change->sourceEdit.valid) {
        const auto& expected = redo
            ? change->sourceEdit.before
            : change->sourceEdit.after;
        const auto& target = redo
            ? change->sourceEdit.after
            : change->sourceEdit.before;
        auto* source = core::state::modulation::findProjectModulator(
            pages.control.authored.modulation,
            expected.id
        );
        if (source == nullptr || !sameObjectBits(*source, expected)) {
            return false;
        }
        *source = target;
        pages.control.markAuthoredMutation();
    } else if (change->destinationScale.valid) {
        auto& graph = pages.control.authored.modulation;
        const auto& scale = change->destinationScale;
        const uint16_t expected = redo
            ? scale.beforeScaleQ15
            : scale.afterScaleQ15;
        const uint16_t target = redo
            ? scale.afterScaleQ15
            : scale.beforeScaleQ15;
        if (core::state::modulation::projectModulationDestinationScaleQ15(
                graph,
                scale.destination
            ) != expected ||
            !core::state::modulation::setProjectModulationDestinationScale(
                graph,
                scale.destination,
                target
            ).changed()) {
            return false;
        }
        pages.control.markAuthoredMutation();
    } else if (change->auxiliary && change->auxiliary->trackConfig.valid) {
        const auto& config = change->auxiliary->trackConfig;
        const auto& expectedCc = redo ? config.beforeCc : config.afterCc;
        const auto& targetCc = redo ? config.afterCc : config.beforeCc;
        const auto& expectedTracks = redo
            ? config.beforeTracks
            : config.afterTracks;
        const auto& targetTracks = redo
            ? config.afterTracks
            : config.beforeTracks;
        if (projectTracks == nullptr || config.track >= TRACK_COUNT ||
            config.page >= PAGE_COUNT ||
            pages.pageData(config.track, config.page).cc != expectedCc ||
            !core::state::project::sameProjectTrackSnapshot(
                projectTracks->authored,
                expectedTracks
            )) {
            return false;
        }
        if (!core::state::project::sameProjectTrackSnapshot(
                config.beforeTracks,
                config.afterTracks
            ) && !core::state::project::applyProjectTrackSnapshot(
                *projectTracks,
                targetTracks
            ).changed()) {
            return false;
        }
        pages.pageData(config.track, config.page).cc = targetCc;
        pages.updateActiveConfigs();
    } else if (change->auxiliary && change->auxiliary->trackRouting.valid) {
        const auto& routing = change->auxiliary->trackRouting;
        if (projectTracks == nullptr || change->slot == nullptr) return false;
        const auto& expectedRouting = redo ? routing.before : routing.after;
        const auto& targetRouting = redo ? routing.after : routing.before;
        const auto& expectedSlot = redo
            ? change->slot->before
            : change->slot->after;
        const auto& targetSlot = redo
            ? change->slot->after
            : change->slot->before;
        if (!core::state::project::sameProjectTrackSnapshot(
                projectTracks->authored,
                expectedRouting
            ) ||
            !liveMacroSlotMatchesHistorySnapshot(
                pages,
                expectedSlot
            )) {
            return false;
        }
        if (!core::state::project::applyProjectTrackSnapshot(
                *projectTracks,
                targetRouting
            ).changed()) {
            return false;
        }
        if (!applyMacroSlotHistorySnapshot(pages, targetSlot)) {
            (void)core::state::project::applyProjectTrackSnapshot(
                *projectTracks,
                expectedRouting
            );
            return false;
        }
    } else if (change->auxiliary &&
               change->auxiliary->manualOverride.valid) {
        const auto& manual = change->auxiliary->manualOverride;
        const bool expectedActive = redo
            ? manual.beforeActive
            : manual.afterActive;
        const float expectedValue = redo
            ? manual.beforeValue
            : manual.afterValue;
        const bool targetActive = redo
            ? manual.afterActive
            : manual.beforeActive;
        const float targetValue = redo
            ? manual.afterValue
            : manual.beforeValue;
        if (manualOverrides == nullptr ||
            !manualOverrideMatches(
                *manualOverrides,
                change->address,
                expectedActive,
                expectedValue
            ) ||
            !canApplyManualOverride(
                *manualOverrides,
                change->address,
                targetActive
            )) {
            return false;
        }
        auto& page = pages.pageData(change->address.track, change->address.page);
        const float expectedBase = redo
            ? change->valueEdit.before
            : change->valueEdit.after;
        const float targetBase = redo
            ? change->valueEdit.after
            : change->valueEdit.before;
        if (change->valueEdit.valid &&
            !sameFloatBits(
                page.values[change->address.macro],
                expectedBase
            )) {
            return false;
        }
        if (!applyManualOverride(
                *manualOverrides,
                change->address,
                targetActive,
                targetValue
            )) {
            return false;
        }
        if (change->valueEdit.valid) {
            page.values[change->address.macro] = targetBase;
        }
    } else if (change->automation && change->automation->metadata.valid) {
        if (!applyAutomationMetadataHistory(
                pages,
                change->address,
                change->automation->metadata,
                redo
            )) {
            return false;
        }
    } else if (change->valueEdit.valid) {
        auto& page = pages.pageData(change->address.track, change->address.page);
        const float expected = redo
            ? change->valueEdit.before
            : change->valueEdit.after;
        const float target = redo
            ? change->valueEdit.after
            : change->valueEdit.before;
        if (!sameFloatBits(
                page.values[change->address.macro],
                expected
            )) {
            return false;
        }
        page.values[change->address.macro] = target;
    } else if (change->pageStructure) {
        if (!applyPageStructureHistory(
                pages,
                *change->pageStructure,
                redo
            )) {
            return false;
        }
    } else if (change->slotDeletion) {
        const auto& expected = redo
            ? change->slotDeletion->before
            : change->slotDeletion->after;
        const auto& target = redo
            ? change->slotDeletion->after
            : change->slotDeletion->before;
        if (!liveMacroSlotDeletionStateMatches(
                pages,
                change->address,
                expected
            ) || !applyMacroSlotDeletionState(
                pages,
                change->address,
                target
            )) {
            return false;
        }
    } else if (change->modulationAssignments) {
        const auto& expected = redo
            ? change->modulationAssignments->before
            : change->modulationAssignments->after;
        const auto& target = redo
            ? change->modulationAssignments->after
            : change->modulationAssignments->before;
        if (!liveModulationAssignmentsMatch(
                pages,
                expected
            ) ||
            !applyModulationAssignments(
                pages,
                target
            )) {
            return false;
        }
    } else if (change->automationTake) {
        if (!liveAutomationTakeMatches(
                pages,
                *change->automationTake,
                !redo
            ) || !applyAutomationTakeAtomically(
                pages,
                *change->automationTake,
                redo
            )) {
            return false;
        }
    } else if (change->automation) {
        const auto& expected = redo
            ? change->automation->before
            : change->automation->after;
        const auto& target = redo
            ? change->automation->after
            : change->automation->before;
        if (!liveMacroAutomationMatchesHistorySnapshot(
                pages,
                expected
            ) ||
            !applyMacroAutomationHistorySnapshot(
                pages,
                target
            )) {
            return false;
        }
    } else {
        if (!change->slot) return false;
        const auto& expected = redo
            ? change->slot->before
            : change->slot->after;
        const auto& target = redo
            ? change->slot->after
            : change->slot->before;
        if (!liveMacroSlotMatchesHistorySnapshot(
                pages,
                expected
            ) ||
            !applyMacroSlotHistorySnapshot(pages, target)) {
            return false;
        }
    }
    auto applied = std::move(change);
    if (appliedAddress != nullptr) *appliedAddress = applied->address;
    --sourceCount;
    auto& targetStack = redo ? undo_ : redo_;
    auto& targetCount = redo ? undo_count_ : redo_count_;
    push_(
        targetStack,
        targetCount,
        std::move(applied),
        project_history_sink_
    );
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(
            core::state::project::ProjectHistoryDomain::Macro,
            projectHistoryIdentity,
            direction
        );
    }
    return true;
}

}  // namespace core::state::macro
