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

using namespace history_detail;

FLASHMEM bool MacroHistoryService::undo(
    MacroPagesState& pages,
    MacroAutomationSlotAddress* appliedAddress,
    MacroManualOverrideState* manualOverrides,
    core::state::project::ProjectTrackState* projectTracks
) {
    endCoalescing();
    if (pendingModulatorSlot_() != nullptr) return false;
    if (undo_count_ == 0) return false;
    auto& change = undo_[undo_count_ - 1U];
    if (!change) return false;
    const uintptr_t projectHistoryIdentity =
        reinterpret_cast<uintptr_t>(change.get());
    if (change->kind == MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT ||
        change->kind == MacroHistoryActionKind::CREATE_PROJECT_MODULATOR) {
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
    } else if (change->modulatorSplit) {
        if (!restoreSplitBefore(pages, *change->modulatorSplit)) {
            return false;
        }
    } else if (change->modulatorDelete) {
        if (!restoreDeletedModulator(pages, *change->modulatorDelete)) {
            return false;
        }
    } else if (change->triggerEdit.valid) {
        auto* trigger =
            core::state::modulation::findProjectModulationTriggerForSource(
                pages.control.authored.modulation,
                change->triggerEdit.after.sourceId
            );
        if (trigger == nullptr ||
            !sameObjectBits(*trigger, change->triggerEdit.after)) {
            return false;
        }
        *trigger = change->triggerEdit.before;
        pages.control.markAuthoredMutation();
    } else if (change->recordedShapeEdit) {
        if (!applyRecordedShapeEdit(
                pages,
                *change->recordedShapeEdit,
                false
            )) {
            return false;
        }
    } else if (change->sourceEdit.valid) {
        auto* source = core::state::modulation::findProjectModulator(
            pages.control.authored.modulation,
            change->sourceEdit.after.id
        );
        if (source == nullptr ||
            !sameObjectBits(*source, change->sourceEdit.after)) {
            return false;
        }
        *source = change->sourceEdit.before;
        pages.control.markAuthoredMutation();
    } else if (change->destinationScale.valid) {
        auto& graph = pages.control.authored.modulation;
        const auto& scale = change->destinationScale;
        if (core::state::modulation::projectModulationDestinationScaleQ15(
                graph,
                scale.destination
            ) != scale.afterScaleQ15 ||
            !core::state::modulation::setProjectModulationDestinationScale(
                graph,
                scale.destination,
                scale.beforeScaleQ15
            ).changed()) {
            return false;
        }
        pages.control.markAuthoredMutation();
    } else if (change->auxiliary && change->auxiliary->trackConfig.valid) {
        const auto& config = change->auxiliary->trackConfig;
        if (projectTracks == nullptr || config.track >= TRACK_COUNT ||
            config.page >= PAGE_COUNT ||
            pages.pageData(config.track, config.page).cc != config.afterCc ||
            !core::state::project::sameProjectTrackSnapshot(
                projectTracks->authored,
                config.afterTracks
            )) {
            return false;
        }
        if (!core::state::project::sameProjectTrackSnapshot(
                config.beforeTracks,
                config.afterTracks
            ) && !core::state::project::applyProjectTrackSnapshot(
                *projectTracks,
                config.beforeTracks
            ).changed()) {
            return false;
        }
        pages.pageData(config.track, config.page).cc = config.beforeCc;
        pages.updateActiveConfigs();
    } else if (change->auxiliary && change->auxiliary->trackRouting.valid) {
        const auto& routing = change->auxiliary->trackRouting;
        if (projectTracks == nullptr || change->slot == nullptr ||
            !core::state::project::sameProjectTrackSnapshot(
                projectTracks->authored,
                routing.after
            ) ||
            !liveMacroSlotMatchesHistorySnapshot(
                pages,
                change->slot->after
            )) {
            return false;
        }
        if (!core::state::project::applyProjectTrackSnapshot(
                *projectTracks,
                routing.before
            ).changed()) {
            return false;
        }
        if (!applyMacroSlotHistorySnapshot(pages, change->slot->before)) {
            (void)core::state::project::applyProjectTrackSnapshot(
                *projectTracks,
                routing.after
            );
            return false;
        }
    } else if (change->auxiliary &&
               change->auxiliary->manualOverride.valid) {
        const auto& manual = change->auxiliary->manualOverride;
        if (manualOverrides == nullptr ||
            !manualOverrideMatches(
                *manualOverrides,
                change->address,
                manual.afterActive,
                manual.afterValue
            ) ||
            !canApplyManualOverride(
                *manualOverrides,
                change->address,
                manual.beforeActive
            )) {
            return false;
        }
        auto& page = pages.pageData(change->address.track, change->address.page);
        if (change->valueEdit.valid &&
            !sameFloatBits(
                page.values[change->address.macro],
                change->valueEdit.after
            )) {
            return false;
        }
        if (!applyManualOverride(
                *manualOverrides,
                change->address,
                manual.beforeActive,
                manual.beforeValue
            )) {
            return false;
        }
        if (change->valueEdit.valid) {
            page.values[change->address.macro] = change->valueEdit.before;
        }
    } else if (change->automation && change->automation->metadata.valid) {
        if (!applyAutomationMetadataHistory(
                pages,
                change->address,
                change->automation->metadata,
                false
            )) {
            return false;
        }
    } else if (change->valueEdit.valid) {
        auto& page = pages.pageData(change->address.track, change->address.page);
        if (!sameFloatBits(
                page.values[change->address.macro],
                change->valueEdit.after
            )) {
            return false;
        }
        page.values[change->address.macro] = change->valueEdit.before;
    } else if (change->pageStructure) {
        if (!applyPageStructureHistory(
                pages,
                *change->pageStructure,
                false
            )) {
            return false;
        }
    } else if (change->slotRemoval) {
        if (!liveMacroSlotRemovalStateMatches(
                pages,
                change->address,
                change->slotRemoval->after
            ) || !applyMacroSlotRemovalState(
                pages,
                change->address,
                change->slotRemoval->before
            )) {
            return false;
        }
    } else if (change->modulationAssignments) {
        if (!liveModulationAssignmentsMatch(
                pages,
                change->modulationAssignments->after
            ) ||
            !applyModulationAssignments(
                pages,
                change->modulationAssignments->before
            )) {
            return false;
        }
    } else if (change->automationTake) {
        if (!liveAutomationTakeMatches(
                pages,
                *change->automationTake,
                true
            ) || !applyAutomationTakeAtomically(
                pages,
                *change->automationTake,
                false
            )) {
            return false;
        }
    } else if (change->automation) {
        if (!liveMacroAutomationMatchesHistorySnapshot(
                pages,
                change->automation->after
            ) ||
            !applyMacroAutomationHistorySnapshot(
                pages,
                change->automation->before
            )) {
            return false;
        }
    } else {
        if (!change->slot ||
            !liveMacroSlotMatchesHistorySnapshot(
                pages,
                change->slot->after
            ) ||
            !applyMacroSlotHistorySnapshot(pages, change->slot->before)) {
            return false;
        }
    }
    auto applied = std::move(change);
    if (appliedAddress != nullptr) *appliedAddress = applied->address;
    --undo_count_;
    push_(redo_, redo_count_, std::move(applied), project_history_sink_);
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(
            core::state::project::ProjectHistoryDomain::Macro,
            projectHistoryIdentity,
            core::state::project::ProjectHistoryDirection::Undo
        );
    }
    return true;
}

}  // namespace core::state::macro
