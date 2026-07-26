#include "handler/sequencer/SequencerCcLaneWorkflow.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "app/ExtmemAllocator.hpp"
#include "handler/sequencer/SequencerCcLaneLiveProjection.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"
#include "state/sequencer/SequencerCcLaneDraftLayout.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"

namespace core::handler {

namespace seq = core::state::sequencer;
namespace contextual = core::state::contextual;

namespace {

using ActionId = contextual::ContextActionId;
using ActionImpact = contextual::ContextActionImpact;
using Availability = contextual::ContextActionAvailability;
using Reason = contextual::ContextActionReason;
using Icon = contextual::ContextIconId;
using Tone = contextual::ContextTone;

contextual::ContextEntityRef laneEntity(uint8_t track, uint8_t lane) {
    return {
        .kind = contextual::ContextEntityKind::CC_LANE,
        .track = track,
        .item = lane,
    };
}

contextual::ContextActionVariant variant(
    ActionId action,
    ActionImpact impact,
    Availability availability,
    Reason reason,
    Icon icon,
    Tone tone
) {
    return {
        .action = action,
        .impact = impact,
        .availability = availability,
        .reason = reason,
        .visual = {.icon = icon, .tone = tone},
    };
}

uint8_t clampAdd(int value, int delta, uint8_t minimum, uint8_t maximum) {
    const int next = std::max<int>(minimum, std::min<int>(maximum, value + delta));
    return static_cast<uint8_t>(next);
}

}  // namespace

FLASHMEM SequencerCcLaneWorkflow::SequencerCcLaneWorkflow(
    StateRefs state,
    SequencerCcLaneDomainServices services
)
    : editor_(state.editor)
    , tracks_(state.tracks)
    , project_navigation_(state.projectNavigation)
    , history_(state.history)
    , status_bar_(state.statusBar)
    , midi_cc_coordinator_(state.midiCcCoordinator)
    , services_(services)
    , last_transport_playing_(state.statusBar.playing.get()) {}

FLASHMEM int SequencerCcLaneWorkflow::direction_(float delta) {
    return delta > 0.0f ? 1 : (delta < 0.0f ? -1 : 0);
}

FLASHMEM uint8_t SequencerCcLaneWorkflow::selectorItemCount() const {
    const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
    const uint8_t count = bank ? seq::sequencerCcLaneCount(*bank) : 0;
    return static_cast<uint8_t>(
        count + (count < seq::SequencerCcLaneBank::MAX_LANES ? 1U : 0U)
    );
}

FLASHMEM int8_t SequencerCcLaneWorkflow::selectorLane() const {
    const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
    if (bank == nullptr) return -1;
    uint8_t dense = 0;
    for (uint8_t lane = 0; lane < bank->lanes.size(); ++lane) {
        if (!bank->lanes[lane].occupied) continue;
        if (dense == editor_.ccLaneUi.selectorIndex) return static_cast<int8_t>(lane);
        ++dense;
    }
    return -1;
}

FLASHMEM bool SequencerCcLaneWorkflow::selectorFocusesAdd() const {
    return selectorLane() < 0 &&
           editor_.ccLaneUi.selectorIndex + 1U == selectorItemCount();
}

FLASHMEM void SequencerCcLaneWorkflow::openLaneSelector() {
    (void)commitEventEdit(0);
    auto& ui = editor_.ccLaneUi;
    ui.mode = seq::SequencerCcLaneUiMode::LANE_SELECTOR;
    ui.selectorIndex = 0;
    const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
    if (bank != nullptr && ui.focusedLane < bank->lanes.size() &&
        bank->lanes[ui.focusedLane].occupied) {
        uint8_t dense = 0;
        for (uint8_t lane = 0; lane < ui.focusedLane; ++lane) {
            if (bank->lanes[lane].occupied) ++dense;
        }
        ui.selectorIndex = dense;
    }
    refreshProjection();
}

FLASHMEM bool SequencerCcLaneWorkflow::openLane(uint8_t lane) {
    openGrid_(lane);
    return editor_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID;
}

FLASHMEM void SequencerCcLaneWorkflow::suspendGridForPropertySelector(
    uint32_t nowMs
) {
    if (editor_.ccLaneUi.mode != seq::SequencerCcLaneUiMode::LANE_GRID) return;
    (void)commitEventEdit(nowMs);
    editor_.ccLaneUi.mode = seq::SequencerCcLaneUiMode::CLOSED;
    refreshProjection();
}

FLASHMEM void SequencerCcLaneWorkflow::closeOneLevel(uint32_t nowMs) {
    (void)commitEventEdit(nowMs);
    auto& ui = editor_.ccLaneUi;
    ui.transitionAppliedFeedback = false;
    auto guard = ui.actionGuard.get();
    contextual::cancelGuardedAction(guard);
    contextual::resetGuardedAction(guard);
    ui.actionGuard.set(guard);
    switch (ui.mode) {
        case seq::SequencerCcLaneUiMode::LANE_SETTINGS:
            ui.mode = seq::SequencerCcLaneUiMode::LANE_GRID;
            break;
        case seq::SequencerCcLaneUiMode::TRANSITION_PICKER:
            ui.compactTransitionPicker = false;
            ui.mode = seq::SequencerCcLaneUiMode::LANE_GRID;
            break;
        case seq::SequencerCcLaneUiMode::LANE_GRID:
            ui.mode = seq::SequencerCcLaneUiMode::CLOSED;
            break;
        case seq::SequencerCcLaneUiMode::LANE_SELECTOR:
        case seq::SequencerCcLaneUiMode::CLOSED:
        default:
            ui.mode = seq::SequencerCcLaneUiMode::CLOSED;
            break;
    }
    refreshProjection();
}

FLASHMEM void SequencerCcLaneWorkflow::moveSelector(float delta) {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_SELECTOR) return;
    const int direction = direction_(delta);
    const uint8_t count = selectorItemCount();
    if (direction == 0 || count == 0) return;
    const int next = (static_cast<int>(ui.selectorIndex) + direction + count) % count;
    ui.selectorIndex = static_cast<uint8_t>(next);
    refreshProjection();
}

FLASHMEM bool SequencerCcLaneWorkflow::createDefaultLane(uint32_t nowMs) {
    (void)commitEventEdit(nowMs);
    auto& ui = editor_.ccLaneUi;
    const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
    const int8_t freeLane = bank ? seq::firstFreeSequencerCcLane(*bank) : 0;
    if (freeLane < 0) {
        block_(ActionId::CREATE, Reason::CAPACITY, nowMs);
        return false;
    }
    ui.focusedLane = static_cast<uint8_t>(freeLane);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller =
        core::state::project::sanitizeProjectCcLaneDefault(
            project_navigation_.ccLaneDefaultControllers[ui.focusedLane],
            ui.focusedLane
        );
    draft.destination.minimum = 0U;
    draft.destination.maximum = 127U;
    draft.destination.routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    draft.destination.pinnedPort = 0U;
    draft.destination.pinnedChannel = services_.trackRoute(
        tracks_.activeTrackIndex()
    ).channel;
    draft.initialValue = 64U;

    const auto preflight = services_.preflight(
        tracks_.activeTrackIndex(),
        ui.focusedLane,
        draft
    );
    if (preflight.laneConflict) {
        block_(ActionId::CREATE, Reason::CONFLICT, nowMs);
        return false;
    }

    LaneBankPtr staged;
    if (!stageCurrentBank_(staged, true)) {
        block_(ActionId::CREATE, Reason::ALLOCATION_UNAVAILABLE, nowMs);
        return false;
    }
    // Creation stays a single musical gesture. A Macro collision is retained
    // as explicit authored arbitration metadata instead of reopening a wizard.
    draft.acceptedMacroConflict = preflight.macroConflict;
    if (!seq::createSequencerCcLane(*staged, ui.focusedLane, draft).changed()) {
        block_(ActionId::CREATE, Reason::INCOMPATIBLE, nowMs);
        return false;
    }

    auto change = prepareChange_(
        seq::SequencerHistoryActionKind::CcLaneCreate,
        ui.focusedLane
    );
    if (!change || !installPreparedChange_(std::move(change), std::move(staged))) {
        block_(ActionId::CREATE, Reason::HISTORY_UNAVAILABLE, nowMs);
        return false;
    }

    ui.acceptedMacroConflict = draft.acceptedMacroConflict;
    openGrid_(ui.focusedLane);
    publishFeedback_(
        ActionId::CREATE,
        contextual::OperationFeedbackStatus::APPLIED,
        preflight.macroConflict
            ? Reason::CONFLICT
            : (preflight.routeValid ? Reason::NONE : Reason::NO_ROUTE),
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        900
    );
    return true;
}

FLASHMEM void SequencerCcLaneWorkflow::openGrid_(uint8_t lane) {
    auto& ui = editor_.ccLaneUi;
    const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
    if (bank == nullptr || lane >= bank->lanes.size() || !bank->lanes[lane].occupied) return;
    ui.focusedLane = lane;
    ui.compactTransitionPicker = false;
    ui.focusedStep = std::min<uint8_t>(
        editor_.focusedStep.get(),
        static_cast<uint8_t>(std::max<uint8_t>(1, editor_.pattern.length.get()) - 1U)
    );
    ui.mode = seq::SequencerCcLaneUiMode::LANE_GRID;
    refreshProjection();
}

FLASHMEM bool SequencerCcLaneWorkflow::activateSelector(uint32_t nowMs) {
    if (editor_.ccLaneUi.mode != seq::SequencerCcLaneUiMode::LANE_SELECTOR) return false;
    const int8_t lane = selectorLane();
    if (lane >= 0) {
        openGrid_(static_cast<uint8_t>(lane));
        return true;
    }
    if (!selectorFocusesAdd()) return false;
    return createDefaultLane(nowMs);
}

FLASHMEM void SequencerCcLaneWorkflow::loadSettingsDraft_() {
    auto& ui = editor_.ccLaneUi;
    const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
    if (bank == nullptr || ui.focusedLane >= bank->lanes.size()) return;
    const auto& lane = bank->lanes[ui.focusedLane];
    if (!lane.occupied) return;
    ui.draft.destination = lane.destination;
    ui.draft.initialValue = lane.initialValue;
    ui.draft.acceptedMacroConflict = lane.acceptedMacroConflict;
    ui.acceptedMacroConflict = lane.acceptedMacroConflict;
    ui.focusedField = seq::SequencerCcLaneDraftField::CONTROLLER;
    ui.advancedSettings = false;
    ui.draftDirty = false;
}

FLASHMEM bool SequencerCcLaneWorkflow::openSettings() {
    (void)commitEventEdit(0);
    auto& ui = editor_.ccLaneUi;
    if (ui.mode == seq::SequencerCcLaneUiMode::LANE_SELECTOR) {
        const int8_t lane = selectorLane();
        if (lane < 0) return false;
        ui.focusedLane = static_cast<uint8_t>(lane);
    } else if (ui.mode != seq::SequencerCcLaneUiMode::LANE_GRID) {
        return false;
    }
    loadSettingsDraft_();
    ui.mode = seq::SequencerCcLaneUiMode::LANE_SETTINGS;
    refreshProjection();
    return true;
}

FLASHMEM void SequencerCcLaneWorkflow::moveDraftField(float delta) {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_SETTINGS) return;
    const int direction = direction_(delta);
    if (direction == 0) return;
    const auto layout = seq::buildSequencerCcLaneDraftLayout(
        ui.draft.destination.routePolicy,
        ui.advancedSettings
    );
    if (layout.count == 0) return;
    const int current = layout.indexOf(ui.focusedField);
    const int count = layout.count;
    const int next = (current + direction + count) % count;
    ui.focusedField = layout.fieldAt(static_cast<uint8_t>(next));
    refreshProjection();
}

FLASHMEM bool SequencerCcLaneWorkflow::activateDraftField() {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_SETTINGS ||
        ui.focusedField != seq::SequencerCcLaneDraftField::ADVANCED) {
        return false;
    }
    ui.advancedSettings = !ui.advancedSettings;
    refreshProjection();
    return true;
}

FLASHMEM void SequencerCcLaneWorkflow::editDraft(float delta) {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_SETTINGS) return;
    const int direction = direction_(delta);
    if (direction == 0) return;
    auto& destination = ui.draft.destination;
    switch (ui.focusedField) {
        case seq::SequencerCcLaneDraftField::CONTROLLER:
            destination.controller = clampAdd(destination.controller, direction, 0, 127);
            break;
        case seq::SequencerCcLaneDraftField::ROUTE_POLICY:
            destination.routePolicy = destination.routePolicy ==
                    seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK
                ? seq::SequencerCcLaneRoutePolicy::PINNED
                : seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK;
            if (destination.routePolicy == seq::SequencerCcLaneRoutePolicy::PINNED) {
                destination.pinnedPort = 0;
                destination.pinnedChannel = services_.trackRoute(
                    tracks_.activeTrackIndex()
                ).channel;
            }
            break;
        case seq::SequencerCcLaneDraftField::PINNED_CHANNEL:
            if (destination.routePolicy == seq::SequencerCcLaneRoutePolicy::PINNED) {
                destination.pinnedChannel = clampAdd(
                    destination.pinnedChannel,
                    direction,
                    0,
                    15
                );
            }
            break;
        case seq::SequencerCcLaneDraftField::MINIMUM:
            destination.minimum = clampAdd(
                destination.minimum,
                direction,
                0,
                destination.maximum
            );
            ui.draft.initialValue = std::max(ui.draft.initialValue, destination.minimum);
            break;
        case seq::SequencerCcLaneDraftField::MAXIMUM:
            destination.maximum = clampAdd(
                destination.maximum,
                direction,
                destination.minimum,
                127
            );
            ui.draft.initialValue = std::min(ui.draft.initialValue, destination.maximum);
            break;
        case seq::SequencerCcLaneDraftField::INITIAL:
            ui.draft.initialValue = clampAdd(
                ui.draft.initialValue,
                direction,
                destination.minimum,
                destination.maximum
            );
            break;
        case seq::SequencerCcLaneDraftField::ADVANCED:
            return;
        case seq::SequencerCcLaneDraftField::COUNT:
            return;
    }
    ui.draftDirty = true;
    refreshProjection();
}

FLASHMEM void SequencerCcLaneWorkflow::moveFocusedStep(float delta, uint32_t nowMs) {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_GRID) return;
    ui.transitionAppliedFeedback = false;
    (void)commitEventEdit(nowMs);
    const int direction = direction_(delta);
    const uint8_t length = std::max<uint8_t>(1, editor_.pattern.length.get());
    if (direction == 0) return;
    ui.focusedStep = static_cast<uint8_t>(
        (static_cast<int>(ui.focusedStep) + direction + length) % length
    );
    editor_.focusedStep.set(ui.focusedStep);
    editor_.page.set(static_cast<uint8_t>(
        ui.focusedStep / seq::SequencerPatternState::STEPS_PER_PAGE
    ));
    refreshProjection();
}

FLASHMEM bool SequencerCcLaneWorkflow::focusStep(uint8_t step, uint32_t nowMs) {
    auto& ui = editor_.ccLaneUi;
    const uint8_t length = std::max<uint8_t>(1U, editor_.pattern.length.get());
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_GRID || step >= length) {
        return false;
    }
    if (ui.focusedStep != step) {
        (void)commitEventEdit(nowMs);
        ui.focusedStep = step;
    }
    editor_.focusedStep.set(step);
    editor_.page.set(static_cast<uint8_t>(
        step / seq::SequencerPatternState::STEPS_PER_PAGE
    ));
    refreshProjection();
    return true;
}

FLASHMEM bool SequencerCcLaneWorkflow::stageCurrentBank_(
    LaneBankPtr& out,
    bool materializeEmpty
) const {
    if (!seq::cloneSequencerCcLaneBank(
            out,
            seq::sequencerCcLaneView(editor_.pattern)
        )) {
        return false;
    }
    if (!out && materializeEmpty) {
        out = core::app::makeExtmemUnique<seq::SequencerCcLaneBank>();
    }
    return !materializeEmpty || static_cast<bool>(out);
}

FLASHMEM SequencerCcLaneWorkflow::PatternChangePtr
SequencerCcLaneWorkflow::prepareChange_(
    seq::SequencerHistoryActionKind kind,
    uint8_t lane,
    uint8_t step
) {
    auto change = core::app::makeExtmemUnique<seq::SequencerHistoryPatternChange>();
    if (!change) return {};
    change->trackIndex = tracks_.activeTrackIndex();
    change->storage = seq::SequencerHistoryPatternStorage::FullGraph;
    change->descriptor = {
        .kind = kind,
        .trackIndex = tracks_.activeTrackIndex(),
        .laneIndex = lane,
        .stepIndex = step,
    };
    if (!seq::captureHistorySnapshot(editor_, change->before) ||
        !seq::captureHistorySnapshot(editor_, change->after)) {
        return {};
    }
    return change;
}

FLASHMEM bool SequencerCcLaneWorkflow::captureAfterFromBank_(
    seq::SequencerHistoryPatternChange& change,
    const seq::SequencerCcLaneBank* bank
) {
    if (!seq::captureHistorySnapshotUsingReservedGraph(editor_, change.after) ||
        !seq::captureSequencerCcLaneBankUsingReservedStorage(
            bank,
            change.after.ccLanes
        )) {
        return false;
    }
    change.after.ccLanesCaptured = true;
    return true;
}

FLASHMEM bool SequencerCcLaneWorkflow::installPreparedChange_(
    PatternChangePtr change,
    LaneBankPtr bank
) {
    if (!change || !captureAfterFromBank_(*change, bank.get()) ||
        !history_.canRecordPattern(*change)) {
        return false;
    }
    seq::installSequencerCcLaneBank(editor_.pattern, std::move(bank));
    history_.recordPreparedPattern(std::move(change));
    return true;
}

FLASHMEM bool SequencerCcLaneWorkflow::applySettings_(
    bool macroConflictAuthorized,
    uint32_t nowMs
) {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_SETTINGS) return false;
    const auto preflight = services_.preflight(
        tracks_.activeTrackIndex(),
        ui.focusedLane,
        ui.draft
    );
    if (preflight.laneConflict) {
        block_(ActionId::APPLY, Reason::CONFLICT, nowMs);
        return false;
    }
    if (preflight.macroConflict && !macroConflictAuthorized) {
        block_(ActionId::APPLY, Reason::CONFLICT, nowMs);
        return false;
    }

    LaneBankPtr staged;
    if (!stageCurrentBank_(staged, true)) {
        block_(ActionId::APPLY, Reason::ALLOCATION_UNAVAILABLE, nowMs);
        return false;
    }
    auto draft = ui.draft;
    draft.acceptedMacroConflict = preflight.macroConflict && macroConflictAuthorized;
    const auto mutation =
        seq::updateSequencerCcLaneSettings(*staged, ui.focusedLane, draft);
    if (!mutation.changed()) {
        block_(ActionId::APPLY, Reason::INCOMPATIBLE, nowMs);
        return false;
    }

    auto change = prepareChange_(
        seq::SequencerHistoryActionKind::CcLaneSettings,
        ui.focusedLane
    );
    if (!change || !installPreparedChange_(std::move(change), std::move(staged))) {
        block_(ActionId::APPLY, Reason::HISTORY_UNAVAILABLE, nowMs);
        return false;
    }
    ui.acceptedMacroConflict = draft.acceptedMacroConflict;
    openGrid_(ui.focusedLane);
    publishFeedback_(
        ActionId::APPLY,
        contextual::OperationFeedbackStatus::APPLIED,
        preflight.routeValid ? Reason::NONE : Reason::NO_ROUTE,
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        900
    );
    return true;
}

FLASHMEM bool SequencerCcLaneWorkflow::editFocusedEvent(float delta, uint32_t nowMs) {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_GRID || direction_(delta) == 0) {
        return false;
    }
    const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
    if (bank == nullptr || ui.focusedLane >= bank->lanes.size() ||
        !bank->lanes[ui.focusedLane].occupied) {
        block_(ActionId::EDIT, Reason::INVALID_PAYLOAD, nowMs);
        return false;
    }
    const auto& lane = bank->lanes[ui.focusedLane];
    const bool hadEvent = lane.activeMask.test(ui.focusedStep);
    const uint8_t beforeValue = hadEvent ? lane.values[ui.focusedStep] : 0;
    const uint8_t nextValue = hadEvent
        ? clampAdd(
            beforeValue,
            direction_(delta),
            lane.destination.minimum,
            lane.destination.maximum
        )
        : seq::proposedSequencerCcLaneEventValue(
            lane,
            ui.focusedStep,
            editor_.pattern.length.get()
        );
    return setFocusedEventValue_(nextValue, nowMs);
}

FLASHMEM bool SequencerCcLaneWorkflow::setFocusedEventValue_(
    uint8_t nextValue,
    uint32_t nowMs
) {
    auto& ui = editor_.ccLaneUi;
    LaneBankPtr staged;
    if (!stageCurrentBank_(staged, false) || !staged ||
        ui.focusedLane >= staged->lanes.size() ||
        !staged->lanes[ui.focusedLane].occupied) {
        block_(ActionId::EDIT, Reason::INVALID_PAYLOAD, nowMs);
        return false;
    }
    auto& lane = staged->lanes[ui.focusedLane];
    const bool hadEvent = lane.activeMask.test(ui.focusedStep);
    const uint8_t beforeValue = hadEvent ? lane.values[ui.focusedStep] : 0;
    const auto mutation = seq::setSequencerCcLaneEvent(
        *staged,
        ui.focusedLane,
        ui.focusedStep,
        nextValue
    );
    if (!mutation.changed()) return false;

    if (!history_.beginCoalescedCcLaneEventEdit(
            ui.focusedLane,
            ui.focusedStep,
            hadEvent ? static_cast<int32_t>(beforeValue) : -1,
            static_cast<int32_t>(nextValue),
            staged.get(),
            nowMs
        )) {
        block_(ActionId::EDIT, Reason::HISTORY_UNAVAILABLE, nowMs);
        refreshProjection();
        return false;
    }
    seq::installSequencerCcLaneBank(editor_.pattern, std::move(staged));
    refreshProjection();
    return true;
}

FLASHMEM bool SequencerCcLaneWorkflow::focusVisibleStep_(
    uint8_t indexInWindow,
    uint32_t nowMs
) {
    if (indexInWindow >= seq::SequencerPatternState::STEPS_PER_PAGE ||
        editor_.ccLaneUi.mode != seq::SequencerCcLaneUiMode::LANE_GRID) {
        return false;
    }
    editor_.ccLaneUi.transitionAppliedFeedback = false;
    auto& ui = editor_.ccLaneUi;
    const uint8_t start = static_cast<uint8_t>(
        (ui.focusedStep / seq::SequencerPatternState::STEPS_PER_PAGE) *
        seq::SequencerPatternState::STEPS_PER_PAGE
    );
    const uint8_t step = static_cast<uint8_t>(start + indexInWindow);
    if (step >= std::max<uint8_t>(1U, editor_.pattern.length.get())) return false;
    return focusStep(step, nowMs);
}

FLASHMEM bool SequencerCcLaneWorkflow::editVisibleEvent(
    uint8_t indexInWindow,
    float normalized,
    uint32_t nowMs
) {
    if (!focusVisibleStep_(indexInWindow, nowMs) || !std::isfinite(normalized)) {
        return false;
    }
    const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
    const auto& ui = editor_.ccLaneUi;
    if (bank == nullptr || ui.focusedLane >= bank->lanes.size()) return false;
    const auto& lane = bank->lanes[ui.focusedLane];
    const float clamped = std::clamp(normalized, 0.0f, 1.0f);
    const uint8_t value = static_cast<uint8_t>(std::lround(
        lane.destination.minimum +
        clamped * static_cast<float>(
            lane.destination.maximum - lane.destination.minimum
        )
    ));
    return setFocusedEventValue_(value, nowMs);
}

FLASHMEM bool SequencerCcLaneWorkflow::toggleVisibleEvent(
    uint8_t indexInWindow,
    uint32_t nowMs
) {
    return focusVisibleStep_(indexInWindow, nowMs) && toggleFocusedEvent(nowMs);
}

FLASHMEM bool SequencerCcLaneWorkflow::openTransitionPicker(
    uint8_t indexInWindow,
    uint32_t nowMs
) {
    if (!focusVisibleStep_(indexInWindow, nowMs)) return false;
    return openTransitionPickerForFocused_(false, nowMs);
}

FLASHMEM bool SequencerCcLaneWorkflow::openFocusedTransitionPicker(
    uint32_t nowMs
) {
    return openTransitionPickerForFocused_(true, nowMs);
}

FLASHMEM bool SequencerCcLaneWorkflow::openTransitionPickerForFocused_(
    bool compact,
    uint32_t nowMs
) {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_GRID) return false;
    (void)commitEventEdit(nowMs);
    const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
    if (bank == nullptr || ui.focusedLane >= bank->lanes.size()) return false;
    const auto& lane = bank->lanes[ui.focusedLane];
    if (!lane.activeMask.test(ui.focusedStep)) return false;
    ui.transitionStep = ui.focusedStep;
    ui.selectedTransition = seq::sequencerCcLaneTransition(lane, ui.focusedStep);
    ui.compactTransitionPicker = compact;
    ui.mode = seq::SequencerCcLaneUiMode::TRANSITION_PICKER;
    refreshProjection();
    return true;
}

FLASHMEM void SequencerCcLaneWorkflow::moveTransition(float delta) {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::TRANSITION_PICKER) return;
    const int direction = direction_(delta);
    if (direction == 0) return;
    constexpr int COUNT = 5;
    const int current = static_cast<int>(ui.selectedTransition);
    ui.selectedTransition = static_cast<seq::SequencerCcLaneTransition>(
        (current + direction + COUNT) % COUNT
    );
    ui.bump();
}

FLASHMEM bool SequencerCcLaneWorkflow::selectTransitionNormalized(
    float normalized
) {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::TRANSITION_PICKER ||
        !std::isfinite(normalized)) {
        return false;
    }
    constexpr int COUNT = 5;
    const int selected = std::clamp(
        static_cast<int>(std::lround(std::clamp(normalized, 0.0f, 1.0f) *
                                     static_cast<float>(COUNT - 1))),
        0,
        COUNT - 1
    );
    const auto transition = static_cast<seq::SequencerCcLaneTransition>(selected);
    if (ui.selectedTransition == transition) return true;
    ui.selectedTransition = transition;
    ui.bump();
    return true;
}

FLASHMEM bool SequencerCcLaneWorkflow::applyTransition(uint32_t nowMs) {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::TRANSITION_PICKER) return false;
    LaneBankPtr staged;
    if (!stageCurrentBank_(staged, false) || !staged ||
        ui.focusedLane >= staged->lanes.size()) return false;
    const auto before = seq::sequencerCcLaneTransition(
        staged->lanes[ui.focusedLane],
        ui.transitionStep
    );
    const auto mutation = seq::setSequencerCcLaneTransition(
        *staged,
        ui.focusedLane,
        ui.transitionStep,
        ui.selectedTransition
    );
    if (mutation.status == seq::SequencerCcLaneMutationStatus::NO_CHANGE) {
        ui.mode = seq::SequencerCcLaneUiMode::LANE_GRID;
        ui.compactTransitionPicker = false;
        ui.transitionAppliedFeedback = true;
        publishFeedback_(ActionId::EDIT,
                         contextual::OperationFeedbackStatus::APPLIED,
                         Reason::NONE,
                         contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                         nowMs,
                         650);
        refreshProjection();
        return true;
    }
    if (!mutation.changed()) return false;
    auto change = prepareChange_(
        seq::SequencerHistoryActionKind::CcLaneTransitionEdit,
        ui.focusedLane,
        ui.transitionStep
    );
    if (!change) return false;
    change->descriptor.hasValue = true;
    change->descriptor.beforeValue = static_cast<int32_t>(before);
    change->descriptor.afterValue = static_cast<int32_t>(ui.selectedTransition);
    if (!installPreparedChange_(std::move(change), std::move(staged))) {
        block_(ActionId::EDIT, Reason::HISTORY_UNAVAILABLE, nowMs);
        return false;
    }
    ui.mode = seq::SequencerCcLaneUiMode::LANE_GRID;
    ui.compactTransitionPicker = false;
    ui.transitionAppliedFeedback = true;
    publishFeedback_(ActionId::EDIT,
                     contextual::OperationFeedbackStatus::APPLIED,
                     Reason::NONE,
                     contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                     nowMs,
                     650);
    refreshProjection();
    return true;
}

FLASHMEM void SequencerCcLaneWorkflow::cancelTransition() {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::TRANSITION_PICKER) return;
    ui.mode = seq::SequencerCcLaneUiMode::LANE_GRID;
    ui.compactTransitionPicker = false;
    refreshProjection();
}

FLASHMEM bool SequencerCcLaneWorkflow::toggleFocusedEvent(uint32_t nowMs) {
    auto& ui = editor_.ccLaneUi;
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_GRID) return false;
    ui.transitionAppliedFeedback = false;
    const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
    if (bank == nullptr || ui.focusedLane >= bank->lanes.size() ||
        !bank->lanes[ui.focusedLane].occupied) {
        return false;
    }
    if (bank->lanes[ui.focusedLane].activeMask.test(ui.focusedStep)) {
        return clearFocusedEvent_(nowMs);
    }
    if (!editFocusedEvent(1.0f, nowMs)) return false;
    return commitEventEdit(nowMs);
}

FLASHMEM bool SequencerCcLaneWorkflow::commitEventEdit(uint32_t nowMs) {
    if (!history_.commitCoalescedPatternEdit()) return false;
    publishFeedback_(
        ActionId::EDIT,
        contextual::OperationFeedbackStatus::APPLIED,
        Reason::NONE,
        contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        650
    );
    return true;
}

FLASHMEM bool SequencerCcLaneWorkflow::clearFocusedEvent_(uint32_t nowMs) {
    (void)commitEventEdit(nowMs);
    auto& ui = editor_.ccLaneUi;
    LaneBankPtr staged;
    if (!stageCurrentBank_(staged, false) || !staged ||
        ui.focusedLane >= staged->lanes.size()) return false;
    const auto& beforeLane = staged->lanes[ui.focusedLane];
    if (!beforeLane.occupied || !beforeLane.activeMask.test(ui.focusedStep)) return false;
    const int32_t beforeValue = beforeLane.values[ui.focusedStep];
    if (!seq::clearSequencerCcLaneEvent(
            *staged,
            ui.focusedLane,
            ui.focusedStep
        ).changed()) return false;
    auto change = prepareChange_(
        seq::SequencerHistoryActionKind::CcLaneEventClear,
        ui.focusedLane,
        ui.focusedStep
    );
    if (!change) return false;
    change->descriptor.hasValue = true;
    change->descriptor.beforeValue = beforeValue;
    change->descriptor.afterValue = -1;
    if (!installPreparedChange_(std::move(change), std::move(staged))) {
        block_(ActionId::CLEAR, Reason::HISTORY_UNAVAILABLE, nowMs);
        return false;
    }
    publishFeedback_(ActionId::CLEAR,
                     contextual::OperationFeedbackStatus::APPLIED,
                     Reason::NONE,
                     contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                     nowMs,
                     650);
    refreshProjection();
    return true;
}

FLASHMEM bool SequencerCcLaneWorkflow::removeCurrentLane_(uint32_t nowMs) {
    (void)commitEventEdit(nowMs);
    auto& ui = editor_.ccLaneUi;
    LaneBankPtr staged;
    if (!stageCurrentBank_(staged, false) || !staged ||
        !seq::removeSequencerCcLane(*staged, ui.focusedLane).changed()) return false;
    auto change = prepareChange_(
        seq::SequencerHistoryActionKind::CcLaneRemove,
        ui.focusedLane
    );
    if (!change || !installPreparedChange_(std::move(change), std::move(staged))) {
        block_(ActionId::REMOVE, Reason::HISTORY_UNAVAILABLE, nowMs);
        return false;
    }
    ui.mode = seq::SequencerCcLaneUiMode::CLOSED;
    ui.selectorIndex = 0;
    publishFeedback_(ActionId::REMOVE,
                     contextual::OperationFeedbackStatus::APPLIED,
                     Reason::NONE,
                     contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                     nowMs,
                     900);
    refreshProjection();
    return true;
}

FLASHMEM bool SequencerCcLaneWorkflow::executeTap(
    seq::SequencerCcLaneActionSlot slot,
    uint32_t nowMs
) {
    const auto spec = editor_.ccLaneUi.action(slot);
    if (!contextual::canExecute(spec.tap)) {
        if (contextual::hasTapAction(spec)) block_(spec.tap.action, spec.tap.reason, nowMs);
        return false;
    }
    switch (slot) {
        case seq::SequencerCcLaneActionSlot::BOTTOM_LEFT:
            return clearFocusedEvent_(nowMs);
        case seq::SequencerCcLaneActionSlot::BOTTOM_CENTER:
            return openSettings();
        case seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT:
            return editor_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID
                ? openSettings()
                : applySettings_(false, nowMs);
        case seq::SequencerCcLaneActionSlot::COUNT:
            return false;
    }
    return false;
}

FLASHMEM bool SequencerCcLaneWorkflow::beginGuard(
    seq::SequencerCcLaneActionSlot slot,
    uint32_t nowMs
) {
    const auto spec = editor_.ccLaneUi.action(slot);
    if (!contextual::canExecute(spec.hold) || !contextual::requiresGuard(spec)) return false;
    auto guard = editor_.ccLaneUi.actionGuard.get();
    contextual::resetGuardedAction(guard);
    if (!contextual::beginGuardedActionPress(guard, nowMs, spec.guard.durationMs)) {
        return false;
    }
    guard_slot_ = slot;
    editor_.ccLaneUi.actionGuard.set(guard);
    publishFeedback_(spec.hold.action,
                     contextual::OperationFeedbackStatus::PRESSED,
                     spec.hold.reason,
                     contextual::OperationFeedbackExpiryPolicy::MANUAL,
                     nowMs);
    return true;
}

FLASHMEM bool SequencerCcLaneWorkflow::releaseGuard(
    seq::SequencerCcLaneActionSlot slot,
    uint32_t nowMs
) {
    auto guard = editor_.ccLaneUi.actionGuard.get();
    if (guard_slot_ != slot || guard.phase == contextual::GuardedActionPhase::IDLE) {
        return executeTap(slot, nowMs);
    }
    // Input dispatch is not coupled to the periodic presenter tick. Promote a
    // sufficiently long physical hold here as well, so an exact-deadline
    // release cannot be misclassified as a tap merely because update() did not
    // run during the hold.
    if (guard.phase == contextual::GuardedActionPhase::PRESSED &&
        static_cast<uint32_t>(nowMs - guard.pressedAtMs) >=
            Config::Timing::LATCH_THRESHOLD_MS) {
        const uint32_t pressedAt = guard.pressedAtMs;
        if (contextual::armGuardedAction(guard, pressedAt)) {
            (void)contextual::updateGuardedAction(guard, nowMs);
            editor_.ccLaneUi.actionGuard.set(guard);
            publishFeedback_(
                editor_.ccLaneUi.action(slot).hold.action,
                contextual::OperationFeedbackStatus::ARMED,
                editor_.ccLaneUi.action(slot).hold.reason,
                contextual::OperationFeedbackExpiryPolicy::MANUAL,
                nowMs
            );
        }
    }
    const auto release = contextual::releaseGuardedAction(guard, nowMs);
    editor_.ccLaneUi.actionGuard.set(guard);
    if (release == contextual::GuardedActionRelease::TAP) {
        const auto spec = editor_.ccLaneUi.action(slot);
        contextual::resetGuardedAction(guard);
        editor_.ccLaneUi.actionGuard.set(guard);
        // A hold-only action (Remove) must terminate as Cancelled on an early
        // release.  Leaving the prior PRESSED feedback active would falsely
        // imply that a destructive operation was still pending.
        if (!contextual::hasTapAction(spec)) {
            publishFeedback_(spec.hold.action,
                             contextual::OperationFeedbackStatus::CANCELLED,
                             Reason::NO_ACTION,
                             contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                             nowMs,
                             650);
            return false;
        }
        return executeTap(slot, nowMs);
    }
    if (release != contextual::GuardedActionRelease::COMMITTED) {
        publishFeedback_(editor_.ccLaneUi.action(slot).hold.action,
                         contextual::OperationFeedbackStatus::CANCELLED,
                         Reason::NONE,
                         contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                         nowMs,
                         650);
        return false;
    }
    const auto action = editor_.ccLaneUi.action(slot).hold.action;
    const bool applied = slot == seq::SequencerCcLaneActionSlot::BOTTOM_LEFT
        ? removeCurrentLane_(nowMs)
        : applySettings_(true, nowMs);
    contextual::resetGuardedAction(guard);
    editor_.ccLaneUi.actionGuard.set(guard);
    if (!applied) block_(action, Reason::FAILED, nowMs);
    return applied;
}

FLASHMEM void SequencerCcLaneWorkflow::publishFeedback_(
    ActionId action,
    contextual::OperationFeedbackStatus status,
    Reason reason,
    contextual::OperationFeedbackExpiryPolicy expiry,
    uint32_t nowMs,
    uint32_t durationMs
) {
    auto feedback = editor_.ccLaneUi.operationFeedback.get();
    contextual::setOperationFeedback(
        feedback,
        action,
        laneEntity(tracks_.activeTrackIndex(), editor_.ccLaneUi.focusedLane),
        laneEntity(tracks_.activeTrackIndex(), editor_.ccLaneUi.focusedLane),
        status,
        reason,
        expiry,
        nowMs,
        durationMs
    );
    editor_.ccLaneUi.operationFeedback.set(feedback);
}

FLASHMEM void SequencerCcLaneWorkflow::block_(
    ActionId action,
    Reason reason,
    uint32_t nowMs
) {
    publishFeedback_(action,
                     contextual::OperationFeedbackStatus::BLOCKED,
                     reason,
                     contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
                     nowMs,
                     1100);
}

FLASHMEM void SequencerCcLaneWorkflow::refreshValueProjection_() {
    auto& ui = editor_.ccLaneUi;
    ui.hasAuthoredValue = false;
    ui.authoredValue = 0;
    ui.hasResolvedValue = false;
    ui.resolvedValue = 0;
    ui.liveProjection = false;
    const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
    if (bank == nullptr || ui.focusedLane >= bank->lanes.size()) return;
    const auto& lane = bank->lanes[ui.focusedLane];
    if (!lane.occupied) return;
    if (ui.focusedStep < seq::SequencerCcLaneBank::MAX_STEPS &&
        lane.activeMask.test(ui.focusedStep)) {
        ui.hasAuthoredValue = true;
        ui.authoredValue = lane.values[ui.focusedStep];
    }
    if (ui.mode != seq::SequencerCcLaneUiMode::LANE_GRID ||
        !status_bar_.playing.get()) {
        return;
    }
    const auto live = projectSequencerCcLaneLive(
        midi_cc_coordinator_,
        {tracks_.activeTrackIndex(), ui.focusedLane},
        lane,
        services_.trackRoute(tracks_.activeTrackIndex())
    );
    ui.liveProjection = live.lanePresent;
    ui.hasResolvedValue = live.lanePresent && live.hasOutput;
    ui.resolvedValue = live.outputValue;
    ui.winnerClass = live.winnerClass;
}

FLASHMEM void SequencerCcLaneWorkflow::refreshActions_(
    const SequencerCcLanePreflight& preflight
) {
    auto& ui = editor_.ccLaneUi;
    ui.actions = {};
    const auto entity = laneEntity(tracks_.activeTrackIndex(), ui.focusedLane);
    auto makeSpec = [&](seq::SequencerCcLaneActionSlot slot) -> contextual::ContextActionSpec& {
        auto& spec = ui.actions[static_cast<size_t>(slot)];
        spec.scope = contextual::ContextScope::CC_LANE;
        spec.source = entity;
        spec.target = entity;
        return spec;
    };

    if (ui.mode == seq::SequencerCcLaneUiMode::LANE_SELECTOR ||
        ui.mode == seq::SequencerCcLaneUiMode::LANE_GRID) {
        const auto slot = ui.mode == seq::SequencerCcLaneUiMode::LANE_GRID
            ? seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT
            : seq::SequencerCcLaneActionSlot::BOTTOM_CENTER;
        auto& settings = makeSpec(slot);
        const bool laneAvailable = ui.mode == seq::SequencerCcLaneUiMode::LANE_GRID ||
            selectorLane() >= 0;
        settings.tap = variant(ActionId::OPEN_SETTINGS,
                               ActionImpact::NON_MUTATING,
                               laneAvailable ? Availability::AVAILABLE : Availability::DISABLED,
                               laneAvailable ? Reason::NONE : Reason::EMPTY_SELECTION,
                               Icon::EDIT,
                               Tone::NEUTRAL);
    }
    if (ui.mode == seq::SequencerCcLaneUiMode::LANE_GRID) {
        auto& clear = makeSpec(seq::SequencerCcLaneActionSlot::BOTTOM_LEFT);
        clear.tap = variant(ActionId::CLEAR,
                            ActionImpact::VALUE_EDIT,
                            ui.hasAuthoredValue ? Availability::AVAILABLE : Availability::DISABLED,
                            ui.hasAuthoredValue ? Reason::NONE : Reason::NO_ACTION,
                            Icon::CLEAR,
                            Tone::NEUTRAL);
    }
    if (ui.mode == seq::SequencerCcLaneUiMode::LANE_SETTINGS) {
        const ActionId action = ActionId::APPLY;
        auto& apply = makeSpec(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT);
        const bool dirtyEnough = ui.draftDirty;
        const Reason routeReason = preflight.routeValid ? Reason::NONE : Reason::NO_ROUTE;
        if (preflight.laneConflict) {
            apply.tap = variant(action, ActionImpact::CONSTRUCTIVE,
                                Availability::DISABLED, Reason::CONFLICT,
                                Icon::CONFLICT, Tone::AMBER);
            apply.hold = apply.tap;
        } else if (preflight.macroConflict) {
            apply.tap = variant(action, ActionImpact::CONSTRUCTIVE,
                                Availability::DISABLED, Reason::CONFLICT,
                                Icon::CONFLICT, Tone::AMBER);
            apply.hold = variant(action, ActionImpact::CONSTRUCTIVE,
                                 Availability::WARNING, Reason::CONFLICT,
                                 Icon::HOLD, Tone::AMBER);
            apply.guard = {
                .kind = contextual::ContextGuardKind::HOLD,
                .durationMs = seq::SequencerCcLaneUiState::ACTION_GUARD_MS,
            };
        } else {
            apply.tap = variant(action, ActionImpact::CONSTRUCTIVE,
                                dirtyEnough ? (preflight.routeValid
                                    ? Availability::AVAILABLE : Availability::WARNING)
                                    : Availability::DISABLED,
                                dirtyEnough ? routeReason : Reason::NO_ACTION,
                                Icon::APPLY,
                                preflight.routeValid ? Tone::GREEN : Tone::AMBER);
        }
    }
    if (ui.mode == seq::SequencerCcLaneUiMode::LANE_SETTINGS) {
        auto& remove = makeSpec(seq::SequencerCcLaneActionSlot::BOTTOM_LEFT);
        remove.hold = variant(ActionId::REMOVE,
                              ActionImpact::DESTRUCTIVE,
                              Availability::AVAILABLE,
                              Reason::NONE,
                              Icon::REMOVE,
                              Tone::RED);
        remove.guard = {
            .kind = contextual::ContextGuardKind::HOLD,
            .durationMs = seq::SequencerCcLaneUiState::ACTION_GUARD_MS,
        };
    }
}

FLASHMEM void SequencerCcLaneWorkflow::refreshProjection() {
    auto& ui = editor_.ccLaneUi;
    SequencerCcLanePreflight preflight{};
    if (ui.mode == seq::SequencerCcLaneUiMode::LANE_SETTINGS) {
        preflight = services_.preflight(
            tracks_.activeTrackIndex(),
            ui.focusedLane,
            ui.draft
        );
    } else {
        const auto* bank = seq::sequencerCcLaneView(editor_.pattern);
        if (bank != nullptr && ui.focusedLane < bank->lanes.size() &&
            bank->lanes[ui.focusedLane].occupied) {
            seq::SequencerCcLaneDraft current{};
            current.destination = bank->lanes[ui.focusedLane].destination;
            current.initialValue = bank->lanes[ui.focusedLane].initialValue;
            current.acceptedMacroConflict =
                bank->lanes[ui.focusedLane].acceptedMacroConflict;
            preflight = services_.preflight(
                tracks_.activeTrackIndex(),
                ui.focusedLane,
                current
            );
        }
    }
    ui.routeValid = preflight.routeValid;
    ui.laneConflict = preflight.laneConflict;
    ui.macroConflict = preflight.macroConflict;
    refreshValueProjection_();
    refreshActions_(preflight);
    ui.bump();
}

FLASHMEM void SequencerCcLaneWorkflow::update(uint32_t nowMs) {
    const bool transportPlaying = status_bar_.playing.get();
    if (transportPlaying != last_transport_playing_) {
        last_transport_playing_ = transportPlaying;
        refreshProjection();
    }
    auto feedback = editor_.ccLaneUi.operationFeedback.get();
    if (contextual::updateOperationFeedback(feedback, nowMs)) {
        editor_.ccLaneUi.operationFeedback.set(feedback);
        if (!feedback.active) {
            editor_.ccLaneUi.transitionAppliedFeedback = false;
        }
    }
    auto guard = editor_.ccLaneUi.actionGuard.get();
    if (guard.phase == contextual::GuardedActionPhase::PRESSED &&
        static_cast<uint32_t>(nowMs - guard.pressedAtMs) >=
            Config::Timing::LATCH_THRESHOLD_MS) {
        const uint32_t pressedAt = guard.pressedAtMs;
        if (contextual::armGuardedAction(guard, pressedAt)) {
            editor_.ccLaneUi.actionGuard.set(guard);
            publishFeedback_(
                editor_.ccLaneUi.action(guard_slot_).hold.action,
                contextual::OperationFeedbackStatus::ARMED,
                editor_.ccLaneUi.action(guard_slot_).hold.reason,
                contextual::OperationFeedbackExpiryPolicy::MANUAL,
                nowMs
            );
        }
    }
    if (guard.phase == contextual::GuardedActionPhase::ARMED &&
        contextual::updateGuardedAction(guard, nowMs)) {
        editor_.ccLaneUi.actionGuard.set(guard);
    }
}

}  // namespace core::handler
