#include "context/standalone/SequencerCcLaneOverlayPresenter.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>

#include "state/contextual/ContextActionSpec.hpp"
#include "context/standalone/SequencerCcLaneOverlayVisuals.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerCcLaneRouting.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone {
namespace {

namespace contextual = core::state::contextual;
namespace seq = core::state::sequencer;

using Mode = seq::SequencerCcLaneUiMode;
using Slot = seq::SequencerCcLaneActionSlot;

constexpr uint8_t GRID_WINDOW = seq::SequencerPatternState::STEPS_PER_PAGE;

const char* routePolicyLabel(seq::SequencerCcLaneRoutePolicy policy) {
    return policy == seq::SequencerCcLaneRoutePolicy::PINNED ? "Pinned" : "Inherited";
}

uint8_t eventCount(const oc::note::sequencer::StepBitMask128& mask) {
    uint8_t count = 0;
    for (uint8_t step = 0; step < seq::SequencerCcLaneBank::MAX_STEPS; ++step) {
        if (mask.test(step)) ++count;
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
        case seq::SequencerCcLaneDraftField::INITIAL: return "Initial";
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

}  // namespace

FLASHMEM SequencerCcLaneOverlayPresenter::SequencerCcLaneOverlayPresenter(
    StateRefs state,
    ms::ui::VirtualListKeyValueOverlay& overlay,
    core::ui::ContextActionStrip& actionStrip
)
    : state_(state)
    , overlay_(overlay)
    , action_strip_(actionStrip)
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
        state_.statusBar.playing
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
        return;
    }

    for (auto& key : keys_) key[0] = '\0';
    for (auto& value : values_) value[0] = '\0';
    title_[0] = '\0';
    meta_[0] = '\0';

    std::array<ms::ui::KeyValueRow, ROW_CAPACITY> rows{};
    int rowCount = 0;
    int selectedIndex = 0;
    const auto* bank = seq::sequencerCcLaneView(state_.sequencer.pattern);
    const auto activeTrack = state_.tracks.activeTrackIndex();
    const auto feedback = ui.operationFeedback.get();
    const char* feedbackText = feedbackLabel(feedback);

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
                std::snprintf(key, sizeof(key), "Lane %u · CC%u",
                              static_cast<unsigned>(laneIndex + 1U),
                              static_cast<unsigned>(lane.destination.controller));
                if (lane.destination.routePolicy == seq::SequencerCcLaneRoutePolicy::PINNED) {
                    std::snprintf(value, sizeof(value), "Pinned · Ch%u · %u events",
                                  static_cast<unsigned>(lane.destination.pinnedChannel + 1U),
                                  static_cast<unsigned>(eventCount(lane.activeMask)));
                } else {
                    std::snprintf(value, sizeof(value), "Inherited · Ch%u · %u events",
                                  static_cast<unsigned>(state_.sequencer.pattern.midiChannel.get() + 1U),
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
        addRow("+ Add lane", "Silent draft", ::standalone::icons::ACTION_APPLY);
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
        if (lane != nullptr) {
            // Keep the persistent identity compact enough to share the 320 px
            // header with live route/conflict truth without wrapping.
            std::snprintf(title_.data(), title_.size(), "CC%u · L%u",
                          static_cast<unsigned>(lane->destination.controller),
                          static_cast<unsigned>(ui.focusedLane + 1U));
            const uint8_t length = std::max<uint8_t>(1U, state_.sequencer.pattern.length.get());
            const uint8_t start = static_cast<uint8_t>((ui.focusedStep / GRID_WINDOW) * GRID_WINDOW);
            const uint8_t end = std::min<uint8_t>(length, static_cast<uint8_t>(start + GRID_WINDOW));
            for (uint8_t step = start; step < end; ++step) {
                char key[16] = {};
                char value[48] = {};
                std::snprintf(key, sizeof(key), "Step %u", static_cast<unsigned>(step + 1U));
                if (!lane->activeMask.test(step)) {
                    std::snprintf(value, sizeof(value), "-- | Init %u",
                                  static_cast<unsigned>(lane->initialValue));
                } else if (!ui.routeValid) {
                    std::snprintf(value, sizeof(value), "Set %u | No route",
                                  static_cast<unsigned>(lane->values[step]));
                } else {
                    std::snprintf(value, sizeof(value), "Set %u | %s %u",
                                  static_cast<unsigned>(lane->values[step]),
                                  state_.statusBar.playing.get() ? "Live" : "Out",
                                  static_cast<unsigned>(lane->values[step]));
                }
                addRow(key, value,
                       !ui.routeValid
                           ? ::standalone::icons::STATUS_ERROR
                           : (ui.macroConflict
                                  ? ::standalone::icons::STATUS_CONFLICT
                                  : ::standalone::icons::MIDI_CC));
            }
            selectedIndex = ui.focusedStep >= start
                ? static_cast<int>(ui.focusedStep - start) : 0;
            const char* route = routePolicyLabel(lane->destination.routePolicy);
            const uint8_t channel = lane->destination.routePolicy ==
                    seq::SequencerCcLaneRoutePolicy::PINNED
                ? lane->destination.pinnedChannel
                : state_.sequencer.pattern.midiChannel.get();
            if (!ui.routeValid) {
                std::snprintf(meta_.data(), meta_.size(), "%s · %s · No route",
                              state_.statusBar.playing.get() ? "Live" : "Preview", route);
            } else if (ui.macroConflict) {
                std::snprintf(meta_.data(), meta_.size(), "%s · %s Ch%u · Lane wins",
                              state_.statusBar.playing.get() ? "Live" : "Preview",
                              route, static_cast<unsigned>(channel + 1U));
            } else if (feedbackText[0]) {
                std::snprintf(meta_.data(), meta_.size(), "%s · %s Ch%u · %s",
                              state_.statusBar.playing.get() ? "Live" : "Preview",
                              route, static_cast<unsigned>(channel + 1U), feedbackText);
            } else {
                std::snprintf(meta_.data(), meta_.size(), "%s · %s Ch%u",
                              state_.statusBar.playing.get() ? "Live" : "Preview",
                              route, static_cast<unsigned>(channel + 1U));
            }
        }
    } else {
        const auto& draft = ui.draft;
        std::snprintf(title_.data(), title_.size(), "%s",
                      ui.mode == Mode::ADD_LANE_DRAFT ? "Add CC lane" : "CC lane settings");
        for (uint8_t fieldIndex = 0;
             fieldIndex < static_cast<uint8_t>(seq::SequencerCcLaneDraftField::COUNT);
             ++fieldIndex) {
            const auto field = static_cast<seq::SequencerCcLaneDraftField>(fieldIndex);
            char value[48] = {};
            switch (field) {
                case seq::SequencerCcLaneDraftField::CONTROLLER:
                    std::snprintf(value, sizeof(value), "CC%u",
                                  static_cast<unsigned>(draft.destination.controller));
                    break;
                case seq::SequencerCcLaneDraftField::ROUTE_POLICY:
                    std::snprintf(value, sizeof(value), "%s",
                                  routePolicyLabel(draft.destination.routePolicy));
                    break;
                case seq::SequencerCcLaneDraftField::PINNED_CHANNEL:
                    if (draft.destination.routePolicy == seq::SequencerCcLaneRoutePolicy::PINNED) {
                        std::snprintf(value, sizeof(value), "Ch%u",
                                      static_cast<unsigned>(draft.destination.pinnedChannel + 1U));
                    } else {
                        std::snprintf(value, sizeof(value), "Track Ch%u",
                                      static_cast<unsigned>(state_.sequencer.pattern.midiChannel.get() + 1U));
                    }
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
                case seq::SequencerCcLaneDraftField::COUNT:
                    break;
            }
            addRow(fieldLabel(field), value,
                       field == seq::SequencerCcLaneDraftField::ROUTE_POLICY ||
                               field == seq::SequencerCcLaneDraftField::PINNED_CHANNEL
                           ? (draft.destination.routePolicy ==
                                      seq::SequencerCcLaneRoutePolicy::PINNED
                                  ? ::standalone::icons::ROUTE_PIN
                                  : ::standalone::icons::ROUTING)
                           : ::standalone::icons::MIDI_CC);
        }
        if (ui.mode == Mode::LANE_SETTINGS) {
            char authored[24] = {};
            char resolved[24] = {};
            std::snprintf(authored, sizeof(authored), "%s",
                          ui.hasAuthoredValue ? "Current step" : "--");
            if (ui.hasAuthoredValue) {
                std::snprintf(authored, sizeof(authored), "%u",
                              static_cast<unsigned>(ui.authoredValue));
            }
            std::snprintf(resolved, sizeof(resolved), "%s",
                          ui.hasResolvedValue ? "Resolved" : "--");
            if (ui.hasResolvedValue) {
                std::snprintf(resolved, sizeof(resolved), "%u",
                              static_cast<unsigned>(ui.resolvedValue));
            }
            addRow("Authored", authored, ::standalone::icons::MIDI_CC);
            addRow("Resolved", resolved, ::standalone::icons::ROUTING);
            if (ui.macroConflict) {
                addRow("Winner", "CC lane · Macro loses",
                       ::standalone::icons::STATUS_CONFLICT);
            }
        }
        selectedIndex = static_cast<int>(ui.focusedField);
        if (ui.laneConflict) {
            std::snprintf(meta_.data(), meta_.size(), "Preview · Lane duplicate · Blocked");
        } else if (ui.macroConflict) {
            std::snprintf(meta_.data(), meta_.size(), "Preview · Macro conflict · Hold");
        } else if (!ui.routeValid) {
            std::snprintf(meta_.data(), meta_.size(), "Preview · No route · Silent");
        } else if (feedbackText[0]) {
            std::snprintf(meta_.data(), meta_.size(), "Preview · Route OK · %s", feedbackText);
        } else {
            std::snprintf(meta_.data(), meta_.size(), "Preview · Route OK");
        }
    }

    overlay_.render({
        .title = title_.data(),
        .meta = meta_.data(),
        .rows = rows.data(),
        .rowCount = rowCount,
        .selectedIndex = std::clamp(selectedIndex, 0, std::max(0, rowCount - 1)),
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
}

}  // namespace core::context::standalone
