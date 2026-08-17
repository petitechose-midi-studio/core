#include "sequencer/DrumPlaybackEngine.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/note/clock/ClockConstants.hpp>
#include <oc/note/sequencer/StepSequencerExpander.hpp>

namespace core::sequencer {

namespace drum = core::state::sequencer;
using oc::note::sequencer::SequencerEvent;
using oc::note::sequencer::SequencerEventType;

namespace {

void incrementSaturating(uint32_t& value) {
    if (value != UINT32_MAX) ++value;
}

uint8_t clampChannel(uint8_t channel) {
    return channel > 15U ? 15U : channel;
}

bool changesPlaybackTopology(
    const drum::DrumPatternRuntimeSnapshot* previous,
    const drum::DrumPatternRuntimeSnapshot* next
) {
    if (previous == next) return false;
    if (previous == nullptr || next == nullptr ||
        previous->laneCount != next->laneCount) {
        return true;
    }

    const uint8_t laneCount = std::min<uint8_t>(
        next->laneCount,
        drum::DRUM_MAX_LANES
    );
    for (uint8_t lane = 0U; lane < laneCount; ++lane) {
        const auto& before = previous->lanes[lane];
        const auto& after = next->lanes[lane];
        if (before.midiNote != after.midiNote ||
            before.length != after.length ||
            before.stepsPerBeat != after.stepsPerBeat) {
            return true;
        }
    }
    return false;
}

}  // namespace

FLASHMEM void DrumPlaybackTelemetry::reset() {
    diagnostics.reset();
    laneSteps.fill(0U);
    lanePhaseQ8.fill(0U);
    laneTicksPerStep.fill(1U);
    laneDecisionSteps.fill(0U);
    laneValidMask = 0U;
    laneDecisionValidMask = 0U;
    laneDecisionPlayedMask = 0U;
    transportTick = 0U;
    tickAnchorUs = 0U;
    tickPeriodUs = 0U;
    playing = false;
}

FLASHMEM DrumResolvedPageSignature
DrumPlaybackEngine::captureResolvedPageSignature(
    uint8_t page,
    uint8_t laneWindowStart
) const {
    DrumResolvedPageSignature signature{};
    signature.pattern = pattern_;
    signature.graph = graph_;
    signature.patternRevision = pattern_ ? pattern_->revision : 0U;
    signature.runSeed = run_seed_;
    signature.page = page;
    signature.laneWindowStart = laneWindowStart;
    signature.playing = playing_ && pattern_ != nullptr;
    if (!signature.playing) return signature;

    const uint8_t laneCount = std::min<uint8_t>(
        pattern_->laneCount,
        drum::DRUM_MAX_LANES
    );
    for (uint8_t row = 0U;
         row < drum::DrumResolvedPageProjection::VISIBLE_LANES;
         ++row) {
        const uint8_t lane = static_cast<uint8_t>(laneWindowStart + row);
        if (lane >= laneCount) break;
        const auto& source = pattern_->lanes[lane];
        const uint8_t length = std::max<uint8_t>(1U, source.length);
        const uint8_t ticksPerStep = ticksPerStep_(source.stepsPerBeat);
        const uint32_t ordinal = telemetry_.transportTick / ticksPerStep;
        signature.laneCycleIndices[row] = ordinal / length;
    }
    return signature;
}

FLASHMEM void DrumPlaybackEngine::buildResolvedPageProjection(
    const DrumResolvedPageSignature& signature,
    drum::DrumResolvedPageProjection& out
) const {
    out.reset();
    out.contextKey = static_cast<uint16_t>(
        (static_cast<uint16_t>(signature.page) << 8U) |
        signature.laneWindowStart
    );
    const auto* pattern = signature.pattern;
    const auto* graph = signature.graph;
    if (pattern == nullptr || graph == nullptr || !graph->enabled) {
        return;
    }

    const uint8_t laneCount = std::min<uint8_t>(
        pattern->laneCount,
        drum::DRUM_MAX_LANES
    );
    const uint16_t pageStart = static_cast<uint16_t>(signature.page) *
        drum::DrumResolvedPageProjection::STEPS_PER_PAGE;
    for (uint8_t row = 0U;
         row < drum::DrumResolvedPageProjection::VISIBLE_LANES;
         ++row) {
        const uint8_t lane = static_cast<uint8_t>(
            signature.laneWindowStart + row
        );
        if (lane >= laneCount) break;
        const auto& source = pattern->lanes[lane];
        const uint8_t length = std::max<uint8_t>(1U, source.length);
        const uint8_t ticksPerStep = ticksPerStep_(source.stepsPerBeat);

        for (uint8_t column = 0U;
             column < drum::DrumResolvedPageProjection::STEPS_PER_PAGE;
             ++column) {
            const uint16_t absoluteStep = pageStart + column;
            if (absoluteStep >= length || absoluteStep >= drum::DRUM_MAX_STEPS) {
                continue;
            }
            const uint8_t step = static_cast<uint8_t>(absoluteStep);
            const int16_t advancedSlot = pattern->advancedRootSlot(lane, step);
            if (advancedSlot < 0 ||
                advancedSlot >= drum::DRUM_ADVANCED_ROOT_SLOT_COUNT) {
                continue;
            }

            const std::size_t cell =
                drum::DrumResolvedPageProjection::cellIndex(row, column);
            const uint64_t bit =
                drum::DrumResolvedPageProjection::cellBit(row, column);
            const auto* rootNode = graph->stepNode(
                static_cast<uint16_t>(advancedSlot)
            );
            const auto* micro = rootNode != nullptr &&
                    rootNode->has(
                        oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE
                    )
                ? graph->sequence(rootNode->childSequenceId)
                : nullptr;
            if (rootNode != nullptr &&
                rootNode->has(oc::note::sequencer::STEP_NODE_CYCLE_SET) &&
                graph->cycleSet(rootNode->cycleSetId) != nullptr) {
                out.cyclePresentMask |= bit;
            }
            if (micro != nullptr) {
                out.microLength[cell] = std::min<uint8_t>(
                    micro->length,
                    oc::note::sequencer::StepSequencerGraphLimits::
                        MAX_EXPANDED_NOTES_PER_ROOT_STEP
                );
                uint16_t authoredMask = 0U;
                for (uint8_t index = 0U;
                     index < out.microLength[cell];
                     ++index) {
                    const auto* child = graph->stepNode(
                        static_cast<uint16_t>(
                            micro->firstStepNode + index
                        )
                    );
                    const bool enabled = child != nullptr &&
                        (!child->has(
                             oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE
                         ) ||
                         child->has(
                             oc::note::sequencer::STEP_NODE_ENABLED_VALUE
                         ));
                    if (enabled) {
                        authoredMask = static_cast<uint16_t>(
                            authoredMask |
                            static_cast<uint16_t>(1U << index)
                        );
                    }
                }
                out.microMask[cell] = authoredMask;
            }
            if (!signature.playing) continue;

            out.validMask |= bit;
            out.velocity[cell] = source.velocity[step];
            out.gate[cell] = source.gate[step];
            out.nudge[cell] = source.nudge[step];
            if (!source.enabledMask.test(step) || source.gate[step] == 0U) {
                out.microMask[cell] = 0U;
                continue;
            }

            // This main-loop preview uses the exact graph traversal and cycle
            // seed as playback. Root Nudge is retained here for visual
            // comparison; the realtime scheduler applies it one level earlier
            // when choosing the root on-tick.
            const auto expansion =
                oc::note::sequencer::StepSequencerExpander::expandRootStep(
                    oc::note::sequencer::StepSequencerRootStepInput{
                        .enabled = true,
                        .values = {
                            .note = source.midiNote,
                            .velocity = source.velocity[step],
                            .gate = source.gate[step],
                            .nudge = source.nudge[step],
                        },
                        .probability = source.probability[step],
                        .variationRanges = {},
                        .scaleSettings = {},
                        .pitchFollowsScale = false,
                        .mode = oc::note::sequencer::
                            StepSequencerRootStepInput::Mode::RhythmOnly,
                    },
                    *graph,
                    static_cast<uint8_t>(advancedSlot),
                    signature.laneCycleIndices[row],
                    ticksPerStep,
                    signature.runSeed,
                    true
                );
            if (expansion.count == 0U) continue;

            const uint8_t microLength = out.microLength[cell];
            if (microLength > 0U) {
                const uint32_t microSpan = std::max<uint32_t>(
                    1U,
                    (static_cast<uint32_t>(ticksPerStep) *
                     source.gate[step]) / 100U
                );
                uint16_t microMask = 0U;
                for (uint8_t noteIndex = 0U;
                     noteIndex < expansion.count;
                     ++noteIndex) {
                    const uint8_t microIndex = std::min<uint8_t>(
                        static_cast<uint8_t>(microLength - 1U),
                        static_cast<uint8_t>(
                            (expansion.notes[noteIndex].localTick *
                             microLength) / microSpan
                        )
                    );
                    microMask = static_cast<uint16_t>(
                        microMask |
                        static_cast<uint16_t>(1U << microIndex)
                    );
                }
                out.microMask[cell] = microMask;
            }

            out.playedMask |= bit;
            const auto& resolved = expansion.notes[0].variation.resolved;
            out.velocity[cell] = resolved.velocity;
            out.gate[cell] = resolved.gate;
            out.nudge[cell] = resolved.nudge;
        }
    }
}

FLASHMEM DrumPlaybackEngine::DrumPlaybackEngine(
    oc::note::sequencer::ISequencerEventSink& eventSink
)
    : event_sink_(eventSink) {
    reset();
}

void DrumPlaybackEngine::setPattern(
    const drum::DrumPatternRuntimeSnapshot* pattern,
    const oc::note::sequencer::StepSequencerGraph* graph,
    uint8_t midiChannel
) {
    const uint8_t channel = clampChannel(midiChannel);
    const uint32_t revision = pattern != nullptr ? pattern->revision : 0U;
    if (playing_ &&
        (midi_channel_ != channel ||
         changesPlaybackTopology(pattern_, pattern))) {
        pattern_change_pending_ = true;
    }
    pattern_ = pattern;
    graph_ = graph;
    pattern_revision_ = revision;
    midi_channel_ = channel;
}

FLASHMEM void DrumPlaybackEngine::reset() {
    clearPendingNotes_();
    last_triggered_ordinals_.fill(UINT32_MAX);
    telemetry_.reset();
    last_tick_ = 0U;
    last_tick_valid_ = false;
    playing_ = false;
    pattern_change_pending_ = false;
}

void DrumPlaybackEngine::update(
    uint32_t tick,
    bool playing,
    uint32_t nowUs,
    uint32_t tickPeriodUs
) {
    if (!playing || pattern_ == nullptr || pattern_->laneCount == 0U) {
        if (playing_) stop_(tick);
        return;
    }

    const bool tickChanged = !last_tick_valid_ || tick != last_tick_;
    if (tickChanged) {
        telemetry_.transportTick = tick;
        telemetry_.tickAnchorUs = nowUs;
    }
    if (tickPeriodUs != 0U) telemetry_.tickPeriodUs = tickPeriodUs;

    if (pattern_change_pending_) {
        (void)emitAllNotesOff_(tick);
        clearPendingNotes_();
        last_triggered_ordinals_.fill(UINT32_MAX);
        pattern_change_pending_ = false;
        // Re-enter the new generation exactly at the current transport phase.
        // Returning here avoids falling through the generic discontinuity path
        // and emitting a second AllNotesOff for the same authored change.
        processTick_(tick);
        refreshLanePhases_(tick, nowUs, tickPeriodUs);
        return;
    }

    if (!playing_) {
        start_(tick);
        processTick_(tick);
        refreshLanePhases_(tick, nowUs, tickPeriodUs);
        return;
    }

    if (!last_tick_valid_ || tick < last_tick_ ||
        tick - last_tick_ > MAX_CATCH_UP_TICKS) {
        (void)emitAllNotesOff_(tick);
        clearPendingNotes_();
        last_triggered_ordinals_.fill(UINT32_MAX);
        processTick_(tick);
        refreshLanePhases_(tick, nowUs, tickPeriodUs);
        return;
    }

    if (tick == last_tick_) {
        processDue_(tick);
        refreshLanePhases_(tick, nowUs, tickPeriodUs);
        return;
    }

    for (uint32_t current = last_tick_ + 1U; current <= tick; ++current) {
        processTick_(current);
        if (current == UINT32_MAX) break;
    }
    refreshLanePhases_(tick, nowUs, tickPeriodUs);
}

FLASHMEM void DrumPlaybackEngine::start_(uint32_t tick) {
    clearPendingNotes_();
    last_triggered_ordinals_.fill(UINT32_MAX);
    ++run_seed_;
    if (run_seed_ == 0U) run_seed_ = 1U;
    playing_ = true;
    telemetry_.playing = true;
    telemetry_.laneValidMask = 0U;
    telemetry_.laneDecisionValidMask = 0U;
    telemetry_.laneDecisionPlayedMask = 0U;
    last_tick_ = tick;
    last_tick_valid_ = false;
    pattern_change_pending_ = false;
}

FLASHMEM void DrumPlaybackEngine::stop_(uint32_t tick) {
    (void)emitAllNotesOff_(tick);
    clearPendingNotes_();
    last_triggered_ordinals_.fill(UINT32_MAX);
    playing_ = false;
    telemetry_.playing = false;
    telemetry_.laneValidMask = 0U;
    telemetry_.laneDecisionValidMask = 0U;
    telemetry_.laneDecisionPlayedMask = 0U;
    last_tick_valid_ = false;
}

void DrumPlaybackEngine::processTick_(uint32_t tick) {
    const uint8_t laneCount = std::min<uint8_t>(
        pattern_->laneCount,
        drum::DRUM_MAX_LANES
    );
    telemetry_.laneValidMask = laneCount >= drum::DRUM_MAX_LANES
        ? UINT16_MAX
        : static_cast<uint16_t>((1U << laneCount) - 1U);
    for (uint8_t lane = 0U; lane < laneCount; ++lane) {
        if (!triggerDueLaneStep_(lane, tick)) break;
    }
    (void)processDue_(tick);
    last_tick_ = tick;
    last_tick_valid_ = true;
}

bool DrumPlaybackEngine::triggerDueLaneStep_(uint8_t lane, uint32_t tick) {
    const auto& source = pattern_->lanes[lane];
    const uint8_t length = std::max<uint8_t>(1U, source.length);
    const uint8_t ticksPerStep = ticksPerStep_(source.stepsPerBeat);
    const uint32_t baseOrdinal = tick / ticksPerStep;
    telemetry_.laneSteps[lane] = static_cast<uint8_t>(baseOrdinal % length);
    telemetry_.laneTicksPerStep[lane] = ticksPerStep;

    // A negative nudge can pull only the next ordinal into this base interval;
    // the current ordinal covers zero and positive nudges.
    for (uint8_t candidate = 0U; candidate < 2U; ++candidate) {
        const uint32_t ordinal = baseOrdinal + candidate;
        const uint8_t step = static_cast<uint8_t>(ordinal % length);
        const int32_t offset = nudgeTickOffset_(
            source.nudge[step],
            ticksPerStep
        );
        int64_t onTickSigned =
            static_cast<int64_t>(ordinal) * ticksPerStep + offset;
        if (onTickSigned < 0) onTickSigned = 0;
        const uint32_t onTick = static_cast<uint32_t>(onTickSigned);
        if (onTick != tick || last_triggered_ordinals_[lane] == ordinal) {
            continue;
        }
        last_triggered_ordinals_[lane] = ordinal;
        if (!scheduleLaneStep_(lane, ordinal, onTick)) return false;
    }
    return true;
}

bool DrumPlaybackEngine::scheduleLaneStep_(
    uint8_t lane,
    uint32_t ordinal,
    uint32_t onTick
) {
    const auto& source = pattern_->lanes[lane];
    const uint8_t length = std::max<uint8_t>(1U, source.length);
    const uint8_t step = static_cast<uint8_t>(ordinal % length);
    if (!source.enabledMask.test(step) || source.gate[step] == 0U) {
        clearLaneDecision_(lane);
        return true;
    }

    const uint8_t probability = std::min<uint8_t>(source.probability[step], 100U);
    const int16_t advancedSlot = pattern_->advancedRootSlot(lane, step);
    if (advancedSlot >= 0 && graph_ != nullptr && graph_->enabled) {
        const uint8_t ticksPerStep = ticksPerStep_(source.stepsPerBeat);
        const auto expansion =
            oc::note::sequencer::StepSequencerExpander::expandRootStep(
                oc::note::sequencer::StepSequencerRootStepInput{
                    .enabled = true,
                    .values = {
                        .note = source.midiNote,
                        .velocity = source.velocity[step],
                        .gate = source.gate[step],
                        // Root nudge already selected `onTick`. Child nudge
                        // offsets remain active inside the shared expander.
                        .nudge = 0,
                    },
                    .probability = probability,
                    .variationRanges = {},
                    .scaleSettings = {},
                    .pitchFollowsScale = false,
                    .mode = oc::note::sequencer::
                        StepSequencerRootStepInput::Mode::RhythmOnly,
                },
                *graph_,
                static_cast<uint8_t>(advancedSlot),
                ordinal / length,
                ticksPerStep,
                run_seed_,
                true
            );
        if (expansion.noteBudgetExceeded) {
            telemetry_.diagnostics.noteBudgetExceeded = true;
            incrementSaturating(
                telemetry_.diagnostics.noteBudgetExceededCount);
        }
        if (expansion.depthLimitReached) {
            telemetry_.diagnostics.depthLimitReached = true;
            incrementSaturating(
                telemetry_.diagnostics.depthLimitReachedCount);
        }
        if (probability < 100U) {
            publishLaneDecision_(lane, step, expansion.count != 0U);
        } else {
            clearLaneDecision_(lane);
        }
        for (uint8_t index = 0U; index < expansion.count; ++index) {
            const auto& note = expansion.notes[index];
            const auto& resolved = note.variation.resolved;
            const uint16_t spanTicks = std::max<uint16_t>(1U, note.spanTicks);
            const uint32_t baseTick = onTick + note.localTick;
            const int32_t nudgeOffset = nudgeTickOffset_(
                resolved.nudge,
                static_cast<uint8_t>(std::min<uint16_t>(255U, spanTicks))
            );
            int64_t noteOnSigned =
                static_cast<int64_t>(baseTick) + nudgeOffset;
            if (noteOnSigned < 0) noteOnSigned = 0;
            const uint32_t noteOn = static_cast<uint32_t>(noteOnSigned);
            uint32_t gateTicks =
                static_cast<uint32_t>(std::min<uint16_t>(
                    resolved.gate,
                    drum::DRUM_MAX_GATE_PERCENT
                )) * spanTicks / 100U;
            if (gateTicks == 0U) gateTicks = 1U;
            if (!scheduleNote_(
                    noteOn,
                    noteOn + gateTicks,
                    midi_channel_,
                    source.midiNote,
                    resolved.velocity
                )) {
                return false;
            }
        }
        return true;
    }

    if (probability == 0U) {
        publishLaneDecision_(lane, step, false);
        return true;
    }
    const uint32_t cycle = ordinal / length;
    if (probability < 100U &&
        probabilityHash_(run_seed_, lane, cycle, step) % 100U >= probability) {
        publishLaneDecision_(lane, step, false);
        return true;
    }
    if (probability < 100U) publishLaneDecision_(lane, step, true);
    else clearLaneDecision_(lane);

    const uint8_t ticksPerStep = ticksPerStep_(source.stepsPerBeat);
    uint32_t gateTicks =
        static_cast<uint32_t>(std::min<uint16_t>(
            source.gate[step],
            drum::DRUM_MAX_GATE_PERCENT
        )) * ticksPerStep / 100U;
    if (gateTicks == 0U) gateTicks = 1U;

    return scheduleNote_(
            onTick,
            onTick + gateTicks,
            midi_channel_,
            source.midiNote,
            source.velocity[step]
    );
}

FLASHMEM bool DrumPlaybackEngine::emitAllNotesOff_(uint32_t tick) {
    SequencerEvent event{};
    event.tick = tick;
    event.type = SequencerEventType::AllNotesOff;
    if (event_sink_.emitSequencerEvent(event)) return true;
    telemetry_.diagnostics.sinkRejectedEvent = true;
    incrementSaturating(telemetry_.diagnostics.sinkRejectedEventCount);
    return false;
}

bool DrumPlaybackEngine::processDue_(uint32_t tick) {
    if (scheduler_.processUntil(tick, event_sink_)) return true;
    telemetry_.diagnostics.sinkRejectedEvent = true;
    incrementSaturating(telemetry_.diagnostics.sinkRejectedEventCount);
    (void)emitAllNotesOff_(tick);
    clearPendingNotes_();
    return false;
}

bool DrumPlaybackEngine::scheduleNote_(
    uint32_t onTick,
    uint32_t offTick,
    uint8_t channel,
    uint8_t note,
    uint8_t velocity
) {
    if (!scheduler_.scheduleRetriggeringNote(
            onTick,
            offTick,
            channel,
            note,
            velocity
        )) {
        telemetry_.diagnostics.schedulerCapacityExceeded = true;
        incrementSaturating(
            telemetry_.diagnostics.schedulerCapacityExceededCount);
        (void)emitAllNotesOff_(onTick);
        clearPendingNotes_();
        return false;
    }
    return true;
}

FLASHMEM void DrumPlaybackEngine::clearPendingNotes_() {
    scheduler_.clear();
}

void DrumPlaybackEngine::refreshLanePhases_(
    uint32_t tick,
    uint32_t nowUs,
    uint32_t tickPeriodUs
) {
    telemetry_.transportTick = tick;
    if (tickPeriodUs != 0U) telemetry_.tickPeriodUs = tickPeriodUs;
    uint16_t subTickQ8 = 0U;
    const uint32_t period = telemetry_.tickPeriodUs;
    if (period != 0U && nowUs != 0U) {
        const uint32_t elapsed = nowUs - telemetry_.tickAnchorUs;
        subTickQ8 = static_cast<uint16_t>(std::min<uint32_t>(
            255U,
            static_cast<uint32_t>(
                (static_cast<uint64_t>(elapsed) * 256U) / period
            )
        ));
    }
    const uint8_t laneCount = pattern_ != nullptr
        ? std::min<uint8_t>(pattern_->laneCount, drum::DRUM_MAX_LANES)
        : 0U;
    for (uint8_t lane = 0U; lane < laneCount; ++lane) {
        const uint8_t ticksPerStep = std::max<uint8_t>(
            1U,
            telemetry_.laneTicksPerStep[lane]
        );
        const uint32_t phaseNumerator =
            (tick % ticksPerStep) * 256U + subTickQ8;
        telemetry_.lanePhaseQ8[lane] = static_cast<uint8_t>(
            std::min<uint32_t>(255U, phaseNumerator / ticksPerStep)
        );
    }
}

void DrumPlaybackEngine::publishLaneDecision_(
    uint8_t lane,
    uint8_t step,
    bool played
) {
    if (lane >= drum::DRUM_MAX_LANES) return;
    const uint16_t bit = static_cast<uint16_t>(1U << lane);
    telemetry_.laneDecisionSteps[lane] = step;
    telemetry_.laneDecisionValidMask = static_cast<uint16_t>(
        telemetry_.laneDecisionValidMask | bit
    );
    if (played) {
        telemetry_.laneDecisionPlayedMask = static_cast<uint16_t>(
            telemetry_.laneDecisionPlayedMask | bit
        );
    } else {
        telemetry_.laneDecisionPlayedMask = static_cast<uint16_t>(
            telemetry_.laneDecisionPlayedMask & static_cast<uint16_t>(~bit)
        );
    }
}

void DrumPlaybackEngine::clearLaneDecision_(uint8_t lane) {
    if (lane >= drum::DRUM_MAX_LANES) return;
    const uint16_t bit = static_cast<uint16_t>(1U << lane);
    telemetry_.laneDecisionValidMask = static_cast<uint16_t>(
        telemetry_.laneDecisionValidMask & static_cast<uint16_t>(~bit)
    );
    telemetry_.laneDecisionPlayedMask = static_cast<uint16_t>(
        telemetry_.laneDecisionPlayedMask & static_cast<uint16_t>(~bit)
    );
}

uint8_t DrumPlaybackEngine::ticksPerStep_(uint8_t stepsPerBeat) {
    uint8_t safe = stepsPerBeat;
    if (safe == 0U) safe = drum::DRUM_DEFAULT_STEPS_PER_BEAT;
    if (safe > oc::note::clock::PPQN) {
        safe = static_cast<uint8_t>(oc::note::clock::PPQN);
    }
    const uint8_t ticks = static_cast<uint8_t>(
        oc::note::clock::PPQN / safe
    );
    return ticks == 0U ? 1U : ticks;
}

int32_t DrumPlaybackEngine::nudgeTickOffset_(
    int8_t nudge,
    uint8_t ticksPerStep
) {
    const int32_t clamped = std::max<int32_t>(-50, std::min<int32_t>(50, nudge));
    const int32_t scaled = clamped * ticksPerStep;
    return scaled >= 0
        ? (scaled + 50) / 100
        : -(((-scaled) + 50) / 100);
}

FLASHMEM uint32_t DrumPlaybackEngine::probabilityHash_(
    uint32_t runSeed,
    uint8_t lane,
    uint32_t cycleIndex,
    uint8_t step
) {
    uint32_t value = runSeed * 747796405U;
    value ^= static_cast<uint32_t>(lane) * 2246822519U;
    value ^= cycleIndex * 2891336453U;
    value ^= static_cast<uint32_t>(step) * 277803737U;
    value ^= value >> 16U;
    value *= 2246822519U;
    value ^= value >> 13U;
    value *= 3266489917U;
    value ^= value >> 16U;
    return value;
}

}  // namespace core::sequencer
