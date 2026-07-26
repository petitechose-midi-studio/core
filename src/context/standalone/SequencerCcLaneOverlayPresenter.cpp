#include "context/standalone/SequencerCcLaneOverlayPresenter.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>

#include "handler/sequencer/SequencerCcLaneLiveProjection.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/contextual/ContextActionSpec.hpp"
#include "context/standalone/SequencerCcLaneOverlayVisuals.hpp"
#include "state/sequencer/SequencerCcLaneDraftLayout.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerPatternRegionOps.hpp"
#include "state/sequencer/SequencerCcLaneRouting.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/SequencerCcLaneGridProjection.hpp"
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone {

FLASHMEM SequencerCcLaneOverlayPresenter::~SequencerCcLaneOverlayPresenter() {}

namespace {

namespace contextual = core::state::contextual;
namespace seq = core::state::sequencer;

using Mode = seq::SequencerCcLaneUiMode;
using Slot = seq::SequencerCcLaneActionSlot;

constexpr uint8_t GRID_WINDOW = seq::SequencerPatternState::STEPS_PER_PAGE;

const char* routePolicyLabel(seq::SequencerCcLaneRoutePolicy policy) {
    return policy == seq::SequencerCcLaneRoutePolicy::PINNED ? "Fixed" : "Track";
}

const char* routePolicyValue(seq::SequencerCcLaneRoutePolicy policy) {
    return policy == seq::SequencerCcLaneRoutePolicy::PINNED
        ? "Fixed channel"
        : "Follow track";
}

uint8_t eventCount(const oc::note::sequencer::StepBitMask128& mask) {
    uint8_t count = 0;
    for (uint16_t step = 0; step < seq::SequencerCcLaneBank::MAX_STEPS; ++step) {
        if (mask.test(static_cast<uint8_t>(step))) ++count;
    }
    return count;
}

const char* fieldLabel(seq::SequencerCcLaneDraftField field) {
    switch (field) {
        case seq::SequencerCcLaneDraftField::CONTROLLER: return "Controller";
        case seq::SequencerCcLaneDraftField::ROUTE_POLICY: return "Route";
        case seq::SequencerCcLaneDraftField::PINNED_CHANNEL: return "Channel";
        case seq::SequencerCcLaneDraftField::MINIMUM: return "Minimum";
        case seq::SequencerCcLaneDraftField::MAXIMUM: return "Maximum";
        case seq::SequencerCcLaneDraftField::INITIAL: return "New event";
        case seq::SequencerCcLaneDraftField::ADVANCED: return "Advanced";
        case seq::SequencerCcLaneDraftField::COUNT: return "";
    }
    return "";
}

const char* feedbackLabel(const contextual::OperationFeedbackState& feedback) {
    if (!feedback.active) return "";
    using Status = contextual::OperationFeedbackStatus;
    switch (feedback.status) {
        case Status::PREVIEW: return "Preview";
        case Status::PRESSED: return "Hold";
        case Status::ARMED: return "Armed";
        case Status::QUEUED: return "Queued";
        case Status::APPLIED: return "Applied";
        case Status::CANCELLED: return "Cancelled";
        case Status::BLOCKED: return "Blocked";
        case Status::WARNING: return "Warning";
        case Status::CONFLICT: return "Conflict";
        case Status::FAILED: return "Failed";
        case Status::NONE: return "";
    }
    return "";
}

const char* iconFor(contextual::ContextIconId icon) {
    using Icon = contextual::ContextIconId;
    switch (icon) {
        case Icon::CREATE:
        case Icon::APPLY:
            return ::standalone::icons::ACTION_APPLY;
        case Icon::ENTER:
            return ::standalone::icons::ACTION_VALIDATE;
        case Icon::EDIT:
            return ::standalone::icons::SETTINGS_GEAR;
        case Icon::CLEAR: return ::standalone::icons::ACTION_CLEAR;
        case Icon::REMOVE: return ::standalone::icons::ACTION_REMOVE;
        case Icon::ROUTE_INHERITED:
            return ::standalone::icons::ROUTING;
        case Icon::ROUTE_PINNED:
            return ::standalone::icons::ROUTE_PIN;
        case Icon::CONFLICT: return ::standalone::icons::STATUS_CONFLICT;
        case Icon::ERROR: return ::standalone::icons::STATUS_ERROR;
        case Icon::QUEUED: return ::standalone::icons::STATUS_QUEUED;
        case Icon::APPLIED: return ::standalone::icons::ACTION_VALIDATE;
        case Icon::NO_ROUTE: return ::standalone::icons::STATUS_ERROR;
        case Icon::WARNING: return ::standalone::icons::STATUS_WARNING;
        case Icon::HOLD:
            return ::standalone::icons::ACTION_OVERWRITE;
        default:
            return ::standalone::icons::MIDI_CC;
    }
}

core::ui::ContextActionStripTone stripTone(contextual::ContextTone tone) {
    using Tone = contextual::ContextTone;
    using StripTone = core::ui::ContextActionStripTone;
    switch (tone) {
        case Tone::GREEN: return StripTone::POSITIVE;
        case Tone::BLUE: return StripTone::CONSTRUCTIVE;
        case Tone::AMBER: return StripTone::WARNING;
        case Tone::RED: return StripTone::DESTRUCTIVE;
        case Tone::DEFAULT:
        case Tone::NEUTRAL:
        default: return StripTone::NEUTRAL;
    }
}

const char* winnerLabel(core::state::shared::MidiCcCandidateClass winner) {
    using Winner = core::state::shared::MidiCcCandidateClass;
    switch (winner) {
        case Winner::LIVE_MANUAL: return "Manual wins";
        case Winner::SEQUENCER_CC_LANE: return "Lane";
        case Winner::MACRO_COMPUTED: return "Macro automation";
        case Winner::MACRO_STATIC: return "Macro";
    }
    return "";
}

const char* transitionLabel(seq::SequencerCcLaneTransition transition) {
    switch (transition) {
        case seq::SequencerCcLaneTransition::HOLD: return "Hold";
        case seq::SequencerCcLaneTransition::LINEAR: return "Linear";
        case seq::SequencerCcLaneTransition::EASE_IN: return "Ease In";
        case seq::SequencerCcLaneTransition::EASE_OUT: return "Ease Out";
        case seq::SequencerCcLaneTransition::EASE_IN_OUT: return "Ease In/Out";
    }
    return "Hold";
}

}  // namespace

FLASHMEM SequencerCcLaneOverlayPresenter::SequencerCcLaneOverlayPresenter(
    StateRefs state,
    ms::ui::VirtualListKeyValueOverlay& overlay,
    core::ui::ContextActionStrip& actionStrip
)
    : state_(state)
    , overlay_(overlay)
    , action_strip_(actionStrip)
    , grid_(overlay.getElement())
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("SequencerCcLane"),
          &SequencerCcLaneOverlayPresenter::drainRenderQueue,
          this
      ) {}

FLASHMEM bool SequencerCcLaneOverlayPresenter::bind() {
    watcher_.bind<&SequencerCcLaneOverlayPresenter::requestRender>(
        *this, 0, "SequencerCcLane.all"
    );
    return render_scheduler_.valid() && watcher_.watchAll(
        state_.sequencer.ccLaneUi.revision,
        state_.sequencer.ccLaneUi.actionGuard,
        state_.sequencer.ccLaneUi.operationFeedback,
        state_.statusBar.playing,
        state_.sequencer.playheadStep,
        state_.projectTracks.revision
    );
}

FLASHMEM void SequencerCcLaneOverlayPresenter::drainRenderQueue(
    void* context,
    uint32_t
) {
    auto* self = static_cast<SequencerCcLaneOverlayPresenter*>(context);
    if (self) self->render();
}

FLASHMEM void SequencerCcLaneOverlayPresenter::requestRender() {
    render_scheduler_.request(1U);
}

FLASHMEM void SequencerCcLaneOverlayPresenter::render() {
    renderOverlay();
    renderActionStrip();
}

FLASHMEM void SequencerCcLaneOverlayPresenter::renderOverlay() {
    const auto& ui = state_.sequencer.ccLaneUi;
    if (!ui.visible()) {
        overlay_.render({.visible = false});
        grid_.render({.visible = false});
        return;
    }

    for (auto& key : keys_) key[0] = '\0';
    for (auto& value : values_) value[0] = '\0';
    title_[0] = '\0';
    meta_[0] = '\0';
    hint_[0] = '\0';

    std::array<ms::ui::KeyValueRow, ROW_CAPACITY> rows{};
    int rowCount = 0;
    int selectedIndex = 0;
    const auto* bank = seq::sequencerCcLaneView(state_.sequencer.pattern);
    const auto activeTrack = state_.tracks.activeTrackIndex();
    const uint8_t inheritedChannel =
        core::state::project::projectTrackMidiChannel(
            state_.projectTracks,
            activeTrack
        );
    const auto feedback = ui.operationFeedback.get();
    const char* feedbackText = feedbackLabel(feedback);

    if (ui.mode != Mode::LANE_GRID &&
        ui.mode != Mode::TRANSITION_PICKER) {
        grid_.render({.visible = false});
    }

    auto addRow = [&](const char* key, const char* value, const char* icon) {
        if (rowCount >= static_cast<int>(ROW_CAPACITY)) return;
        std::snprintf(keys_[rowCount].data(), keys_[rowCount].size(), "%s", key ? key : "");
        std::snprintf(values_[rowCount].data(), values_[rowCount].size(), "%s", value ? value : "");
        rows[static_cast<size_t>(rowCount)] = ms::ui::KeyValueRow{
            .key = keys_[rowCount].data(),
            .value = values_[rowCount].data(),
            .icon = icon ? icon : "",
            .iconFont = icon ? standalone_fonts.icons_14 : nullptr,
            .iconColor = ::standalone::theme::color::MACRO_CC_COLOR,
        };
        ++rowCount;
    };

    if (ui.mode == Mode::LANE_SELECTOR) {
        std::snprintf(title_.data(), title_.size(), "CC lanes · Track %u",
                      static_cast<unsigned>(activeTrack + 1U));
        uint8_t item = 0;
        if (bank != nullptr) {
            for (uint8_t laneIndex = 0; laneIndex < bank->lanes.size(); ++laneIndex) {
                const auto& lane = bank->lanes[laneIndex];
                if (!lane.occupied) continue;
                char key[24] = {};
                char value[48] = {};
                std::snprintf(key, sizeof(key), "CC%u · Lane %u",
                              static_cast<unsigned>(lane.destination.controller),
                              static_cast<unsigned>(laneIndex + 1U));
                if (lane.destination.routePolicy == seq::SequencerCcLaneRoutePolicy::PINNED) {
                    std::snprintf(value, sizeof(value), "Fixed Ch%u · %u events",
                                  static_cast<unsigned>(lane.destination.pinnedChannel + 1U),
                                  static_cast<unsigned>(eventCount(lane.activeMask)));
                } else {
                    std::snprintf(value, sizeof(value), "Track Ch%u · %u events",
                                  static_cast<unsigned>(inheritedChannel + 1U),
                                  static_cast<unsigned>(eventCount(lane.activeMask)));
                }
                addRow(key, value,
                       lane.destination.routePolicy ==
                               seq::SequencerCcLaneRoutePolicy::PINNED
                           ? ::standalone::icons::ROUTE_PIN
                           : ::standalone::icons::ROUTING);
                ++item;
            }
        }
        const int8_t freeLane = bank
            ? seq::firstFreeSequencerCcLane(*bank)
            : 0;
        if (freeLane >= 0) {
            char key[24] = {};
            char value[48] = {};
            const uint8_t slot = static_cast<uint8_t>(freeLane);
            std::snprintf(
                key,
                sizeof(key),
                "+ Lane %u",
                static_cast<unsigned>(slot + 1U)
            );
            std::snprintf(
                value,
                sizeof(value),
                "CC%u · Track",
                static_cast<unsigned>(
                    state_.projectNavigation.ccLaneDefaultControllers[slot]
                )
            );
            addRow(key, value, ::standalone::icons::ACTION_APPLY);
        }
        selectedIndex = std::min<int>(ui.selectorIndex, std::max(0, rowCount - 1));
        std::snprintf(meta_.data(), meta_.size(), "%u/4 lanes%s%s",
                      static_cast<unsigned>(item),
                      feedbackText[0] ? " · " : "",
                      feedbackText);
    } else if (ui.mode == Mode::LANE_GRID) {
        const seq::SequencerCcLane* lane = nullptr;
        if (bank != nullptr && ui.focusedLane < bank->lanes.size() &&
            bank->lanes[ui.focusedLane].occupied) {
            lane = &bank->lanes[ui.focusedLane];
        }
        overlay_.render({
            .title = "",
            .meta = "",
            .rows = nullptr,
            .rowCount = 0,
            .selectedIndex = 0,
            .visible = true,
            .dataRevision = ui.revision.get(),
        });
        if (lane == nullptr) {
            grid_.render({.visible = false});
            return;
        }

        std::snprintf(
            title_.data(),
            title_.size(),
            "Lane %u · CC%u · %s",
            static_cast<unsigned>(ui.focusedLane + 1U),
            static_cast<unsigned>(lane->destination.controller),
            routePolicyLabel(lane->destination.routePolicy)
        );
        const uint8_t length = std::max<uint8_t>(
            1U,
            state_.sequencer.pattern.length.get()
        );
        const uint8_t start = static_cast<uint8_t>(
            (ui.focusedStep / GRID_WINDOW) * GRID_WINDOW
        );
        const uint8_t end = std::min<uint8_t>(
            length,
            static_cast<uint8_t>(start + GRID_WINDOW)
        );
        const uint8_t channel = lane->destination.routePolicy ==
                seq::SequencerCcLaneRoutePolicy::PINNED
            ? lane->destination.pinnedChannel
            : inheritedChannel;
        const auto live = core::handler::projectSequencerCcLaneLive(
            state_.midiCcCoordinator,
            {activeTrack, ui.focusedLane},
            *lane,
            seq::makeSequencerCcTrackRoute(0, inheritedChannel)
        );

        uint32_t statusColor = ::standalone::theme::color::TEXT_SECONDARY;
        if (!ui.routeValid) {
            std::snprintf(meta_.data(), meta_.size(), "No MIDI route");
            statusColor = ::standalone::theme::color::MACRO_AUTOMATION_RECORDING;
        } else if (!state_.statusBar.playing.get()) {
            std::snprintf(meta_.data(), meta_.size(), "%s Ch%u · Stopped",
                          routePolicyLabel(lane->destination.routePolicy),
                          static_cast<unsigned>(channel + 1U));
        } else if (!live.lanePresent) {
            std::snprintf(meta_.data(), meta_.size(), "Playing · waiting for event");
            statusColor = ::standalone::theme::color::MACRO_SUSPENDED;
        } else if (!live.hasOutput) {
            std::snprintf(meta_.data(), meta_.size(), "Playing · no MIDI output");
            statusColor = ::standalone::theme::color::MACRO_SUSPENDED;
        } else {
            std::snprintf(meta_.data(), meta_.size(), "Out %u · %s",
                          static_cast<unsigned>(live.outputValue),
                          winnerLabel(live.winnerClass));
            statusColor = live.winnerClass ==
                    core::state::shared::MidiCcCandidateClass::SEQUENCER_CC_LANE
                ? ::standalone::theme::color::MACRO_CC_COLOR
                : ::standalone::theme::color::MACRO_SUSPENDED;
        }
        if (ui.transitionAppliedFeedback) {
            std::snprintf(
                hint_.data(), hint_.size(), "Step %u · %s applied",
                static_cast<unsigned>(ui.transitionStep + 1U),
                transitionLabel(ui.selectedTransition)
            );
        } else if (feedbackText[0]) {
            std::snprintf(hint_.data(), hint_.size(), "%s · knobs value · press toggle",
                          feedbackText);
        } else {
            std::snprintf(hint_.data(), hint_.size(), "Steps %u-%u · knobs value · hold+turn shape",
                          static_cast<unsigned>(start + 1U),
                          static_cast<unsigned>(end));
        }

        core::ui::SequencerCcLaneGridProps gridProps{
            .visible = true,
            .title = title_.data(),
            .meta = meta_.data(),
            .hint = hint_.data(),
            .accentColor = ::standalone::theme::color::MACRO_CC_COLOR,
            .statusColor = statusColor,
        };
        const int16_t playhead = state_.sequencer.playheadStep.get();
        const auto region = seq::patternPlaybackRegion(state_.sequencer.pattern);
        for (uint8_t cell = 0; cell < GRID_WINDOW; ++cell) {
            const uint8_t step = static_cast<uint8_t>(start + cell);
            const bool visible = step < length;
            const bool authored = visible && lane->activeMask.test(step);
            gridProps.cells[cell] = {
                .visible = visible,
                .authored = authored,
                .focused = visible && step == ui.focusedStep,
                .playhead = visible && state_.statusBar.playing.get() &&
                    playhead >= 0 && step == static_cast<uint8_t>(playhead),
                .step = step,
                .value = authored ? lane->values[step] : lane->initialValue,
                .transition = authored
                    ? seq::sequencerCcLaneTransition(*lane, step)
                    : seq::SequencerCcLaneTransition::HOLD,
            };
            if (cell + 1U < GRID_WINDOW) {
                const uint8_t nextStep = static_cast<uint8_t>(step + 1U);
                if (visible && nextStep < length) {
                    gridProps.segments[cell] =
                        core::ui::sequencer::projectSequencerCcLaneGridSegment(
                            *lane,
                            step,
                            region
                        );
                }
            }
        }
        if (!ui.transitionAppliedFeedback && feedbackText[0] == '\0' &&
            ui.focusedStep < length && lane->activeMask.test(ui.focusedStep)) {
            const auto span =
                core::ui::sequencer::sequencerCcLaneProjectionSpanAtStep(
                    *lane,
                    ui.focusedStep,
                    region
                );
            if (span.valid) {
                gridProps.contextualHint = true;
                gridProps.hintSourceStep = ui.focusedStep;
                gridProps.hintTargetStep = span.target;
                gridProps.hintTransition = seq::sequencerCcLaneTransition(
                    *lane,
                    ui.focusedStep
                );
            }
        }
        grid_.render(gridProps);
        return;
    } else if (ui.mode == Mode::TRANSITION_PICKER) {
        std::snprintf(title_.data(), title_.size(), "Step %u · Transition",
                      static_cast<unsigned>(ui.transitionStep + 1U));
        std::snprintf(meta_.data(), meta_.size(), "To next authored event");
        std::snprintf(hint_.data(), hint_.size(), "%s",
                      ui.compactTransitionPicker
                          ? "NAV hold + turn · release apply"
                          : "Held knob/NAV choose · release/press apply");
        overlay_.render({
            .title = "",
            .meta = "",
            .rows = nullptr,
            .rowCount = 0,
            .selectedIndex = 0,
            .visible = true,
            .dataRevision = ui.revision.get(),
        });
        grid_.render({
            .visible = true,
            .title = title_.data(),
            .meta = meta_.data(),
            .hint = hint_.data(),
            .accentColor = ::standalone::theme::color::MACRO_CC_COLOR,
            .statusColor = ::standalone::theme::color::MACRO_CC_COLOR,
            .transitionPicker = true,
            .compactTransitionPicker = ui.compactTransitionPicker,
            .pickerSelection = ui.selectedTransition,
        });
        return;
    } else {
        const auto& draft = ui.draft;
        std::snprintf(title_.data(), title_.size(), "Lane %u settings",
                      static_cast<unsigned>(ui.focusedLane + 1U));
        const auto layout = seq::buildSequencerCcLaneDraftLayout(
            draft.destination.routePolicy,
            ui.advancedSettings
        );
        for (uint8_t fieldIndex = 0; fieldIndex < layout.count; ++fieldIndex) {
            const auto field = layout.fieldAt(fieldIndex);
            char value[48] = {};
            switch (field) {
                case seq::SequencerCcLaneDraftField::CONTROLLER:
                    std::snprintf(value, sizeof(value), "CC%u",
                                  static_cast<unsigned>(draft.destination.controller));
                    break;
                case seq::SequencerCcLaneDraftField::ROUTE_POLICY:
                    std::snprintf(value, sizeof(value), "%s",
                                  routePolicyValue(draft.destination.routePolicy));
                    break;
                case seq::SequencerCcLaneDraftField::PINNED_CHANNEL:
                    std::snprintf(value, sizeof(value), "Ch%u",
                                  static_cast<unsigned>(draft.destination.pinnedChannel + 1U));
                    break;
                case seq::SequencerCcLaneDraftField::MINIMUM:
                    std::snprintf(value, sizeof(value), "%u",
                                  static_cast<unsigned>(draft.destination.minimum));
                    break;
                case seq::SequencerCcLaneDraftField::MAXIMUM:
                    std::snprintf(value, sizeof(value), "%u",
                                  static_cast<unsigned>(draft.destination.maximum));
                    break;
                case seq::SequencerCcLaneDraftField::INITIAL:
                    std::snprintf(value, sizeof(value), "%u",
                                  static_cast<unsigned>(draft.initialValue));
                    break;
                case seq::SequencerCcLaneDraftField::ADVANCED:
                    std::snprintf(value, sizeof(value), "%s",
                                  ui.advancedSettings ? "Hide" : "Show");
                    break;
                case seq::SequencerCcLaneDraftField::COUNT:
                    break;
            }
            const char* icon = ::standalone::icons::MIDI_CC;
            if (field == seq::SequencerCcLaneDraftField::ROUTE_POLICY ||
                field == seq::SequencerCcLaneDraftField::PINNED_CHANNEL) {
                icon = draft.destination.routePolicy ==
                        seq::SequencerCcLaneRoutePolicy::PINNED
                    ? ::standalone::icons::ROUTE_PIN
                    : ::standalone::icons::ROUTING;
            } else if (field == seq::SequencerCcLaneDraftField::ADVANCED) {
                icon = ::standalone::icons::SETTINGS_GEAR;
            }
            addRow(fieldLabel(field), value, icon);
        }
        selectedIndex = layout.indexOf(ui.focusedField);
        const uint8_t channel = draft.destination.routePolicy ==
                seq::SequencerCcLaneRoutePolicy::PINNED
            ? draft.destination.pinnedChannel
            : inheritedChannel;
        if (ui.laneConflict) {
            std::snprintf(meta_.data(), meta_.size(), "Duplicate destination · Blocked");
        } else if (ui.macroConflict) {
            std::snprintf(meta_.data(), meta_.size(), "Macro conflict · Hold Apply");
        } else if (!ui.routeValid) {
            std::snprintf(meta_.data(), meta_.size(), "No MIDI route · Lane stays silent");
        } else if (feedbackText[0]) {
            std::snprintf(meta_.data(), meta_.size(), "%s Ch%u · %s",
                          routePolicyLabel(draft.destination.routePolicy),
                          static_cast<unsigned>(channel + 1U),
                          feedbackText);
        } else {
            std::snprintf(meta_.data(), meta_.size(), "%s Ch%u · Ready",
                          routePolicyLabel(draft.destination.routePolicy),
                          static_cast<unsigned>(channel + 1U));
        }
    }

    overlay_.render({
        .title = title_.data(),
        .meta = meta_.data(),
        .rows = rows.data(),
        .rowCount = rowCount,
        .selectedIndex = std::clamp(selectedIndex, 0, std::max(0, rowCount - 1)),
        .dimUnselected = false,
        .visible = true,
        .dataRevision = ui.revision.get(),
    });
}

FLASHMEM void SequencerCcLaneOverlayPresenter::renderActionStrip() {
    const auto& ui = state_.sequencer.ccLaneUi;
    if (!ui.visible()) {
        action_strip_.render({.visible = false});
        return;
    }

    core::ui::ContextActionStripProps props{.visible = true};
    const auto guard = ui.actionGuard.get();
    const auto feedback = ui.operationFeedback.get();
    for (size_t index = 0; index < props.slots.size(); ++index) {
        const auto slot = static_cast<Slot>(index);
        const auto& spec = ui.action(slot);
        const auto& action = cc_lane_overlay_visuals::visibleVariant(
            spec, guard, feedback
        );
        auto& out = props.slots[index];
        out.visualState = cc_lane_overlay_visuals::stripVisual(
            action, guard, feedback
        );
        if (out.visualState == core::ui::ContextActionStripVisualState::HIDDEN) continue;
        const auto visualPolicy =
            cc_lane_overlay_visuals::projectedVisualPolicy(action, feedback);
        out.tone = stripTone(visualPolicy.tone);
        out.showIcon = true;
        out.icon = iconFor(visualPolicy.icon);
        out.iconUsesStandaloneFont = true;
        out.iconSize = ::standalone::icons::Size::M;
        const bool guardMatches = contextual::hasHoldAction(spec) &&
            feedback.active && feedback.action == spec.hold.action;
        out.holdActive = guardMatches &&
            (guard.phase == contextual::GuardedActionPhase::PRESSED ||
             guard.phase == contextual::GuardedActionPhase::ARMED);
        out.holdStartedAtMs = guard.phase == contextual::GuardedActionPhase::PRESSED
            ? guard.pressedAtMs
            : guard.armedAtMs;
        out.holdDurationMs = guard.guardDurationMs;
    }
    action_strip_.render(props);
    if (auto* strip = action_strip_.getElement()) {
        lv_obj_move_foreground(strip);
    }
}

}  // namespace core::context::standalone
