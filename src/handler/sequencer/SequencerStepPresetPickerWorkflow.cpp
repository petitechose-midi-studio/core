#include "handler/sequencer/SequencerStepPresetPickerWorkflow.hpp"

#include <algorithm>
#include <cstring>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/type/Result.hpp>

#include "config/Timing.hpp"
#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"

namespace core::handler {

namespace contextual = core::state::contextual;

namespace {

constexpr uint8_t DETAIL_ROW_COUNT = 5;
constexpr uint8_t PREVIEW_DETAIL_ROW = 4;

FLASHMEM contextual::ContextActionVariant activeVariant(
    const contextual::ContextActionSpec& spec
) {
    return contextual::hasHoldAction(spec) ? spec.hold : spec.tap;
}

FLASHMEM contextual::ContextActionReason reasonForResult(
    const SequencerStepPresetActionResult& result
) {
    switch (result.status) {
        case SequencerStepPresetStatus::CAPACITY:
            return contextual::ContextActionReason::CAPACITY;
        case SequencerStepPresetStatus::CORRUPT:
            return contextual::ContextActionReason::CORRUPT_ASSET;
        case SequencerStepPresetStatus::UNSUPPORTED_VERSION:
            return contextual::ContextActionReason::UNSUPPORTED_VERSION;
        case SequencerStepPresetStatus::STALE_TARGET:
            return contextual::ContextActionReason::STALE_TARGET;
        case SequencerStepPresetStatus::COLLISION:
            return contextual::ContextActionReason::CONFLICT;
        case SequencerStepPresetStatus::STORAGE_UNAVAILABLE:
            return contextual::ContextActionReason::STORAGE_UNAVAILABLE;
        case SequencerStepPresetStatus::INCOMPATIBLE:
            return contextual::ContextActionReason::INCOMPATIBLE;
        default:
            return contextual::ContextActionReason::FAILED;
    }
}

}  // namespace

FLASHMEM SequencerStepPresetPickerWorkflow::SequencerStepPresetPickerWorkflow(
    core::state::sequencer::SequencerState& sequencer,
    SequencerStepPresetDomainServices& stepPresets,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
)
    : sequencer_(sequencer)
    , step_presets_(stepPresets)
    , overlays_(overlays) {}

FLASHMEM void SequencerStepPresetPickerWorkflow::open() {
    if (!sequencer_.stepEdit.visible.get()) return;

    sequencer_.stepEdit.contextHold.clear();
    sequencer_.stepEdit.localVariationEditActive.set(false);
    auto& picker = sequencer_.stepPresetPicker;
    picker.open(core::state::sequencer::SequencerStepPresetPickerMode::LOAD);
    picker.frozenTarget = step_presets_.captureTarget();
    load_page_cache_ = {};
    refreshFirstPage();
    overlays_.show(core::ui::OverlayType::SEQ_STEP_PRESET, true);
}

FLASHMEM void SequencerStepPresetPickerWorkflow::close() {
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::SEQ_STEP_PRESET);
    sequencer_.stepPresetPicker.reset();
    load_page_cache_ = {};
}

FLASHMEM void SequencerStepPresetPickerWorkflow::refreshFirstPage() {
    (void)refreshPage(
        nullptr,
        core::persistence::StepPresetFilePageDirection::FORWARD,
        false
    );
}

FLASHMEM bool SequencerStepPresetPickerWorkflow::refreshPage(
    const char* anchorExclusive,
    core::persistence::StepPresetFilePageDirection direction,
    bool selectLast
) {
    auto& picker = sequencer_.stepPresetPicker;
    SequencerStepPresetDomainServices::Entry entries[PickerState::ENTRY_CAPACITY]{};
    const auto listed = step_presets_.listPresetsPage(
        entries,
        PickerState::ENTRY_CAPACITY,
        anchorExclusive,
        direction
    );
    if (!listed.ok()) {
        for (uint8_t i = 0; i < PickerState::ENTRY_CAPACITY; ++i) {
            picker.setEntry(i, nullptr);
        }
        picker.entryCount.set(0);
        picker.truncated.set(false);
        picker.hasPreviousPage.set(false);
        picker.hasNextPage.set(false);
        picker.totalEntryCount.set(0);
        picker.descriptor = {};
        picker.setFeedback(core::state::sequencer::SequencerStepPresetFeedback::FAILED);
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::FAILED,
            contextual::ContextActionReason::STORAGE_UNAVAILABLE,
            contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
            0
        );
        return false;
    }

    for (uint8_t i = 0; i < PickerState::ENTRY_CAPACITY; ++i) {
        picker.setEntry(
            i,
            i < listed.count ? entries[i].id : nullptr,
            i < listed.count ? entries[i].semanticName : nullptr,
            i < listed.count && entries[i].metadataReadable
        );
    }
    picker.entryCount.set(listed.count);
    picker.truncated.set(listed.truncated);
    picker.hasPreviousPage.set(listed.hasPrevious);
    picker.hasNextPage.set(listed.hasNext);
    picker.totalEntryCount.set(listed.totalCount);
    picker.descriptor = {};
    picker.detailVisible.set(false);
    picker.detailFocus.set(0);
    picker.operationFeedback.set({});

    if (picker.itemCount() == 0) {
        picker.selectedIndex.set(0);
        picker.setFeedback(core::state::sequencer::SequencerStepPresetFeedback::EMPTY);
        return true;
    }

    picker.selectedIndex.set(
        selectLast ? static_cast<uint8_t>(picker.itemCount() - 1U) : 0U
    );
    picker.feedback.set(core::state::sequencer::SequencerStepPresetFeedback::NONE);
    inspectFocused(true);
    picker.bump();
    return true;
}

FLASHMEM bool
SequencerStepPresetPickerWorkflow::refreshPageContainingAndSelect(
    const char* presetId
) {
    if (presetId == nullptr || presetId[0] == '\0') return false;

    // Cursor pages are deliberately bounded and the directory is not retained
    // in RAM. Find the last full page of entries before the asset, then use its
    // first entry as an exclusive forward anchor. This takes two bounded list
    // passes and guarantees that the requested asset is part of the refreshed
    // page without walking an unbounded number of pages.
    char pageAnchor[PickerState::ID_SIZE]{};
    bool hasPageAnchor = false;
    {
        SequencerStepPresetDomainServices::Entry preceding[
            PickerState::ENTRY_CAPACITY
        ]{};
        const auto listed = step_presets_.listPresetsPage(
            preceding,
            PickerState::ENTRY_CAPACITY,
            presetId,
            core::persistence::StepPresetFilePageDirection::BACKWARD
        );
        if (!listed.ok()) {
            refreshFirstPage();
            return false;
        }
        if (listed.count == PickerState::ENTRY_CAPACITY) {
            std::strncpy(
                pageAnchor,
                preceding[0].id,
                sizeof(pageAnchor) - 1U
            );
            hasPageAnchor = true;
        }
    }

    if (!refreshPage(
            hasPageAnchor ? pageAnchor : nullptr,
            core::persistence::StepPresetFilePageDirection::FORWARD,
            false
        )) {
        return false;
    }

    auto& picker = sequencer_.stepPresetPicker;
    for (uint8_t i = 0; i < picker.entryCount.get(); ++i) {
        if (std::strcmp(picker.entryId(i), presetId) != 0) continue;
        picker.selectedIndex.set(i);
        inspectFocused(true);
        return true;
    }
    return false;
}

FLASHMEM void SequencerStepPresetPickerWorkflow::cacheLoadPage() {
    const auto& picker = sequencer_.stepPresetPicker;
    load_page_cache_.valid = true;
    load_page_cache_.selectedIndex = picker.selectedIndex.get();
    load_page_cache_.entryCount = picker.entryCount.get();
    load_page_cache_.hasPrevious = picker.hasPreviousPage.get();
    load_page_cache_.hasNext = picker.hasNextPage.get();
    load_page_cache_.totalCount = picker.totalEntryCount.get();
    load_page_cache_.previewStateIndex = picker.previewStateIndex.get();
    load_page_cache_.descriptor = picker.descriptor;
    for (uint8_t i = 0; i < PickerState::ENTRY_CAPACITY; ++i) {
        std::strncpy(
            load_page_cache_.entryIds[i].data(),
            picker.entryId(i),
            PickerState::ID_SIZE - 1U
        );
        load_page_cache_.entryIds[i][PickerState::ID_SIZE - 1U] = '\0';
        std::strncpy(
            load_page_cache_.entryNames[i].data(),
            picker.entryName(i),
            PickerState::NAME_SIZE - 1U
        );
        load_page_cache_.entryNames[i][PickerState::NAME_SIZE - 1U] = '\0';
        load_page_cache_.entryMetadataReadable[i] =
            picker.entryHasReadableMetadata(i);
    }
}

FLASHMEM void SequencerStepPresetPickerWorkflow::restoreLoadPage() {
    auto& picker = sequencer_.stepPresetPicker;
    if (!load_page_cache_.valid) {
        refreshFirstPage();
        return;
    }

    for (uint8_t i = 0; i < PickerState::ENTRY_CAPACITY; ++i) {
        picker.setEntry(
            i,
            load_page_cache_.entryIds[i].data(),
            load_page_cache_.entryNames[i].data(),
            load_page_cache_.entryMetadataReadable[i]
        );
    }
    picker.entryCount.set(load_page_cache_.entryCount);
    picker.hasPreviousPage.set(load_page_cache_.hasPrevious);
    picker.hasNextPage.set(load_page_cache_.hasNext);
    picker.truncated.set(
        load_page_cache_.hasPrevious || load_page_cache_.hasNext
    );
    picker.totalEntryCount.set(load_page_cache_.totalCount);
    picker.selectedIndex.set(load_page_cache_.selectedIndex);
    picker.previewStateIndex.set(load_page_cache_.previewStateIndex);
    picker.descriptor = load_page_cache_.descriptor;
    picker.feedback.set(core::state::sequencer::SequencerStepPresetFeedback::NONE);
    picker.operationFeedback.set({});
    picker.clampSelection();

    const bool previewStale = picker.descriptor.previewKey.projectRevision !=
        step_presets_.projectRevision() ||
        picker.descriptor.previewKey.targetHash !=
            core::state::sequencer::sequencerStepPresetTargetHash(
                picker.frozenTarget
            );
    inspectFocused(previewStale);
    picker.bump();
}

FLASHMEM void SequencerStepPresetPickerWorkflow::move(float delta) {
    auto& picker = sequencer_.stepPresetPicker;
    if (!picker.visible.get() || !nav::hasTurnDelta(delta)) return;

    if (picker.detailVisible.get()) {
        const int next = nav::nextWrappedIndex(
            delta,
            picker.detailFocus.get(),
            DETAIL_ROW_COUNT
        );
        picker.detailFocus.set(static_cast<uint8_t>(next));
        return;
    }

    const uint8_t count = picker.itemCount();
    if (count == 0) return;
    const bool forward = delta > 0.0f;
    const uint8_t current = picker.selectedIndex.get();
    if (forward && static_cast<uint8_t>(current + 1U) < count) {
        picker.selectedIndex.set(static_cast<uint8_t>(current + 1U));
        inspectFocused();
        return;
    }
    if (!forward && current > 0) {
        picker.selectedIndex.set(static_cast<uint8_t>(current - 1U));
        inspectFocused();
        return;
    }

    if (forward && picker.hasNextPage.get() && picker.entryCount.get() > 0) {
        (void)refreshPage(
            picker.entryId(static_cast<uint8_t>(picker.entryCount.get() - 1U)),
            core::persistence::StepPresetFilePageDirection::FORWARD,
            false
        );
    } else if (!forward && picker.hasPreviousPage.get() &&
               picker.entryCount.get() > 0) {
        (void)refreshPage(
            picker.entryId(0),
            core::persistence::StepPresetFilePageDirection::BACKWARD,
            true
        );
    }
}

FLASHMEM void SequencerStepPresetPickerWorkflow::toggleDetail() {
    auto& picker = sequencer_.stepPresetPicker;
    if (!picker.visible.get() || !focusedExistingAsset()) return;
    picker.detailVisible.set(!picker.detailVisible.get());
    picker.detailFocus.set(0);
    picker.bump();
}

FLASHMEM void SequencerStepPresetPickerWorkflow::movePreviewState(float delta) {
    auto& picker = sequencer_.stepPresetPicker;
    if (!picker.visible.get() || !picker.detailVisible.get() ||
        picker.detailFocus.get() != PREVIEW_DETAIL_ROW ||
        !nav::hasTurnDelta(delta)) {
        return;
    }
    const uint8_t count = std::max<uint8_t>(1, picker.descriptor.previewStateCount);
    const int next = nav::nextWrappedIndex(
        delta,
        picker.previewStateIndex.get(),
        count
    );
    picker.previewStateIndex.set(static_cast<uint8_t>(next));
    inspectFocused(true);
}

FLASHMEM void SequencerStepPresetPickerWorkflow::toggleMode() {
    auto& picker = sequencer_.stepPresetPicker;
    using Mode = core::state::sequencer::SequencerStepPresetPickerMode;
    picker.actionGuard.set({});
    picker.operationFeedback.set({});
    picker.detailVisible.set(false);
    picker.detailFocus.set(0);

    if (picker.mode.get() == Mode::LOAD) {
        cacheLoadPage();
        picker.mode.set(Mode::SAVE);
        refreshFirstPage();
    } else {
        picker.mode.set(Mode::LOAD);
        restoreLoadPage();
    }
    picker.bump();
}

FLASHMEM bool SequencerStepPresetPickerWorkflow::focusedExistingAsset() const {
    const auto& picker = sequencer_.stepPresetPicker;
    return picker.entryCount.get() > 0 && !picker.selectedItemIsNewAsset() &&
           picker.existingEntryIndexForSelectedItem() < picker.entryCount.get();
}

FLASHMEM const char* SequencerStepPresetPickerWorkflow::selectedPresetId() const {
    const auto& picker = sequencer_.stepPresetPicker;
    if (!focusedExistingAsset()) return "";
    return picker.entryId(picker.existingEntryIndexForSelectedItem());
}

FLASHMEM void SequencerStepPresetPickerWorkflow::inspectFocused(bool force) {
    auto& picker = sequencer_.stepPresetPicker;
    if (!focusedExistingAsset()) {
        picker.descriptor = {};
        picker.inspecting.set(false);
        picker.bump();
        return;
    }

    picker.frozenTarget.projectRevision = step_presets_.projectRevision();
    char presetId[PickerState::ID_SIZE]{};
    std::strncpy(presetId, selectedPresetId(), sizeof(presetId) - 1U);
    const core::state::sequencer::SequencerStepPresetPreviewKey wanted{
        .assetHash = core::state::sequencer::sequencerStepPresetIdHash(presetId),
        .targetHash = core::state::sequencer::sequencerStepPresetTargetHash(
            picker.frozenTarget
        ),
        .projectRevision = picker.frozenTarget.projectRevision,
        .stateIndex = picker.previewStateIndex.get(),
    };
    if (!force && picker.descriptor.valid &&
        std::strcmp(picker.descriptor.technicalId, presetId) == 0 &&
        picker.descriptor.previewKey.targetHash == wanted.targetHash &&
        picker.descriptor.previewKey.projectRevision == wanted.projectRevision &&
        picker.descriptor.previewKey.stateIndex == wanted.stateIndex) {
        return;
    }

    uint32_t generation = picker.previewGeneration.get() + 1U;
    if (generation == 0) generation = 1;
    picker.previewGeneration.set(generation);
    picker.inspecting.set(true);
    picker.bump();

    const auto inspected = step_presets_.inspectPreset(
        presetId,
        picker.frozenTarget,
        picker.previewStateIndex.get(),
        generation
    );

    // Synchronous today, generation-safe for a future deferred inspector.
    if (picker.previewGeneration.get() != generation ||
        std::strcmp(selectedPresetId(), presetId) != 0 ||
        !core::state::sequencer::sequencerStepPresetInspectionMatches(
            generation,
            wanted,
            inspected.descriptor
        )) {
        return;
    }

    picker.descriptor = inspected.descriptor;
    picker.inspecting.set(false);
    setInspectionFeedback(inspected);
    picker.bump();
}

FLASHMEM bool SequencerStepPresetPickerWorkflow::shouldCommitBeforeLoad() const {
    const auto& picker = sequencer_.stepPresetPicker;
    return picker.visible.get() &&
           picker.mode.get() ==
               core::state::sequencer::SequencerStepPresetPickerMode::LOAD &&
           focusedExistingAsset();
}

FLASHMEM contextual::ContextActionSpec
SequencerStepPresetPickerWorkflow::actionSpec() const {
    const auto& picker = sequencer_.stepPresetPicker;
    return core::state::sequencer::buildSequencerStepPresetActionSpec(
        picker.mode.get() ==
            core::state::sequencer::SequencerStepPresetPickerMode::SAVE,
        picker.selectedItemIsNewAsset(),
        focusedExistingAsset(),
        picker.frozenTarget,
        picker.descriptor
    );
}

FLASHMEM bool SequencerStepPresetPickerWorkflow::actionGuardEngaged() const {
    const auto phase = sequencer_.stepPresetPicker.actionGuard.get().phase;
    return phase == contextual::GuardedActionPhase::PRESSED ||
           phase == contextual::GuardedActionPhase::ARMED ||
           phase == contextual::GuardedActionPhase::COMMITTED;
}

FLASHMEM bool SequencerStepPresetPickerWorkflow::beginActionGuard(uint32_t nowMs) {
    const auto spec = actionSpec();
    if (!contextual::canExecute(spec.hold) || !contextual::requiresGuard(spec)) {
        return false;
    }
    auto guard = sequencer_.stepPresetPicker.actionGuard.get();
    if (contextual::guardedActionTerminal(guard)) {
        contextual::resetGuardedAction(guard);
    }
    if (!contextual::beginGuardedActionPress(
            guard,
            nowMs,
            spec.guard.durationMs
        )) {
        return false;
    }
    sequencer_.stepPresetPicker.operationActivationGeneration = 0;
    sequencer_.stepPresetPicker.actionGuard.set(guard);
    publishOperationFeedback(
        contextual::OperationFeedbackStatus::PRESSED,
        spec.hold.reason,
        contextual::OperationFeedbackExpiryPolicy::MANUAL,
        nowMs
    );
    return true;
}

FLASHMEM SequencerStepPresetPickerOutcome
SequencerStepPresetPickerWorkflow::update(uint32_t nowMs) {
    auto& picker = sequencer_.stepPresetPicker;
    SequencerStepPresetPickerOutcome resolvedOutcome =
        SequencerStepPresetPickerOutcome::NONE;
    auto feedback = picker.operationFeedback.get();
    if (contextual::updateOperationFeedback(feedback, nowMs)) {
        picker.operationFeedback.set(feedback);
    }

    if (picker.feedback.get() ==
            core::state::sequencer::SequencerStepPresetFeedback::QUEUED &&
        picker.operationActivationGeneration != 0) {
        using ActivationStatus =
            core::state::sequencer::SequencerTrackActivationStatus;
        const auto activation = step_presets_.activationStatus(
            picker.frozenTarget.trackIndex,
            picker.operationActivationGeneration
        );
        if (activation == ActivationStatus::APPLIED) {
            picker.feedback.set(
                core::state::sequencer::SequencerStepPresetFeedback::APPLIED
            );
            publishOperationFeedback(
                contextual::OperationFeedbackStatus::APPLIED,
                contextual::ContextActionReason::NONE,
                contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                nowMs,
                Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS
            );
            resolvedOutcome = SequencerStepPresetPickerOutcome::APPLIED;
        } else if (activation == ActivationStatus::CANCELLED ||
                   activation == ActivationStatus::IDLE) {
            picker.feedback.set(
                core::state::sequencer::SequencerStepPresetFeedback::CANCELLED
            );
            publishOperationFeedback(
                contextual::OperationFeedbackStatus::CANCELLED,
                contextual::ContextActionReason::STALE_TARGET,
                contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                nowMs,
                Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS
            );
            resolvedOutcome = SequencerStepPresetPickerOutcome::CANCELLED;
        }
    }

    auto guard = picker.actionGuard.get();
    if (guard.phase == contextual::GuardedActionPhase::CANCELLED) {
        if (!picker.operationFeedback.get().active) {
            contextual::resetGuardedAction(guard);
            picker.actionGuard.set(guard);
        }
        return resolvedOutcome;
    }
    if (guard.phase == contextual::GuardedActionPhase::PRESSED &&
        (nowMs - guard.pressedAtMs) >= Config::Timing::LATCH_THRESHOLD_MS) {
        const uint32_t pressedAt = guard.pressedAtMs;
        contextual::armGuardedAction(guard, pressedAt);
        picker.actionGuard.set(guard);
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::ARMED,
            actionSpec().hold.reason,
            contextual::OperationFeedbackExpiryPolicy::MANUAL,
            nowMs
        );
    }
    if (guard.phase == contextual::GuardedActionPhase::ARMED &&
        contextual::updateGuardedAction(guard, nowMs)) {
        picker.actionGuard.set(guard);
    }
    return resolvedOutcome;
}

FLASHMEM bool SequencerStepPresetPickerWorkflow::cancelActionGuard(uint32_t nowMs) {
    auto& picker = sequencer_.stepPresetPicker;
    auto guard = picker.actionGuard.get();
    const bool cancelled = contextual::cancelGuardedAction(guard);
    if (!cancelled && guard.phase == contextual::GuardedActionPhase::COMMITTED) {
        guard.phase = contextual::GuardedActionPhase::CANCELLED;
    } else if (!cancelled) {
        return false;
    }
    picker.actionGuard.set(guard);
    publishOperationFeedback(
        contextual::OperationFeedbackStatus::CANCELLED,
        contextual::ContextActionReason::NONE,
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS
    );
    picker.feedback.set(core::state::sequencer::SequencerStepPresetFeedback::CANCELLED);
    return true;
}

FLASHMEM SequencerStepPresetPickerOutcome
SequencerStepPresetPickerWorkflow::executeTap(uint32_t nowMs) {
    const auto spec = actionSpec();
    if (!contextual::canExecute(spec.tap)) {
        return SequencerStepPresetPickerOutcome::BLOCKED;
    }
    return executeCurrentAction(false, nowMs);
}

FLASHMEM SequencerStepPresetPickerOutcome
SequencerStepPresetPickerWorkflow::commitActionGuard(uint32_t nowMs) {
    auto& picker = sequencer_.stepPresetPicker;
    auto guard = picker.actionGuard.get();
    // This entry point is dispatched exclusively by ButtonAPI's completed
    // LONG_PRESS binding. Treat that event as the authority for the physical
    // deadline instead of requiring a second clock sample to reach the same
    // millisecond. Host/recorder scheduling can otherwise deliver LONG_PRESS
    // while the workflow clock is a few milliseconds behind, leaving the
    // guard ARMED and turning a completed hold into a cancellation on release.
    if (guard.phase == contextual::GuardedActionPhase::PRESSED) {
        const uint32_t pressedAt = guard.pressedAtMs;
        contextual::armGuardedAction(guard, pressedAt);
    }
    if (guard.phase == contextual::GuardedActionPhase::ARMED) {
        contextual::updateGuardedAction(
            guard,
            guard.armedAtMs + guard.guardDurationMs
        );
    }
    if (guard.phase != contextual::GuardedActionPhase::COMMITTED) {
        return SequencerStepPresetPickerOutcome::BLOCKED;
    }

    const auto previousFeedback = picker.operationFeedback.get();
    const auto spec = actionSpec();
    const bool sameAction = previousFeedback.active &&
        previousFeedback.action == spec.hold.action &&
        previousFeedback.source == spec.source &&
        previousFeedback.target == spec.target &&
        contextual::canExecute(spec.hold);
    if (!sameAction) {
        contextual::resetGuardedAction(guard);
        picker.actionGuard.set(guard);
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::CANCELLED,
            contextual::ContextActionReason::STALE_TARGET,
            contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
            nowMs
        );
        return SequencerStepPresetPickerOutcome::BLOCKED;
    }

    picker.actionGuard.set(guard);
    const auto outcome = executeCurrentAction(true, nowMs);
    contextual::resetGuardedAction(guard);
    picker.actionGuard.set(guard);
    return outcome;
}

FLASHMEM SequencerStepPresetPickerOutcome
SequencerStepPresetPickerWorkflow::executeCurrentAction(
    bool overwriteAuthorized,
    uint32_t nowMs
) {
    auto& picker = sequencer_.stepPresetPicker;
    using Mode = core::state::sequencer::SequencerStepPresetPickerMode;
    picker.operationActivationGeneration = 0;

    if (picker.mode.get() == Mode::SAVE) {
        char presetId[PickerState::ID_SIZE] = {};
        if (picker.selectedItemIsNewAsset()) {
            const auto next = step_presets_.nextPresetId(presetId, sizeof(presetId));
            if (!next.ok()) {
                setFeedback(next);
                publishOperationFeedback(
                    contextual::OperationFeedbackStatus::FAILED,
                    reasonForResult(next),
                    contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
                    nowMs
                );
                return SequencerStepPresetPickerOutcome::BLOCKED;
            }
        } else {
            if (!overwriteAuthorized || !focusedExistingAsset()) {
                return SequencerStepPresetPickerOutcome::BLOCKED;
            }
            std::strncpy(presetId, selectedPresetId(), sizeof(presetId) - 1U);
        }

        const auto result = step_presets_.savePreset(
            presetId,
            picker.frozenTarget,
            overwriteAuthorized
        );
        if (!result.ok()) {
            setFeedback(result);
            publishOperationFeedback(
                contextual::OperationFeedbackStatus::FAILED,
                reasonForResult(result),
                contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
                nowMs
            );
            return SequencerStepPresetPickerOutcome::BLOCKED;
        }
        // Keep the picker visible long enough for explicit, temporary success
        // feedback and expose the newly saved asset immediately in Load mode.
        // This avoids making Save feel like a silent modal dismissal.
        picker.mode.set(Mode::LOAD);
        (void)refreshPageContainingAndSelect(result.presetId);
        picker.feedback.set(core::state::sequencer::SequencerStepPresetFeedback::SAVED);
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::APPLIED,
            contextual::ContextActionReason::NONE,
            contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
            nowMs,
            Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS,
            contextual::ContextActionId::SAVE
        );
        return SequencerStepPresetPickerOutcome::SAVED;
    }

    if (!focusedExistingAsset()) {
        picker.setFeedback(core::state::sequencer::SequencerStepPresetFeedback::EMPTY);
        return SequencerStepPresetPickerOutcome::LOAD_EMPTY;
    }

    // A preceding live edit may have been committed as its own history action.
    // Refresh compatibility and generation before the atomic preset action.
    inspectFocused(true);
    const auto currentSpec = actionSpec();
    const bool executable = overwriteAuthorized
        ? contextual::canExecute(currentSpec.hold)
        : contextual::canExecute(currentSpec.tap);
    if (!executable) {
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::BLOCKED,
            overwriteAuthorized ? currentSpec.hold.reason : currentSpec.tap.reason,
            contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
            nowMs
        );
        return SequencerStepPresetPickerOutcome::BLOCKED;
    }

    const auto result = step_presets_.applyPreset(
        selectedPresetId(),
        picker.frozenTarget,
        picker.descriptor.previewKey
    );
    if (!result.ok()) {
        setFeedback(result);
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::FAILED,
            reasonForResult(result),
            contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
            nowMs
        );
        return SequencerStepPresetPickerOutcome::LOAD_FAILED;
    }

    if (result.activation == SequencerStepPresetActivation::QUEUED) {
        picker.operationActivationGeneration = result.activationGeneration;
        picker.feedback.set(core::state::sequencer::SequencerStepPresetFeedback::QUEUED);
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::QUEUED,
            contextual::ContextActionReason::PENDING,
            contextual::OperationFeedbackExpiryPolicy::WHEN_RESOLVED,
            nowMs
        );
        return SequencerStepPresetPickerOutcome::QUEUED;
    }

    picker.operationActivationGeneration = result.activationGeneration;
    picker.feedback.set(core::state::sequencer::SequencerStepPresetFeedback::APPLIED);
    publishOperationFeedback(
        contextual::OperationFeedbackStatus::APPLIED,
        currentSpec.tap.reason != contextual::ContextActionReason::NONE
            ? currentSpec.tap.reason
            : currentSpec.hold.reason,
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS
    );
    return SequencerStepPresetPickerOutcome::APPLIED;
}

FLASHMEM void SequencerStepPresetPickerWorkflow::setFeedback(
    const SequencerStepPresetActionResult& result
) {
    using Feedback = core::state::sequencer::SequencerStepPresetFeedback;
    if (result.status == SequencerStepPresetStatus::EMPTY) {
        sequencer_.stepPresetPicker.setFeedback(Feedback::EMPTY);
    } else if (result.status == SequencerStepPresetStatus::INCOMPATIBLE ||
               result.status == SequencerStepPresetStatus::CAPACITY ||
               result.status == SequencerStepPresetStatus::CORRUPT ||
               result.status == SequencerStepPresetStatus::UNSUPPORTED_VERSION ||
               result.status == SequencerStepPresetStatus::STALE_TARGET) {
        sequencer_.stepPresetPicker.setFeedback(Feedback::INCOMPATIBLE);
    } else if (!result.ok()) {
        sequencer_.stepPresetPicker.setFeedback(Feedback::FAILED);
    } else {
        sequencer_.stepPresetPicker.setFeedback(Feedback::NONE);
    }
}

FLASHMEM void SequencerStepPresetPickerWorkflow::setInspectionFeedback(
    const SequencerStepPresetInspectResult& result
) {
    using Feedback = core::state::sequencer::SequencerStepPresetFeedback;
    const auto compatibility = result.descriptor.compatibility;
    if (core::state::sequencer::sequencerStepPresetCanApply(compatibility)) {
        sequencer_.stepPresetPicker.feedback.set(Feedback::NONE);
    } else if (compatibility == core::state::sequencer::
                   SequencerStepPresetCompatibility::STORAGE_UNAVAILABLE) {
        sequencer_.stepPresetPicker.feedback.set(Feedback::FAILED);
    } else {
        sequencer_.stepPresetPicker.feedback.set(Feedback::INCOMPATIBLE);
    }
}

FLASHMEM void SequencerStepPresetPickerWorkflow::publishOperationFeedback(
    contextual::OperationFeedbackStatus status,
    contextual::ContextActionReason reason,
    contextual::OperationFeedbackExpiryPolicy expiry,
    uint32_t nowMs,
    uint32_t durationMs,
    contextual::ContextActionId completedAction
) {
    auto& picker = sequencer_.stepPresetPicker;
    const auto spec = actionSpec();
    const auto variant = activeVariant(spec);
    auto feedback = picker.operationFeedback.get();
    contextual::setOperationFeedback(
        feedback,
        completedAction != contextual::ContextActionId::NONE
            ? completedAction
            : (variant.action != contextual::ContextActionId::NONE
                   ? variant.action
                   : contextual::ContextActionId::LOAD),
        spec.source,
        spec.target,
        status,
        reason,
        expiry,
        nowMs,
        durationMs
    );
    picker.operationFeedback.set(feedback);
}

}  // namespace core::handler
