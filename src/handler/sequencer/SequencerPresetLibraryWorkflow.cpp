#include "handler/sequencer/SequencerPresetLibraryWorkflow.hpp"

#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/time/Time.hpp>

#include "config/Timing.hpp"
#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "state/interaction/TextKeyboardLayout.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"
#include "state/sequencer/SequencerPresetLibraryActionSpec.hpp"

namespace core::handler {
namespace {

namespace contextual = core::state::contextual;

FLASHMEM contextual::ContextActionVariant activeVariant(
    const contextual::ContextActionSpec& spec
) {
    return contextual::hasHoldAction(spec) ? spec.hold : spec.tap;
}

FLASHMEM core::state::sequencer::SequencerPresetLibraryFeedback
terminalFeedback(SequencerPresetLibraryOutcome outcome) {
    using Feedback =
        core::state::sequencer::SequencerPresetLibraryFeedback;
    switch (outcome) {
        case SequencerPresetLibraryOutcome::SAVED:
            return Feedback::SAVED;
        case SequencerPresetLibraryOutcome::LOADED:
            return Feedback::LOADED;
        case SequencerPresetLibraryOutcome::QUEUED:
        case SequencerPresetLibraryOutcome::RETRY_PENDING:
            return Feedback::QUEUED;
        case SequencerPresetLibraryOutcome::CANCELLED:
            return Feedback::CANCELLED;
        case SequencerPresetLibraryOutcome::LOAD_EMPTY:
            return Feedback::EMPTY;
        case SequencerPresetLibraryOutcome::LOAD_FAILED:
        case SequencerPresetLibraryOutcome::BLOCKED:
            return Feedback::FAILED;
        case SequencerPresetLibraryOutcome::NONE:
        default:
            return Feedback::NONE;
    }
}

FLASHMEM void managedCatalogId(
    const core::state::sequencer::SequencerPatternPresetLibraryState& pattern,
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0U) return;
    if (pattern.managedEntryKind == core::state::sequencer::
            SequencerPresetLibraryEntryKind::FOLDER) {
        std::snprintf(
            out,
            outSize,
            "@%s",
            pattern.managedEntryId.data()
        );
    } else {
        std::snprintf(
            out,
            outSize,
            "%s",
            pattern.managedEntryId.data()
        );
    }
}

}  // namespace

FLASHMEM SequencerPresetLibraryWorkflow::SequencerPresetLibraryWorkflow(
    core::state::sequencer::SequencerState& sequencer,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
) : sequencer_(sequencer),
    overlays_(overlays),
    pager_(sequencer_.presetLibrary, nullptr, nullptr) {}

FLASHMEM bool SequencerPresetLibraryWorkflow::open(
    const SequencerPresetLibraryAdapter& adapter
) {
    OC_PERF_SCOPE(perfOpen, "ui.preset-library.open");
    OC_PERF_UNITS(perfOpen, static_cast<uint32_t>(adapter.kind), 0U);
    if (!adapter.valid()) return false;
    inspection_pending_ = false;
    inspection_due_at_ms_ = 0U;
    pending_page_purpose_ = PendingPagePurpose::NONE;
    action_retry_pending_ = false;
    action_retry_overwrite_authorized_ = false;

    auto& picker = sequencer_.presetLibrary;
    picker.open(
        core::state::sequencer::SequencerPresetLibraryMode::LOAD,
        adapter.kind
    );
    adapter_ = adapter;
    pager_.configure(adapter_.context, adapter_.loadPage);
    if (!adapter_.beginSession(adapter_.context)) {
        adapter_ = {};
        picker.reset();
        pager_.configure(nullptr, nullptr);
        return false;
    }

    (void)refreshPage(
        nullptr,
        SequencerPresetLibraryPager::PageDirection::FORWARD,
        false
    );
    overlays_.show(core::ui::OverlayType::PRESET_LIBRARY, true);
    return true;
}

FLASHMEM void SequencerPresetLibraryWorkflow::close() {
    inspection_pending_ = false;
    inspection_due_at_ms_ = 0U;
    pending_page_purpose_ = PendingPagePurpose::NONE;
    action_retry_pending_ = false;
    action_retry_overwrite_authorized_ = false;
    modal::hideIfCurrent(
        overlays_,
        core::ui::OverlayType::PRESET_LIBRARY
    );
    sequencer_.presetLibrary.reset();
    adapter_ = {};
    pager_.configure(nullptr, nullptr);
}

FLASHMEM bool SequencerPresetLibraryWorkflow::back(uint32_t nowMs) {
    auto& picker = sequencer_.presetLibrary;
    if (!active()) return false;
    if (pager_.pending() || action_retry_pending_) {
        // Catalog work is independent of the modal. Abandon the unchanged UI
        // continuation immediately; the admitted background scan may finish
        // and be reused by a later open without trapping the user in playback.
        close();
        return true;
    }
    if (operationPending()) return false;
    if (textEditing()) {
        cancelTextEditing();
        return false;
    }
    if (actionGuardEngaged()) {
        (void)cancelActionGuard(nowMs);
        return false;
    }
    if (picker.libraryKind.get() ==
        core::state::sequencer::SequencerPresetLibraryKind::PATTERN) {
        auto& pattern = picker.pattern();
        if (pattern.panel == core::state::sequencer::
                SequencerPatternPresetLibraryPanel::MOVE_DESTINATION) {
            if (adapter_.leaveFolder != nullptr &&
                adapter_.leaveFolder(adapter_.context)) {
                (void)refreshPage(
                    nullptr,
                    SequencerPresetLibraryPager::PageDirection::FORWARD,
                    false
                );
            } else {
                restoreManagedLocation();
                pattern.panel = core::state::sequencer::
                    SequencerPatternPresetLibraryPanel::MANAGE;
                picker.operationFeedback.set({});
                picker.bump();
            }
            return false;
        }
        if (pattern.panel == core::state::sequencer::
                SequencerPatternPresetLibraryPanel::MANAGE) {
            restoreManagedLocation();
            pattern.panel = core::state::sequencer::
                SequencerPatternPresetLibraryPanel::BROWSE;
            char entryId[core::state::sequencer::
                SequencerPresetLibrarySessionState::ID_SIZE]{};
            managedCatalogId(pattern, entryId, sizeof(entryId));
            (void)refreshPageContainingAndSelect(entryId);
            return false;
        }
        if (pattern.factoryCopyPending && pattern.location.root()) {
            cancelFactoryCopy();
            return false;
        }
    }
    if (picker.detailVisible.get()) {
        picker.detailVisible.set(false);
        picker.detailFocus.set(0);
        picker.bump();
        return false;
    }
    if (adapter_.leaveFolder != nullptr &&
        adapter_.leaveFolder(adapter_.context)) {
        (void)refreshPage(
            nullptr,
            SequencerPresetLibraryPager::PageDirection::FORWARD,
            false
        );
        return false;
    }
    close();
    return true;
}

FLASHMEM void SequencerPresetLibraryWorkflow::move(
    float delta,
    uint32_t nowMs
) {
    OC_PERF_SCOPE(perfNav, "ui.preset-library.nav");
    auto& picker = sequencer_.presetLibrary;
    if (!active() || !nav::hasTurnDelta(delta) ||
        operationPending() || actionGuardEngaged()) {
        return;
    }
    if (textEditing()) {
        auto& pattern = picker.pattern();
        pattern.textKeyIndex =
            core::state::interaction::textKeyboardMoveColumn(
                pattern.textKeyIndex,
                delta > 0.0f ? 1 : -1
            );
        picker.bump();
        return;
    }

    if (picker.libraryKind.get() ==
            core::state::sequencer::SequencerPresetLibraryKind::PATTERN &&
        picker.pattern().panel == core::state::sequencer::
            SequencerPatternPresetLibraryPanel::MANAGE) {
        const int count = static_cast<int>(
            core::state::sequencer::
                SequencerPatternPresetManagementAction::COUNT
        );
        const int current = static_cast<int>(
            picker.pattern().managementAction
        );
        picker.pattern().managementAction = static_cast<
            core::state::sequencer::SequencerPatternPresetManagementAction>(
                nav::nextWrappedIndex(delta, current, count)
            );
        picker.actionGuard.set({});
        picker.operationFeedback.set({});
        picker.bump();
        return;
    }
    OC_PERF_UNITS(
        perfNav,
        static_cast<uint32_t>(picker.libraryKind.get()),
        picker.totalEntryCount.get()
    );

    if (picker.detailVisible.get()) {
        const uint8_t count = adapter_.detailRowCount != nullptr
            ? adapter_.detailRowCount(adapter_.context)
            : 0U;
        if (count < 2U) return;
        picker.detailFocus.set(static_cast<uint8_t>(
            nav::nextWrappedIndex(
                delta,
                picker.detailFocus.get(),
                count
            )
        ));
        return;
    }

    if (pager_.move(delta)) {
        scheduleFocusedInspection(nowMs);
    } else if (pager_.pending()) {
        inspection_pending_ = false;
        inspection_due_at_ms_ = 0U;
        adapter_.clearInspection(adapter_.context);
        picker.inspecting.set(false);
        pending_page_purpose_ = PendingPagePurpose::BROWSE;
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::QUEUED,
            contextual::ContextActionReason::PENDING,
            contextual::OperationFeedbackExpiryPolicy::WHEN_RESOLVED,
            nowMs
        );
    }
}

FLASHMEM void SequencerPresetLibraryWorkflow::enterDetail() {
    auto& picker = sequencer_.presetLibrary;
    if (textEditing()) {
        auto& pattern = picker.pattern();
        if (core::state::interaction::textKeyboardAppend(
                pattern.textDraft.data(),
                pattern.textDraft.size(),
                core::state::interaction::textKeyboardCharacterAt(
                    pattern.textKeyIndex,
                    pattern.textShiftActive
                )
            )) {
            picker.bump();
        }
        return;
    }
    if (!active() || operationPending() ||
        picker.detailVisible.get()) {
        return;
    }
    if (picker.libraryKind.get() ==
            core::state::sequencer::SequencerPresetLibraryKind::PATTERN &&
        picker.pattern().panel == core::state::sequencer::
            SequencerPatternPresetLibraryPanel::MANAGE) {
        return;
    }
    if (pager_.focusedFolder()) {
        if (adapter_.enterFolder != nullptr &&
            adapter_.enterFolder(
                adapter_.context,
                pager_.selectedEntryId()
            )) {
            (void)refreshPage(
                nullptr,
                SequencerPresetLibraryPager::PageDirection::FORWARD,
                false
            );
        }
        return;
    }
    if (!pager_.focusedExistingAsset()) return;
    completePendingInspection();
    bool descriptorValid = false;
    if (picker.libraryKind.get() ==
        core::state::sequencer::SequencerPresetLibraryKind::CHORD) {
        descriptorValid = picker.chord().descriptor.valid;
    } else if (picker.libraryKind.get() ==
               core::state::sequencer::
                   SequencerPresetLibraryKind::PATTERN) {
        descriptorValid = picker.pattern().descriptor.valid;
    } else {
        descriptorValid = picker.step().descriptor.valid;
    }
    if (!descriptorValid) {
        return;
    }
    picker.detailVisible.set(true);
    picker.detailFocus.set(0);
    picker.bump();
}

FLASHMEM bool
SequencerPresetLibraryWorkflow::openFocusedManagement() {
    auto& picker = sequencer_.presetLibrary;
    if (!active() || operationPending() || actionGuardEngaged() ||
        picker.libraryKind.get() !=
            core::state::sequencer::SequencerPresetLibraryKind::PATTERN ||
        picker.pattern().panel != core::state::sequencer::
            SequencerPatternPresetLibraryPanel::BROWSE ||
        !picker.selectedItemIsExistingAsset() ||
        adapter_.beginManagement == nullptr) {
        return false;
    }

    completePendingInspection();
    const uint8_t index = picker.existingEntryIndexForSelectedItem();
    if (!adapter_.beginManagement(
            adapter_.context,
            picker.entryKind(index),
            picker.entryId(index),
            picker.entryName(index)
        )) {
        return false;
    }
    auto& pattern = picker.pattern();
    pattern.panel = core::state::sequencer::
        SequencerPatternPresetLibraryPanel::MANAGE;
    pattern.managementAction = core::state::sequencer::
        SequencerPatternPresetManagementAction::RENAME;
    picker.detailVisible.set(false);
    picker.actionGuard.set({});
    picker.operationFeedback.set({});
    picker.bump();
    return true;
}

FLASHMEM void SequencerPresetLibraryWorkflow::adjustFocusedDetail(
    float delta
) {
    auto& picker = sequencer_.presetLibrary;
    if (textEditing() && nav::hasTurnDelta(delta)) {
        auto& pattern = picker.pattern();
        pattern.textKeyIndex =
            core::state::interaction::textKeyboardMoveRow(
                pattern.textKeyIndex,
                delta > 0.0f ? 1 : -1
            );
        picker.bump();
        return;
    }
    if (!active() || operationPending() ||
        !picker.detailVisible.get() ||
        adapter_.adjustFocusedDetail == nullptr ||
        !nav::hasTurnDelta(delta)) {
        return;
    }
    adapter_.adjustFocusedDetail(
        adapter_.context,
        pager_.selectedAssetId(),
        delta
    );
}

FLASHMEM void SequencerPresetLibraryWorkflow::toggleMode() {
    if (textEditing()) {
        auto& picker = sequencer_.presetLibrary;
        if (core::state::interaction::textKeyboardBackspace(
                picker.pattern().textDraft.data()
            )) {
            picker.bump();
        }
        return;
    }
    if (!active() || operationPending() || actionGuardEngaged()) return;
    inspection_pending_ = false;
    inspection_due_at_ms_ = 0U;
    auto& picker = sequencer_.presetLibrary;
    if (picker.libraryKind.get() ==
        core::state::sequencer::SequencerPresetLibraryKind::PATTERN) {
        auto& pattern = picker.pattern();
        if (pattern.factoryCopyPending) {
            cancelFactoryCopy();
            return;
        }
        if (picker.detailVisible.get() && pattern.descriptor.valid &&
            pattern.descriptor.source == core::state::sequencer::
                SequencerPatternPresetSource::FACTORY) {
            beginFactoryCopy();
            return;
        }
        if (pattern.panel == core::state::sequencer::
                SequencerPatternPresetLibraryPanel::MOVE_DESTINATION) {
            restoreManagedLocation();
            pattern.panel = core::state::sequencer::
                SequencerPatternPresetLibraryPanel::MANAGE;
            picker.bump();
            return;
        }
        if (pattern.panel == core::state::sequencer::
                SequencerPatternPresetLibraryPanel::MANAGE) {
            pattern.panel = core::state::sequencer::
                SequencerPatternPresetLibraryPanel::BROWSE;
            (void)refreshPage(
                nullptr,
                SequencerPresetLibraryPager::PageDirection::FORWARD,
                false
            );
            return;
        }
        if ((picker.detailVisible.get() || pager_.focusedFolder()) &&
            openFocusedManagement()) {
            return;
        }
    }
    picker.inspecting.set(false);
    adapter_.clearInspection(adapter_.context);
    pager_.toggleModePreservingSelection();
    if (picker.libraryKind.get() ==
        core::state::sequencer::SequencerPresetLibraryKind::PATTERN) {
        picker.pattern().sourceFilter = picker.mode.get() ==
                core::state::sequencer::SequencerPresetLibraryMode::SAVE
            ? core::state::sequencer::
                  SequencerPatternPresetSourceFilter::USER
            : core::state::sequencer::
                  SequencerPatternPresetSourceFilter::ALL;
        (void)refreshPage(
            nullptr,
            SequencerPresetLibraryPager::PageDirection::FORWARD,
            false
        );
        return;
    }
    if (picker.mode.get() ==
        core::state::sequencer::SequencerPresetLibraryMode::LOAD) {
        inspectFocused(true);
    }
}

FLASHMEM void SequencerPresetLibraryWorkflow::beginFactoryCopy() {
    auto& picker = sequencer_.presetLibrary;
    if (!active() || picker.libraryKind.get() !=
            core::state::sequencer::SequencerPresetLibraryKind::PATTERN) {
        return;
    }
    auto& pattern = picker.pattern();
    if (!picker.detailVisible.get() || !pattern.descriptor.valid ||
        pattern.descriptor.source != core::state::sequencer::
            SequencerPatternPresetSource::FACTORY) {
        return;
    }

    pattern.copySourceLocation = pattern.location;
    std::strncpy(
        pattern.copySourceId.data(),
        pattern.descriptor.metadata.technicalId,
        pattern.copySourceId.size() - 1U
    );
    std::strncpy(
        pattern.copySourceName.data(),
        pattern.descriptor.metadata.semanticName,
        pattern.copySourceName.size() - 1U
    );
    pattern.factoryCopyPending = true;
    pattern.location.reset();
    pattern.sourceFilter = core::state::sequencer::
        SequencerPatternPresetSourceFilter::USER;
    pattern.descriptor = {};
    picker.mode.set(
        core::state::sequencer::SequencerPresetLibraryMode::SAVE
    );
    picker.detailVisible.set(false);
    picker.detailFocus.set(0U);
    picker.actionGuard.set({});
    picker.operationFeedback.set({});
    picker.feedback.set(
        core::state::sequencer::SequencerPresetLibraryFeedback::NONE
    );
    (void)refreshPage(
        nullptr,
        SequencerPresetLibraryPager::PageDirection::FORWARD,
        false
    );
}

FLASHMEM void SequencerPresetLibraryWorkflow::cancelFactoryCopy() {
    auto& picker = sequencer_.presetLibrary;
    if (!active() || picker.libraryKind.get() !=
            core::state::sequencer::SequencerPresetLibraryKind::PATTERN ||
        !picker.pattern().factoryCopyPending) {
        return;
    }
    auto& pattern = picker.pattern();
    const auto sourceLocation = pattern.copySourceLocation;
    char sourceId[core::state::sequencer::
        SequencerPresetLibrarySessionState::ID_SIZE]{};
    std::strncpy(sourceId, pattern.copySourceId.data(), sizeof(sourceId) - 1U);

    pattern.factoryCopyPending = false;
    pattern.copySourceId.fill('\0');
    pattern.copySourceName.fill('\0');
    pattern.copySourceLocation.reset();
    pattern.location = sourceLocation;
    pattern.sourceFilter = core::state::sequencer::
        SequencerPatternPresetSourceFilter::FACTORY;
    picker.mode.set(
        core::state::sequencer::SequencerPresetLibraryMode::LOAD
    );
    picker.actionGuard.set({});
    picker.operationFeedback.set({});
    picker.feedback.set(
        core::state::sequencer::SequencerPresetLibraryFeedback::NONE
    );
    const auto status = refreshPageContainingAndSelect(sourceId);
    if (status == SequencerPresetLibraryPager::PageLoadStatus::READY &&
        pattern.descriptor.valid) {
        picker.detailVisible.set(true);
        picker.bump();
    }
}

FLASHMEM void
SequencerPresetLibraryWorkflow::cyclePatternSourceFilter() {
    using Filter = core::state::sequencer::
        SequencerPatternPresetSourceFilter;
    auto& picker = sequencer_.presetLibrary;
    if (textEditing()) {
        picker.pattern().textShiftActive = false;
        picker.bump();
        return;
    }
    if (!active() || operationPending() || actionGuardEngaged() ||
        picker.detailVisible.get() ||
        picker.libraryKind.get() !=
            core::state::sequencer::SequencerPresetLibraryKind::PATTERN ||
        picker.pattern().panel != core::state::sequencer::
            SequencerPatternPresetLibraryPanel::BROWSE ||
        picker.mode.get() !=
            core::state::sequencer::SequencerPresetLibraryMode::LOAD) {
        return;
    }
    auto& filter = picker.pattern().sourceFilter;
    filter = filter == Filter::ALL
        ? Filter::FACTORY
        : (filter == Filter::FACTORY ? Filter::USER : Filter::ALL);
    picker.pattern().location.reset();
    (void)refreshPage(
        nullptr,
        SequencerPresetLibraryPager::PageDirection::FORWARD,
        false
    );
}

FLASHMEM void SequencerPresetLibraryWorkflow::setTextShift(bool active) {
    if (!textEditing()) return;
    auto& picker = sequencer_.presetLibrary;
    picker.pattern().textShiftActive = active;
    picker.bump();
}

FLASHMEM bool SequencerPresetLibraryWorkflow::active() const {
    return adapter_.valid() &&
           sequencer_.presetLibrary.visible.get() &&
           sequencer_.presetLibrary.libraryKind.get() == adapter_.kind;
}

FLASHMEM bool
SequencerPresetLibraryWorkflow::shouldCommitBeforeLoad(
    bool guardedAction
) const {
    if (!active() || adapter_.shouldCommitBeforeLoad == nullptr) {
        return false;
    }
    if (sequencer_.presetLibrary.mode.get() !=
        core::state::sequencer::
            SequencerPresetLibraryMode::LOAD) {
        return false;
    }
    const auto spec = actionSpec();
    const auto& variant = guardedAction ? spec.hold : spec.tap;
    if (!contextual::canExecute(variant)) return false;
    return adapter_.shouldCommitBeforeLoad(
        adapter_.context,
        pager_.focusedExistingAsset()
    );
}

FLASHMEM bool SequencerPresetLibraryWorkflow::operationPending() const {
    if (!active()) return false;
    const auto& picker = sequencer_.presetLibrary;
    return pager_.pending() || action_retry_pending_ ||
           picker.feedback.get() ==
               core::state::sequencer::
                   SequencerPresetLibraryFeedback::QUEUED ||
           (picker.operationFeedback.get().active &&
            picker.operationFeedback.get().status ==
                contextual::OperationFeedbackStatus::QUEUED);
}

FLASHMEM contextual::ContextActionSpec
SequencerPresetLibraryWorkflow::actionSpec() const {
    if (!active()) return {};
    return core::state::sequencer::
        buildSequencerPresetLibraryActionSpec(sequencer_.presetLibrary);
}

FLASHMEM bool
SequencerPresetLibraryWorkflow::actionGuardEngaged() const {
    const auto phase =
        sequencer_.presetLibrary.actionGuard.get().phase;
    return phase == contextual::GuardedActionPhase::PRESSED ||
           phase == contextual::GuardedActionPhase::ARMED ||
           phase == contextual::GuardedActionPhase::COMMITTED;
}

FLASHMEM bool SequencerPresetLibraryWorkflow::textEditing() const {
    const auto& picker = sequencer_.presetLibrary;
    return active() &&
        picker.libraryKind.get() ==
            core::state::sequencer::SequencerPresetLibraryKind::PATTERN &&
        picker.pattern().textEdit !=
            core::state::sequencer::SequencerPatternPresetTextEdit::NONE;
}

FLASHMEM bool SequencerPresetLibraryWorkflow::beginActionGuard(
    uint32_t nowMs
) {
    if (!active() || operationPending() || textEditing() ||
        sequencer_.presetLibrary.selectedItemIsNewFolder()) {
        return false;
    }
    completePendingInspection();
    const auto spec = actionSpec();
    if (!contextual::canExecute(spec.hold) ||
        !contextual::requiresGuard(spec)) {
        return false;
    }

    auto& picker = sequencer_.presetLibrary;
    auto guard = picker.actionGuard.get();
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
    picker.actionGuard.set(guard);
    publishOperationFeedback(
        contextual::OperationFeedbackStatus::PRESSED,
        spec.hold.reason,
        contextual::OperationFeedbackExpiryPolicy::MANUAL,
        nowMs
    );
    return true;
}

FLASHMEM SequencerPresetLibraryResult
SequencerPresetLibraryWorkflow::update(uint32_t nowMs) {
    SequencerPresetLibraryResult result{};
    if (!active()) return result;

    auto& picker = sequencer_.presetLibrary;
    if (pager_.pending()) {
        const auto purpose = pending_page_purpose_;
        const auto pageStatus = pager_.retryPending();
        if (pageStatus ==
            SequencerPresetLibraryPager::PageLoadStatus::PENDING) {
            return result;
        }
        pending_page_purpose_ = PendingPagePurpose::NONE;
        if (pageStatus ==
            SequencerPresetLibraryPager::PageLoadStatus::FAILED) {
            publishOperationFeedback(
                contextual::OperationFeedbackStatus::FAILED,
                contextual::ContextActionReason::STORAGE_UNAVAILABLE,
                contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
                nowMs
            );
            return result;
        }
        if (picker.itemCount() > 0U) inspectFocused(true);
        if (purpose == PendingPagePurpose::POST_SAVE) {
            picker.feedback.set(
                core::state::sequencer::
                    SequencerPresetLibraryFeedback::SAVED
            );
            publishOperationFeedback(
                contextual::OperationFeedbackStatus::APPLIED,
                contextual::ContextActionReason::NONE,
                contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                nowMs,
                Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS,
                contextual::ContextActionId::SAVE
            );
        }
    }

    if (inspection_pending_ &&
        oc::time::deadlineReachedMs(
            nowMs,
            inspection_due_at_ms_
        )) {
        completePendingInspection();
    }
    auto feedback = picker.operationFeedback.get();
    if (contextual::updateOperationFeedback(feedback, nowMs)) {
        picker.operationFeedback.set(feedback);
    }

    if (action_retry_pending_) {
        const bool overwriteAuthorized =
            action_retry_overwrite_authorized_;
        action_retry_pending_ = false;
        action_retry_overwrite_authorized_ = false;
        return executeCurrentAction(overwriteAuthorized, nowMs);
    }

    if (adapter_.update != nullptr) {
        result = adapter_.update(adapter_.context, nowMs);
        if (result.outcome != SequencerPresetLibraryOutcome::NONE) {
            publishTerminalResult(result, nowMs);
        }
    }

    auto guard = picker.actionGuard.get();
    if (guard.phase == contextual::GuardedActionPhase::CANCELLED) {
        if (!picker.operationFeedback.get().active) {
            contextual::resetGuardedAction(guard);
            picker.actionGuard.set(guard);
        }
        return result;
    }
    if (guard.phase == contextual::GuardedActionPhase::PRESSED &&
        (nowMs - guard.pressedAtMs) >=
            Config::Timing::LATCH_THRESHOLD_MS) {
        contextual::armGuardedAction(guard, guard.pressedAtMs);
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
    return result;
}

FLASHMEM bool SequencerPresetLibraryWorkflow::cancelActionGuard(
    uint32_t nowMs
) {
    auto& picker = sequencer_.presetLibrary;
    auto guard = picker.actionGuard.get();
    const bool cancelled = contextual::cancelGuardedAction(guard);
    if (!cancelled &&
        guard.phase == contextual::GuardedActionPhase::COMMITTED) {
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
    picker.feedback.set(
        core::state::sequencer::SequencerPresetLibraryFeedback::CANCELLED
    );
    return true;
}

FLASHMEM SequencerPresetLibraryResult
SequencerPresetLibraryWorkflow::executeTap(uint32_t nowMs) {
    if (textEditing()) return confirmTextEditing(nowMs);
    if (sequencer_.presetLibrary.selectedItemIsNewFolder()) {
        beginTextEditing(
            core::state::sequencer::
                SequencerPatternPresetTextEdit::CREATE_FOLDER
        );
        return {};
    }
    const bool managingPatternEntry =
        sequencer_.presetLibrary.libraryKind.get() ==
            core::state::sequencer::SequencerPresetLibraryKind::PATTERN &&
        sequencer_.presetLibrary.pattern().panel ==
            core::state::sequencer::
                SequencerPatternPresetLibraryPanel::MANAGE;
    if (pager_.focusedFolder() && !managingPatternEntry) {
        enterDetail();
        return {};
    }
    if (operationPending()) {
        return blockedResult(contextual::ContextActionReason::PENDING);
    }
    completePendingInspection();
    const auto spec = actionSpec();
    if (!contextual::canExecute(spec.tap)) {
        const auto result = blockedResult(spec.tap.reason);
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::BLOCKED,
            result.reason,
            contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
            nowMs
        );
        return result;
    }
    return executeCurrentAction(false, nowMs);
}

FLASHMEM void
SequencerPresetLibraryWorkflow::beginTextEditing(
    core::state::sequencer::SequencerPatternPresetTextEdit purpose
) {
    auto& picker = sequencer_.presetLibrary;
    if (!active() ||
        picker.libraryKind.get() !=
            core::state::sequencer::SequencerPresetLibraryKind::PATTERN) {
        return;
    }
    auto& pattern = picker.pattern();
    if ((purpose == core::state::sequencer::
             SequencerPatternPresetTextEdit::CREATE_FOLDER &&
         (picker.mode.get() != core::state::sequencer::
              SequencerPresetLibraryMode::SAVE ||
          pattern.panel != core::state::sequencer::
              SequencerPatternPresetLibraryPanel::BROWSE)) ||
        (purpose == core::state::sequencer::
             SequencerPatternPresetTextEdit::RENAME &&
         pattern.panel != core::state::sequencer::
             SequencerPatternPresetLibraryPanel::MANAGE)) {
        return;
    }
    pattern.textEdit = purpose;
    pattern.textShiftActive = false;
    pattern.textKeyIndex =
        core::state::interaction::TEXT_KEYBOARD_DEFAULT_INDEX;
    pattern.textDraft.fill('\0');
    if (purpose == core::state::sequencer::
            SequencerPatternPresetTextEdit::RENAME) {
        std::strncpy(
            pattern.textDraft.data(),
            pattern.managedEntryName.data(),
            pattern.textDraft.size() - 1U
        );
    }
    picker.actionGuard.set({});
    picker.operationFeedback.set({});
    picker.bump();
}

FLASHMEM void
SequencerPresetLibraryWorkflow::cancelTextEditing() {
    auto& picker = sequencer_.presetLibrary;
    if (picker.libraryKind.get() !=
        core::state::sequencer::SequencerPresetLibraryKind::PATTERN) {
        return;
    }
    auto& pattern = picker.pattern();
    pattern.textEdit = core::state::sequencer::
        SequencerPatternPresetTextEdit::NONE;
    pattern.textShiftActive = false;
    pattern.textDraft.fill('\0');
    picker.operationFeedback.set({});
    picker.bump();
}

FLASHMEM SequencerPresetLibraryResult
SequencerPresetLibraryWorkflow::confirmTextEditing(uint32_t nowMs) {
    auto& picker = sequencer_.presetLibrary;
    if (!textEditing()) {
        return blockedResult(contextual::ContextActionReason::NO_ACTION);
    }

    auto& pattern = picker.pattern();
    const auto purpose = pattern.textEdit;
    const auto& draft = pattern.textDraft;
    const bool renamingAsset = purpose == core::state::sequencer::
            SequencerPatternPresetTextEdit::RENAME &&
        pattern.managedEntryKind == core::state::sequencer::
            SequencerPresetLibraryEntryKind::ASSET;
    const bool validName = renamingAsset
        ? core::state::sequencer::validSequencerPresetSemanticName(
              draft.data()
          )
        : core::state::sequencer::sequencerPatternPresetFolderNameIsValid(
              draft.data()
          );
    if (!validName) {
        const auto result =
            blockedResult(contextual::ContextActionReason::INVALID_PAYLOAD);
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::BLOCKED,
            result.reason,
            contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
            nowMs
        );
        return result;
    }

    char editedName[
        core::state::sequencer::SEQUENCER_PRESET_SEMANTIC_NAME_SIZE
    ]{};
    std::strncpy(editedName, draft.data(), sizeof(editedName) - 1U);
    const auto result = purpose == core::state::sequencer::
            SequencerPatternPresetTextEdit::RENAME
        ? (adapter_.renameManaged != nullptr
            ? adapter_.renameManaged(adapter_.context, editedName)
            : blockedResult(contextual::ContextActionReason::NO_ACTION))
        : (adapter_.createFolder != nullptr
            ? adapter_.createFolder(adapter_.context, editedName)
            : blockedResult(contextual::ContextActionReason::NO_ACTION));
    if (result.outcome != SequencerPresetLibraryOutcome::SAVED) {
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::BLOCKED,
            result.reason,
            contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
            nowMs
        );
        return result;
    }

    cancelTextEditing();
    if (purpose == core::state::sequencer::
            SequencerPatternPresetTextEdit::RENAME) {
        std::strncpy(
            pattern.managedEntryName.data(),
            editedName,
            pattern.managedEntryName.size() - 1U
        );
        if (pattern.managedEntryKind == core::state::sequencer::
                SequencerPresetLibraryEntryKind::FOLDER) {
            std::strncpy(
                pattern.managedEntryId.data(),
                editedName,
                pattern.managedEntryId.size() - 1U
            );
        }
        restoreManagedLocation();
        pattern.panel = core::state::sequencer::
            SequencerPatternPresetLibraryPanel::BROWSE;
        pattern.sourceFilter = core::state::sequencer::
            SequencerPatternPresetSourceFilter::USER;
        char entryId[core::state::sequencer::
            SequencerPresetLibrarySessionState::ID_SIZE]{};
        managedCatalogId(pattern, entryId, sizeof(entryId));
        (void)refreshPageContainingAndSelect(entryId);
        picker.feedback.set(
            core::state::sequencer::SequencerPresetLibraryFeedback::SAVED
        );
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::APPLIED,
            contextual::ContextActionReason::NONE,
            contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
            nowMs,
            Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS,
            contextual::ContextActionId::RENAME
        );
        return result;
    }

    char entryId[
        core::state::sequencer::
            SequencerPatternPresetLocation::MAX_FOLDER_NAME_SIZE + 2U
    ]{};
    std::snprintf(entryId, sizeof(entryId), "@%s", editedName);
    if (adapter_.enterFolder == nullptr ||
        !adapter_.enterFolder(adapter_.context, entryId)) {
        return blockedResult(contextual::ContextActionReason::FAILED);
    }
    (void)refreshPage(
        nullptr,
        SequencerPresetLibraryPager::PageDirection::FORWARD,
        false
    );
    picker.feedback.set(
        core::state::sequencer::SequencerPresetLibraryFeedback::SAVED
    );
    publishOperationFeedback(
        contextual::OperationFeedbackStatus::APPLIED,
        contextual::ContextActionReason::NONE,
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS,
        contextual::ContextActionId::CREATE
    );
    return result;
}

FLASHMEM SequencerPresetLibraryResult
SequencerPresetLibraryWorkflow::executeManagementAction(uint32_t nowMs) {
    auto& picker = sequencer_.presetLibrary;
    auto& pattern = picker.pattern();
    switch (pattern.managementAction) {
        case core::state::sequencer::
                SequencerPatternPresetManagementAction::RENAME:
            beginTextEditing(
                core::state::sequencer::
                    SequencerPatternPresetTextEdit::RENAME
            );
            return {};
        case core::state::sequencer::
                SequencerPatternPresetManagementAction::MOVE:
            pattern.location.reset();
            pattern.sourceFilter = core::state::sequencer::
                SequencerPatternPresetSourceFilter::USER;
            pattern.panel = core::state::sequencer::
                SequencerPatternPresetLibraryPanel::MOVE_DESTINATION;
            (void)refreshPage(
                nullptr,
                SequencerPresetLibraryPager::PageDirection::FORWARD,
                false
            );
            return {};
        case core::state::sequencer::
                SequencerPatternPresetManagementAction::DELETE: {
            if (adapter_.deleteManaged == nullptr) {
                return blockedResult(
                    contextual::ContextActionReason::NO_ACTION
                );
            }
            const auto result = adapter_.deleteManaged(adapter_.context);
            if (result.outcome != SequencerPresetLibraryOutcome::SAVED) {
                publishOperationFeedback(
                    contextual::OperationFeedbackStatus::BLOCKED,
                    result.reason,
                    contextual::OperationFeedbackExpiryPolicy::
                        ON_ACKNOWLEDGEMENT,
                    nowMs
                );
                return result;
            }
            restoreManagedLocation();
            pattern.panel = core::state::sequencer::
                SequencerPatternPresetLibraryPanel::BROWSE;
            pattern.sourceFilter = core::state::sequencer::
                SequencerPatternPresetSourceFilter::USER;
            (void)refreshPage(
                nullptr,
                SequencerPresetLibraryPager::PageDirection::FORWARD,
                false
            );
            publishOperationFeedback(
                contextual::OperationFeedbackStatus::APPLIED,
                contextual::ContextActionReason::NONE,
                contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                nowMs,
                Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS,
                contextual::ContextActionId::DELETE_ASSET
            );
            return result;
        }
        case core::state::sequencer::
                SequencerPatternPresetManagementAction::COUNT:
        default:
            return blockedResult(contextual::ContextActionReason::NO_ACTION);
    }
}

FLASHMEM SequencerPresetLibraryResult
SequencerPresetLibraryWorkflow::executeMove(uint32_t nowMs) {
    auto& picker = sequencer_.presetLibrary;
    auto& pattern = picker.pattern();
    if (picker.selectedIndex.get() != 0U || adapter_.moveManaged == nullptr) {
        return blockedResult(contextual::ContextActionReason::NO_ACTION);
    }
    const auto result = adapter_.moveManaged(adapter_.context);
    if (result.outcome != SequencerPresetLibraryOutcome::SAVED) {
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::BLOCKED,
            result.reason,
            contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
            nowMs
        );
        return result;
    }
    pattern.panel = core::state::sequencer::
        SequencerPatternPresetLibraryPanel::BROWSE;
    pattern.sourceFilter = core::state::sequencer::
        SequencerPatternPresetSourceFilter::USER;
    char entryId[core::state::sequencer::
        SequencerPresetLibrarySessionState::ID_SIZE]{};
    managedCatalogId(pattern, entryId, sizeof(entryId));
    (void)refreshPageContainingAndSelect(entryId);
    publishOperationFeedback(
        contextual::OperationFeedbackStatus::APPLIED,
        contextual::ContextActionReason::NONE,
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS,
        contextual::ContextActionId::MOVE
    );
    return result;
}

FLASHMEM void SequencerPresetLibraryWorkflow::restoreManagedLocation() {
    auto& pattern = sequencer_.presetLibrary.pattern();
    pattern.location = pattern.managedLocation;
    pattern.sourceFilter = core::state::sequencer::
        SequencerPatternPresetSourceFilter::USER;
}

FLASHMEM SequencerPresetLibraryResult
SequencerPresetLibraryWorkflow::commitActionGuard(uint32_t nowMs) {
    if (operationPending()) {
        return blockedResult(contextual::ContextActionReason::PENDING);
    }
    completePendingInspection();
    auto& picker = sequencer_.presetLibrary;
    auto guard = picker.actionGuard.get();
    if (guard.phase == contextual::GuardedActionPhase::PRESSED) {
        contextual::armGuardedAction(guard, guard.pressedAtMs);
    }
    if (guard.phase == contextual::GuardedActionPhase::ARMED) {
        contextual::updateGuardedAction(
            guard,
            guard.armedAtMs + guard.guardDurationMs
        );
    }
    if (guard.phase != contextual::GuardedActionPhase::COMMITTED) {
        return blockedResult(contextual::ContextActionReason::NO_ACTION);
    }

    const auto previousFeedback = picker.operationFeedback.get();
    const auto spec = actionSpec();
    const bool sameAction =
        previousFeedback.active &&
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
        return blockedResult(
            contextual::ContextActionReason::STALE_TARGET
        );
    }

    picker.actionGuard.set(guard);
    const auto result = executeCurrentAction(true, nowMs);
    contextual::resetGuardedAction(guard);
    picker.actionGuard.set(guard);
    return result;
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPresetLibraryWorkflow::refreshPage(
    const char* anchorExclusive,
    SequencerPresetLibraryPager::PageDirection direction,
    bool selectLast
) {
    auto& picker = sequencer_.presetLibrary;
    inspection_pending_ = false;
    inspection_due_at_ms_ = 0U;
    adapter_.clearInspection(adapter_.context);
    picker.inspecting.set(false);
    const auto status = pager_.refreshPage(
        anchorExclusive,
        direction,
        selectLast
    );
    if (status ==
        SequencerPresetLibraryPager::PageLoadStatus::PENDING) {
        pending_page_purpose_ = PendingPagePurpose::BROWSE;
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::QUEUED,
            contextual::ContextActionReason::PENDING,
            contextual::OperationFeedbackExpiryPolicy::WHEN_RESOLVED,
            0U
        );
        return status;
    }
    pending_page_purpose_ = PendingPagePurpose::NONE;
    if (status ==
        SequencerPresetLibraryPager::PageLoadStatus::FAILED) {
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::FAILED,
            contextual::ContextActionReason::STORAGE_UNAVAILABLE,
            contextual::OperationFeedbackExpiryPolicy::ON_ACKNOWLEDGEMENT,
            0U
        );
        return status;
    }
    if (picker.itemCount() > 0U) inspectFocused(true);
    return status;
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPresetLibraryWorkflow::refreshPageContainingAndSelect(
    const char* assetId
) {
    inspection_pending_ = false;
    inspection_due_at_ms_ = 0U;
    adapter_.clearInspection(adapter_.context);
    const auto status = pager_.refreshPageContainingAndSelect(assetId);
    if (status ==
        SequencerPresetLibraryPager::PageLoadStatus::PENDING) {
        pending_page_purpose_ = PendingPagePurpose::POST_SAVE;
        return status;
    }
    pending_page_purpose_ = PendingPagePurpose::NONE;
    if (status ==
        SequencerPresetLibraryPager::PageLoadStatus::FAILED) {
        return status;
    }
    inspectFocused(true);
    return status;
}

FLASHMEM void
SequencerPresetLibraryWorkflow::scheduleFocusedInspection(
    uint32_t nowMs
) {
    auto& picker = sequencer_.presetLibrary;
    adapter_.clearInspection(adapter_.context);
    picker.actionGuard.set({});
    picker.operationFeedback.set({});
    picker.feedback.set(
        core::state::sequencer::SequencerPresetLibraryFeedback::NONE
    );
    inspection_pending_ = pager_.focusedExistingAsset();
    inspection_due_at_ms_ = inspection_pending_
        ? nowMs +
              Config::Timing::
                  PRESET_LIBRARY_INSPECTION_SETTLE_MS
        : 0U;
    picker.inspecting.set(inspection_pending_);
    picker.bump();
}

FLASHMEM void
SequencerPresetLibraryWorkflow::completePendingInspection() {
    if (!inspection_pending_) return;
    inspection_pending_ = false;
    inspection_due_at_ms_ = 0U;
    inspectFocused();
}

FLASHMEM void SequencerPresetLibraryWorkflow::inspectFocused(
    bool force
) {
    auto& picker = sequencer_.presetLibrary;
    if (!pager_.focusedExistingAsset()) {
        adapter_.clearInspection(adapter_.context);
        picker.inspecting.set(false);
        picker.bump();
        return;
    }
    picker.feedback.set(adapter_.inspect(
        adapter_.context,
        pager_.selectedAssetId(),
        force
    ));
}

FLASHMEM SequencerPresetLibraryResult
SequencerPresetLibraryWorkflow::executeCurrentAction(
    bool overwriteAuthorized,
    uint32_t nowMs
) {
    OC_PERF_SCOPE(perfAction, "ui.preset-library.action");
    auto& picker = sequencer_.presetLibrary;
    OC_PERF_UNITS(
        perfAction,
        static_cast<uint32_t>(picker.libraryKind.get()),
        static_cast<uint32_t>(picker.mode.get())
    );
    using Mode =
        core::state::sequencer::SequencerPresetLibraryMode;

    if (picker.libraryKind.get() ==
        core::state::sequencer::SequencerPresetLibraryKind::PATTERN) {
        const auto panel = picker.pattern().panel;
        if (panel == core::state::sequencer::
                SequencerPatternPresetLibraryPanel::MANAGE) {
            return executeManagementAction(nowMs);
        }
        if (panel == core::state::sequencer::
                SequencerPatternPresetLibraryPanel::MOVE_DESTINATION) {
            return executeMove(nowMs);
        }
    }

    if (picker.mode.get() == Mode::SAVE) {
        const bool createNew = picker.selectedItemIsNewAsset();
        if (!createNew &&
            (!overwriteAuthorized ||
             !pager_.focusedExistingAsset())) {
            return blockedResult(
                contextual::ContextActionReason::NO_ACTION
            );
        }
        const auto result = adapter_.execute(
            adapter_.context,
            Mode::SAVE,
            createNew ? "" : pager_.selectedAssetId(),
            createNew,
            overwriteAuthorized
        );
        if (result.outcome ==
            SequencerPresetLibraryOutcome::RETRY_PENDING) {
            action_retry_pending_ = true;
            action_retry_overwrite_authorized_ = overwriteAuthorized;
            publishTerminalResult(result, nowMs);
            return result;
        }
        if (result.outcome != SequencerPresetLibraryOutcome::SAVED) {
            picker.setFeedback(
                result.feedback !=
                        core::state::sequencer::
                            SequencerPresetLibraryFeedback::NONE
                    ? result.feedback
                    : core::state::sequencer::
                          SequencerPresetLibraryFeedback::FAILED
            );
            publishOperationFeedback(
                contextual::OperationFeedbackStatus::FAILED,
                result.reason,
                contextual::OperationFeedbackExpiryPolicy::
                    ON_ACKNOWLEDGEMENT,
                nowMs
            );
            return result;
        }

        picker.mode.set(Mode::LOAD);
        (void)refreshPageContainingAndSelect(result.assetId);
        picker.feedback.set(
            core::state::sequencer::
                SequencerPresetLibraryFeedback::SAVED
        );
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::APPLIED,
            contextual::ContextActionReason::NONE,
            contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
            nowMs,
            Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS,
            contextual::ContextActionId::SAVE
        );
        return result;
    }

    if (!pager_.focusedExistingAsset()) {
        auto result = blockedResult(
            contextual::ContextActionReason::EMPTY_SELECTION
        );
        result.outcome =
            SequencerPresetLibraryOutcome::LOAD_EMPTY;
        result.feedback =
            core::state::sequencer::
                SequencerPresetLibraryFeedback::EMPTY;
        picker.setFeedback(result.feedback);
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::BLOCKED,
            result.reason,
            contextual::OperationFeedbackExpiryPolicy::
                ON_ACKNOWLEDGEMENT,
            nowMs
        );
        return result;
    }

    // Refresh only when the adapter's target identity changed. The adapter
    // keeps this allocation- and I/O-free for an unchanged inspection, while
    // still catching a history commit that advanced the project revision
    // between press and release.
    inspectFocused();
    const auto spec = actionSpec();
    const auto variant = overwriteAuthorized ? spec.hold : spec.tap;
    if (!contextual::canExecute(variant)) {
        const auto result = blockedResult(variant.reason);
        publishOperationFeedback(
            contextual::OperationFeedbackStatus::BLOCKED,
            result.reason,
            contextual::OperationFeedbackExpiryPolicy::
                ON_ACKNOWLEDGEMENT,
            nowMs
        );
        return result;
    }

    const auto result = adapter_.execute(
        adapter_.context,
        Mode::LOAD,
        pager_.selectedAssetId(),
        false,
        overwriteAuthorized
    );
    if (result.outcome ==
        SequencerPresetLibraryOutcome::RETRY_PENDING) {
        action_retry_pending_ = true;
        action_retry_overwrite_authorized_ = overwriteAuthorized;
        publishTerminalResult(result, nowMs);
        return result;
    }
    if (result.outcome ==
            SequencerPresetLibraryOutcome::LOADED ||
        result.outcome ==
            SequencerPresetLibraryOutcome::QUEUED ||
        result.outcome ==
            SequencerPresetLibraryOutcome::CANCELLED) {
        publishTerminalResult(result, nowMs);
        return result;
    }

    picker.setFeedback(
        result.feedback !=
                core::state::sequencer::
                    SequencerPresetLibraryFeedback::NONE
            ? result.feedback
            : core::state::sequencer::
                  SequencerPresetLibraryFeedback::FAILED
    );
    publishOperationFeedback(
        contextual::OperationFeedbackStatus::FAILED,
        result.reason,
        contextual::OperationFeedbackExpiryPolicy::
            ON_ACKNOWLEDGEMENT,
        nowMs
    );
    return result;
}

FLASHMEM void
SequencerPresetLibraryWorkflow::publishTerminalResult(
    const SequencerPresetLibraryResult& result,
    uint32_t nowMs
) {
    auto& picker = sequencer_.presetLibrary;
    const auto feedback =
        result.feedback !=
                core::state::sequencer::
                    SequencerPresetLibraryFeedback::NONE
            ? result.feedback
            : terminalFeedback(result.outcome);
    picker.feedback.set(feedback);

    contextual::OperationFeedbackStatus status =
        contextual::OperationFeedbackStatus::APPLIED;
    contextual::OperationFeedbackExpiryPolicy expiry =
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION;
    uint32_t durationMs =
        Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS;
    if (result.outcome ==
            SequencerPresetLibraryOutcome::QUEUED ||
        result.outcome ==
            SequencerPresetLibraryOutcome::RETRY_PENDING) {
        status = contextual::OperationFeedbackStatus::QUEUED;
        expiry =
            contextual::OperationFeedbackExpiryPolicy::WHEN_RESOLVED;
        durationMs = 0U;
    } else if (
        result.outcome ==
        SequencerPresetLibraryOutcome::CANCELLED) {
        status = contextual::OperationFeedbackStatus::CANCELLED;
        durationMs =
            Config::Timing::CONTEXT_CANCELLED_FEEDBACK_MS;
    }

    const auto variant = activeVariant(actionSpec());
    publishOperationFeedback(
        status,
        result.reason != contextual::ContextActionReason::NONE
            ? result.reason
            : variant.reason,
        expiry,
        nowMs,
        durationMs
    );
}

FLASHMEM void
SequencerPresetLibraryWorkflow::publishOperationFeedback(
    contextual::OperationFeedbackStatus status,
    contextual::ContextActionReason reason,
    contextual::OperationFeedbackExpiryPolicy expiry,
    uint32_t nowMs,
    uint32_t durationMs,
    contextual::ContextActionId completedAction
) {
    auto& picker = sequencer_.presetLibrary;
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

FLASHMEM SequencerPresetLibraryResult
SequencerPresetLibraryWorkflow::blockedResult(
    contextual::ContextActionReason reason
) {
    SequencerPresetLibraryResult result{};
    result.outcome = SequencerPresetLibraryOutcome::BLOCKED;
    result.feedback =
        core::state::sequencer::SequencerPresetLibraryFeedback::FAILED;
    result.reason = reason;
    return result;
}

}  // namespace core::handler
