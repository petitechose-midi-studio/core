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

FLASHMEM bool MacroHistoryService::redo(
    MacroPagesState& pages,
    MacroAutomationSlotAddress* appliedAddress,
    MacroManualOverrideState* manualOverrides,
    core::state::project::ProjectTrackState* projectTracks
) {
    endCoalescing();
    if (pendingModulatorSlot_() != nullptr) return false;
    if (redo_count_ == 0) return false;
    auto& change = redo_[redo_count_ - 1U];
    if (!change) return false;
    const uintptr_t projectHistoryIdentity =
        reinterpret_cast<uintptr_t>(change.get());
    if (change->kind == MacroHistoryActionKind::CREATE_MODULATOR_ASSIGNMENT ||
        change->kind == MacroHistoryActionKind::CREATE_PROJECT_MODULATOR) {
        if (!creationBeforeMatches(
                pages,
                change->address,
                change->modulator
            )) {
            return false;
        }
        restoreCreationAfter(pages, change->address, change->modulator);
    } else if (change->modulatorSplit) {
        if (!restoreSplitAfter(pages, *change->modulatorSplit)) {
            return false;
        }
    } else if (change->modulatorDelete) {
        if (!deleteBeforeMatches(pages, *change->modulatorDelete) ||
            !core::state::modulation::deleteProjectModulator(
                 pages.control.authored.modulation,
                 pages.control.authored.curves,
                 change->modulatorDelete->source.id
             ).changed()) {
            return false;
        }
        pages.control.markAuthoredMutation();
    } else if (change->triggerEdit.valid) {
        auto* trigger =
            core::state::modulation::findProjectModulationTriggerForSource(
                pages.control.authored.modulation,
                change->triggerEdit.before.sourceId
            );
        if (trigger == nullptr ||
            !sameObjectBits(*trigger, change->triggerEdit.before)) {
            return false;
        }
        *trigger = change->triggerEdit.after;
        pages.control.markAuthoredMutation();
    } else if (change->recordedShapeEdit) {
        if (!applyRecordedShapeEdit(
                pages,
                *change->recordedShapeEdit,
                true
            )) {
            return false;
        }
    } else if (change->sourceEdit.valid) {
        auto* source = core::state::modulation::findProjectModulator(
            pages.control.authored.modulation,
            change->sourceEdit.before.id
        );
        if (source == nullptr ||
            !sameObjectBits(*source, change->sourceEdit.before)) {
            return false;
        }
        *source = change->sourceEdit.after;
        pages.control.markAuthoredMutation();
    } else if (change->destinationScale.valid) {
        auto& graph = pages.control.authored.modulation;
        const auto& scale = change->destinationScale;
        if (core::state::modulation::projectModulationDestinationScaleQ15(
                graph,
                scale.destination
            ) != scale.beforeScaleQ15 ||
            !core::state::modulation::setProjectModulationDestinationScale(
                graph,
                scale.destination,
                scale.afterScaleQ15
            ).changed()) {
            return false;
        }
        pages.control.markAuthoredMutation();
    } else if (change->auxiliary && change->auxiliary->trackConfig.valid) {
        const auto& config = change->auxiliary->trackConfig;
        if (projectTracks == nullptr || config.track >= TRACK_COUNT ||
            config.page >= PAGE_COUNT ||
            pages.pageData(config.track, config.page).cc != config.beforeCc ||
            !core::state::project::sameProjectTrackSnapshot(
                projectTracks->authored,
                config.beforeTracks
            )) {
            return false;
        }
        if (!core::state::project::sameProjectTrackSnapshot(
                config.beforeTracks,
                config.afterTracks
            ) && !core::state::project::applyProjectTrackSnapshot(
                *projectTracks,
                config.afterTracks
            ).changed()) {
            return false;
        }
        pages.pageData(config.track, config.page).cc = config.afterCc;
        pages.updateActiveConfigs();
    } else if (change->auxiliary && change->auxiliary->trackRouting.valid) {
        const auto& routing = change->auxiliary->trackRouting;
        if (projectTracks == nullptr || change->slot == nullptr ||
            !core::state::project::sameProjectTrackSnapshot(
                projectTracks->authored,
                routing.before
            ) ||
            !liveMacroSlotMatchesHistorySnapshot(
                pages,
                change->slot->before
            )) {
            return false;
        }
        if (!core::state::project::applyProjectTrackSnapshot(
                *projectTracks,
                routing.after
            ).changed()) {
            return false;
        }
        if (!applyMacroSlotHistorySnapshot(pages, change->slot->after)) {
            (void)core::state::project::applyProjectTrackSnapshot(
                *projectTracks,
                routing.before
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
                manual.beforeActive,
                manual.beforeValue
            ) ||
            !canApplyManualOverride(
                *manualOverrides,
                change->address,
                manual.afterActive
            )) {
            return false;
        }
        auto& page = pages.pageData(change->address.track, change->address.page);
        if (change->valueEdit.valid &&
            !sameFloatBits(
                page.values[change->address.macro],
                change->valueEdit.before
            )) {
            return false;
        }
        if (!applyManualOverride(
                *manualOverrides,
                change->address,
                manual.afterActive,
                manual.afterValue
            )) {
            return false;
        }
        if (change->valueEdit.valid) {
            page.values[change->address.macro] = change->valueEdit.after;
        }
    } else if (change->valueEdit.valid) {
        auto& page = pages.pageData(change->address.track, change->address.page);
        if (!sameFloatBits(
                page.values[change->address.macro],
                change->valueEdit.before
            )) {
            return false;
        }
        page.values[change->address.macro] = change->valueEdit.after;
    } else if (change->pageStructure) {
        if (!applyPageStructureHistory(
                pages,
                *change->pageStructure,
                true
            )) {
            return false;
        }
    } else if (change->slotRemoval) {
        if (!liveMacroSlotRemovalStateMatches(
                pages,
                change->address,
                change->slotRemoval->before
            ) || !applyMacroSlotRemovalState(
                pages,
                change->address,
                change->slotRemoval->after
            )) {
            return false;
        }
    } else if (change->modulationAssignments) {
        if (!liveModulationAssignmentsMatch(
                pages,
                change->modulationAssignments->before
            ) ||
            !applyModulationAssignments(
                pages,
                change->modulationAssignments->after
            )) {
            return false;
        }
    } else if (change->automationTake) {
        if (!liveAutomationTakeMatches(
                pages,
                *change->automationTake,
                false
            ) || !applyAutomationTakeAtomically(
                pages,
                *change->automationTake,
                true
            )) {
            return false;
        }
    } else if (change->automation) {
        if (!liveMacroAutomationMatchesHistorySnapshot(
                pages,
                change->automation->before
            ) ||
            !applyMacroAutomationHistorySnapshot(
                pages,
                change->automation->after
            )) {
            return false;
        }
    } else {
        if (!change->slot ||
            !liveMacroSlotMatchesHistorySnapshot(
                pages,
                change->slot->before
            ) ||
            !applyMacroSlotHistorySnapshot(pages, change->slot->after)) {
            return false;
        }
    }
    auto applied = std::move(change);
    if (appliedAddress != nullptr) *appliedAddress = applied->address;
    --redo_count_;
    push_(undo_, undo_count_, std::move(applied), project_history_sink_);
    if (project_history_sink_ != nullptr) {
        project_history_sink_->notifyApplied(
            core::state::project::ProjectHistoryDomain::Macro,
            projectHistoryIdentity,
            core::state::project::ProjectHistoryDirection::Redo
        );
    }
    return true;
}

}  // namespace core::state::macro
