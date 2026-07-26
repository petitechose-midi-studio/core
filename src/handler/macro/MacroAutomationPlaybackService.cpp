#include "handler/macro/MacroAutomationPlaybackService.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/time/Time.hpp>
#include "handler/macro/MacroAutomationTiming.hpp"
#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "midi/MidiUtils.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationRuntimePlan.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"

namespace core::handler {

struct MacroAutomationPlaybackService::FramePublicationContext {
    MacroAutomationPlaybackService* owner = nullptr;
    core::state::modulation::ProjectControlTimeSnapshot time{};
    core::state::macro::MacroUiState::RuntimeProjectionFrameTransaction
        projection{};
    core::state::shared::MidiCcCandidate* candidates = nullptr;
    uint16_t capacity = 0;
    uint16_t count = 0;
    uint16_t evaluationCount = 0;
    uint16_t audibleTrackMask = 0;
    bool candidateOverflow = false;
    std::array<uint16_t, core::state::macro::TRACK_COUNT> computedMasks{};
};

FLASHMEM MacroAutomationPlaybackService::MacroAutomationPlaybackService(
    StateRefs state,
    MacroMidiCcRuntimeAdapter& midiRuntime
)
    : macros_(state.macros)
    , pages_(state.pages)
    , macro_ui_(state.macroUi)
    , project_tracks_(state.projectTracks)
    , runtime_owner_revision_(state.runtimeOwnerRevision)
    , midi_runtime_(midiRuntime) {
    reset();
}

void MacroAutomationPlaybackService::reset() {
    update_scheduled_ = false;
    next_due_ms_ = 0;
    consumed_runtime_owner_revision_ =
        runtime_owner_revision_ != nullptr ? runtime_owner_revision_->get() : 0U;
    consumed_project_track_revision_ = project_tracks_.revision.get();
    cached_track_ = 0xFF;
    cached_page_ = 0xFF;
    pages_.control.compiledRevision = 0;
    pages_.control.runtimeContextHash = 0;
    pages_.control.runtime = {};
    pages_.control.timeTelemetry = {};
    pages_.control.triggerScratch = {};
}

void MacroAutomationPlaybackService::syncActivePageRuntimeUi_(uint8_t track,
                                                               uint8_t page) {
    macro_ui_.refreshManualOverrideMask(track, page);
}

void MacroAutomationPlaybackService::consumeRuntimeOwnerActivation_() {
    if (runtime_owner_revision_ == nullptr) return;

    const uint32_t revision = runtime_owner_revision_->get();
    if (revision == consumed_runtime_owner_revision_) return;

    consumed_runtime_owner_revision_ = revision;
    cached_track_ = pages_.currentActiveTrack();
    cached_page_ = pages_.currentActivePage();
    pages_.control.compiledRevision = 0;
    pages_.control.runtimeContextHash = 0;
    pages_.control.runtime = {};
    pages_.control.timeTelemetry = {};
    syncActivePageRuntimeUi_(cached_track_, cached_page_);
}

core::state::modulation::ProjectModulationCompileContext
MacroAutomationPlaybackService::compileContext_() const {
    core::state::modulation::ProjectModulationCompileContext context{};
    context.enabledTrackMask = pages_.currentTrackEnabledMask();
    for (uint8_t track = 0; track < core::state::macro::TRACK_COUNT; ++track) {
        const auto& trackData = pages_.tracks[track];
        const uint8_t page = std::min<uint8_t>(
            trackData.activePage,
            core::state::macro::PAGE_COUNT - 1U
        );
        context.activePage[track] = page;
        const bool active =
            (context.enabledTrackMask & static_cast<uint16_t>(1U << track)) != 0U &&
            trackData.isPageEnabled(page);
        context.activeMacroMask[track] = active
            ? trackData.pages[page].activeMacroMask
            : 0U;
    }
    return context;
}

bool MacroAutomationPlaybackService::ensureProjectRuntime_(
    const core::state::modulation::ProjectControlTimeSnapshot& time
) {
    auto& control = pages_.control;
    const auto context = compileContext_();
    const uint32_t contextHash =
        core::state::modulation::projectModulationCompileContextHash(context);
    const bool needsCompile =
        control.compiledRevision != control.authoredRevision ||
        control.runtimeContextHash != contextHash;
    if (needsCompile) {
        OC_PERF_SCOPE(perfPlanCompile, "project-control.plan-compile");
        const auto compiled =
            core::state::modulation::compileProjectControlRuntimePlan(
                control.authored,
                context,
                control.plan
            );
        OC_PERF_UNITS(
            perfPlanCompile,
            control.plan.sourceCount,
            control.plan.bindingCount
        );
        if (!compiled.compiled()) return false;
    }
    if (needsCompile || !control.runtime.initialized ||
        control.runtime.sourceCount != control.plan.sourceCount ||
        control.runtime.bindingCount != control.plan.bindingCount) {
        OC_PERF_SCOPE(perfRuntimeSync, "project-control.runtime-sync");
        OC_PERF_UNITS(
            perfRuntimeSync,
            control.plan.sourceCount,
            control.plan.bindingCount
        );
        if (core::state::modulation::synchronizeProjectControlRuntimeState(
                control.runtime,
                control.plan,
                time
            ) != core::state::modulation::ProjectControlRuntimeStatus::OK) {
            return false;
        }
    }
    if (needsCompile) {
        control.compiledRevision = control.authoredRevision;
        control.runtimeContextHash = contextHash;
        syncActivePageRuntimeUi_(
            pages_.currentActiveTrack(),
            pages_.currentActivePage()
        );
    }
    return true;
}

bool MacroAutomationPlaybackService::provideBase_(
    void* context,
    uint16_t,
    const core::state::modulation::ModulationDestination& destination,
    core::state::modulation::ProjectLogicalMacroBaseInput& out
) {
    auto* frame = static_cast<FramePublicationContext*>(context);
    if (frame == nullptr || frame->owner == nullptr ||
        !core::state::modulation::modulationDestinationValid(destination)) {
        return false;
    }
    auto& owner = *frame->owner;
    const auto& page = owner.pages_.pageData(
        destination.track,
        destination.page
    );
    out = {};
    out.staticValue = core::state::macro::macroAutomationClamp01(
        page.values[destination.macro]
    );
    const core::state::macro::MacroAutomationSlotAddress address{
        destination.track,
        destination.page,
        destination.macro,
    };
    float manual = 0.0f;
    if (owner.macro_ui_.manualOverrides.valueFor(address, manual)) {
        out.manualOverride = true;
        out.manualValue = core::state::macro::macroAutomationClamp01(manual);
    }
    const auto& take = owner.macro_ui_.automationTake;
    if (take.phase == core::state::macro::MacroAutomationTakePhase::RECORDING &&
        take.track == address.track && take.page == address.page &&
        take.activeFor(address.macro)) {
        out.manualOverride = true;
        out.manualValue = take.latestBase(address.macro);
    }
    return true;
}

void MacroAutomationPlaybackService::stageVisibleProjection_(
    FramePublicationContext& context,
    const core::state::modulation::ProjectLogicalMacroRuntimeValue& value
) {
    const auto& destination = value.destination;
    if (destination.track != pages_.currentActiveTrack() ||
        destination.page != pages_.currentActivePage()) {
        return;
    }
    core::state::modulation::ProjectControlMacroDestinationView authored{};
    const core::state::macro::MacroAutomationSlotAddress address{
        destination.track,
        destination.page,
        destination.macro,
    };
    (void)core::state::modulation::readProjectControlMacroDestination(
        pages_.control,
        address,
        authored
    );
    const bool automationActive =
        (value.flags & core::state::modulation::
            PROJECT_LOGICAL_MACRO_FLAG_AUTOMATION_ACTIVE) != 0U;
    const bool modulationActive =
        (value.flags & core::state::modulation::
            PROJECT_LOGICAL_MACRO_FLAG_MODULATION_ACTIVE) != 0U;
    core::state::macro::MacroWorkflow::setRuntimeValue(
        macros_,
        destination.macro,
        value.value
    );
    macro_ui_.stageRuntimeProjection(
        context.projection,
        destination.macro,
        core::state::macro::MacroResolvedValue{
            .base = value.base,
            .modulation = value.modulation,
            .resolved = value.value,
            .automationStored = authored.automation.stored(),
            .modulationStored = authored.modulationCount > 0U,
            .automationActive = automationActive,
            .modulationActive = modulationActive,
            .modulationPausedDepthZero =
                authored.primaryModulation.enabled &&
                std::abs(authored.primaryModulation.amount) <= 0.000001f,
        },
        authored.primaryModulation.present()
            ? std::clamp(authored.primaryModulation.amount, 0.0f, 1.0f)
            : 0.0f
    );
}

void MacroAutomationPlaybackService::captureRuntimeDestination_(
    void* context,
    uint16_t,
    const core::state::modulation::ProjectLogicalMacroRuntimeValue& value
) {
    auto* frame = static_cast<FramePublicationContext*>(context);
    if (frame == nullptr || frame->owner == nullptr ||
        frame->candidates == nullptr) {
        return;
    }
    const auto& logical = value.destination;
    if (!core::state::modulation::modulationDestinationValid(logical)) return;
    auto& owner = *frame->owner;
    const auto& page = owner.pages_.tracks[logical.track].pages[logical.page];
    const bool live = (value.flags & core::state::modulation::
        PROJECT_LOGICAL_MACRO_FLAG_MANUAL_OVERRIDE) != 0U;
    const bool computed = (value.flags & static_cast<uint8_t>(
        core::state::modulation::PROJECT_LOGICAL_MACRO_FLAG_AUTOMATION_ACTIVE |
        core::state::modulation::PROJECT_LOGICAL_MACRO_FLAG_MODULATION_ACTIVE
    )) != 0U;
    frame->evaluationCount = static_cast<uint16_t>(frame->evaluationCount + 1U);
    frame->computedMasks[logical.track] = static_cast<uint16_t>(
        frame->computedMasks[logical.track] |
        static_cast<uint16_t>(1U << logical.macro)
    );
    owner.stageVisibleProjection_(*frame, value);

    const uint16_t trackBit = static_cast<uint16_t>(1U << logical.track);
    if ((frame->audibleTrackMask & trackBit) == 0U) {
        return;
    }
    const uint16_t required = static_cast<uint16_t>(live ? 2U : 1U);
    if (frame->count > frame->capacity ||
        required > static_cast<uint16_t>(frame->capacity - frame->count)) {
        frame->candidateOverflow = true;
        return;
    }
    const auto destination = core::state::shared::MidiCcDestination{
        .identity = {
            .port = MidiCcGlobalFrameCoordinator::OUTPUT_PORT,
            .channel = owner.project_tracks_.authored
                .midiChannels[logical.track],
            .controller = page.cc[logical.macro],
        },
        .routeValidity = core::state::shared::MidiCcRouteValidity::VALID,
    };
    const auto baseClass = computed
        ? core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED
        : core::state::shared::MidiCcCandidateClass::MACRO_STATIC;
    const uint16_t stableAddress = MacroMidiCcRuntimeAdapter::stableAddress(
        logical.track,
        logical.page,
        logical.macro
    );
    const float underlyingRaw = value.underlyingBase + value.modulation;
    frame->candidates[frame->count++] = {
        .destination = destination,
        .author = {
            .candidateClass = baseClass,
            .stableAddress = stableAddress,
        },
        .localValue = core::midi::toCC(std::clamp(
            underlyingRaw,
            0.0f,
            1.0f
        )),
    };
    if (live) {
        frame->candidates[frame->count++] = {
            .destination = destination,
            .author = {
                .candidateClass =
                    core::state::shared::MidiCcCandidateClass::LIVE_MANUAL,
                .stableAddress = stableAddress,
            },
            .localValue = core::midi::toCC(value.value),
        };
    }
}

bool MacroAutomationPlaybackService::appendStaticAuthors_(
    FramePublicationContext& frame
) const {
    const uint16_t enabledTracks = pages_.currentTrackEnabledMask();
    for (uint8_t trackIndex = 0;
         trackIndex < core::state::macro::TRACK_COUNT;
         ++trackIndex) {
        if ((enabledTracks & static_cast<uint16_t>(1U << trackIndex)) == 0U) {
            continue;
        }
        if ((frame.audibleTrackMask &
             static_cast<uint16_t>(1U << trackIndex)) == 0U) {
            continue;
        }
        const auto& track = pages_.tracks[trackIndex];
        const uint8_t pageIndex = track.activePage;
        if (pageIndex >= core::state::macro::PAGE_COUNT ||
            !track.isPageEnabled(pageIndex)) {
            continue;
        }
        const auto& page = track.pages[pageIndex];
        for (uint8_t macroIndex = 0;
             macroIndex < core::state::macro::MACRO_COUNT;
             ++macroIndex) {
            const uint16_t bit = static_cast<uint16_t>(1U << macroIndex);
            if (!page.isMacroActive(macroIndex) ||
                (frame.computedMasks[trackIndex] & bit) != 0U) {
                continue;
            }
            if (frame.count >= frame.capacity) return false;
            const core::state::macro::MacroAutomationSlotAddress address{
                trackIndex,
                pageIndex,
                macroIndex,
            };
            float value = page.values[macroIndex];
            bool live = macro_ui_.manualOverrides.valueFor(address, value);
            const auto& take = macro_ui_.automationTake;
            if (take.phase ==
                    core::state::macro::MacroAutomationTakePhase::RECORDING &&
                take.track == address.track && take.page == address.page &&
                take.activeFor(address.macro)) {
                live = true;
                value = take.latestBase(address.macro);
            }
            const auto destination = core::state::shared::MidiCcDestination{
                .identity = {
                    .port = MidiCcGlobalFrameCoordinator::OUTPUT_PORT,
                    .channel = project_tracks_.authored
                        .midiChannels[trackIndex],
                    .controller = page.cc[macroIndex],
                },
                .routeValidity =
                    core::state::shared::MidiCcRouteValidity::VALID,
            };
            const uint16_t stableAddress =
                MacroMidiCcRuntimeAdapter::stableAddress(
                    trackIndex,
                    pageIndex,
                    macroIndex
                );
            frame.candidates[frame.count++] = {
                .destination = destination,
                .author = {
                    .candidateClass =
                        core::state::shared::MidiCcCandidateClass::MACRO_STATIC,
                    .stableAddress = stableAddress,
                },
                .localValue = core::midi::toCC(page.values[macroIndex]),
            };
            if (live) {
                if (frame.count >= frame.capacity) return false;
                frame.candidates[frame.count++] = {
                    .destination = destination,
                    .author = {
                        .candidateClass = core::state::shared::
                            MidiCcCandidateClass::LIVE_MANUAL,
                        .stableAddress = stableAddress,
                    },
                    .localValue = core::midi::toCC(value),
                };
            }
        }
    }
    return true;
}

uint16_t MacroAutomationPlaybackService::activeAuthorCount_() const {
    uint16_t count = 0U;
    const uint16_t enabledTracks = pages_.currentTrackEnabledMask();
    for (uint8_t trackIndex = 0U;
         trackIndex < core::state::macro::TRACK_COUNT;
         ++trackIndex) {
        if ((enabledTracks & static_cast<uint16_t>(1U << trackIndex)) == 0U) {
            continue;
        }
        const auto& track = pages_.tracks[trackIndex];
        const uint8_t pageIndex = track.activePage;
        if (pageIndex >= core::state::macro::PAGE_COUNT ||
            !track.isPageEnabled(pageIndex)) {
            continue;
        }
        uint8_t mask = track.pages[pageIndex].activeMacroMask;
        while (mask != 0U) {
            count = static_cast<uint16_t>(count + (mask & 1U));
            mask = static_cast<uint8_t>(mask >> 1U);
        }
    }
    return count;
}

bool MacroAutomationPlaybackService::produceProjectFrame_(
    void* context,
    core::state::shared::MidiCcCandidate* destination,
    uint16_t capacity,
    uint16_t& written
) {
    auto* frame = static_cast<FramePublicationContext*>(context);
    if (frame == nullptr || frame->owner == nullptr || destination == nullptr) {
        return false;
    }
    frame->candidates = destination;
    frame->capacity = capacity;
    frame->count = 0;
    frame->evaluationCount = 0;
    auto& owner = *frame->owner;
    frame->audibleTrackMask = core::state::project::audibleMask(
        owner.project_tracks_,
        owner.pages_.currentTrackEnabledMask()
    );
    frame->candidateOverflow = false;
    frame->computedMasks.fill(0);
    frame->projection = owner.macro_ui_.beginRuntimeProjectionFrame();
    auto& control = owner.pages_.control;
    const bool provisionalDestination =
        control.runtime.recordedShapeAudition.mode ==
        core::state::modulation::
            ProjectRecordedShapeRuntimeAuditionMode::DESTINATION_ADD;
    if (control.plan.destinationCount > 0U || provisionalDestination) {
        OC_PERF_SCOPE(perfEvaluate, "project-control.evaluate");
        OC_PERF_UNITS(
            perfEvaluate,
            control.plan.sourceCount,
            control.plan.destinationCount
        );
        const auto evaluated =
            core::state::modulation::evaluateProjectControlRuntimeWithBaseProvider(
                control.plan,
                control.authored.curves,
                frame->time,
                &control.triggerScratch,
                provideBase_,
                frame,
                control.runtime,
                control.sourceScratch.data(),
                static_cast<uint16_t>(control.sourceScratch.size()),
                captureRuntimeDestination_,
                frame
        );
        if (!evaluated.evaluated() ||
            frame->evaluationCount != evaluated.destinationEvaluationCount ||
            frame->candidateOverflow) {
            owner.macro_ui_.cancelRuntimeProjectionFrame(frame->projection);
            return false;
        }
    }
    if (!owner.appendStaticAuthors_(*frame)) {
        owner.macro_ui_.cancelRuntimeProjectionFrame(frame->projection);
        return false;
    }
    owner.macro_ui_.commitRuntimeProjectionFrame(
        frame->projection,
        owner.pages_.currentActiveTrack(),
        owner.pages_.currentActivePage()
    );
    written = frame->count;
    return true;
}

void MacroAutomationPlaybackService::update(uint32_t nowMs) {
    const bool ownerActivationPending = runtime_owner_revision_ != nullptr &&
        runtime_owner_revision_->get() != consumed_runtime_owner_revision_;
    const bool addressContextChanged =
        pages_.currentActiveTrack() != cached_track_ ||
        pages_.currentActivePage() != cached_page_;
    const bool triggerEventsPending =
        midi_runtime_.projectModulationTriggersPending();
    const bool projectTrackChangePending =
        project_tracks_.revision.get() != consumed_project_track_revision_;
    if (update_scheduled_ &&
        !oc::time::deadlineReachedMs(nowMs, next_due_ms_) &&
        !ownerActivationPending && !addressContextChanged &&
        !triggerEventsPending && !projectTrackChangePending) {
        return;
    }
    update_scheduled_ = true;
    // A failure remains bounded at the safe fallback cadence. A successful
    // frame below tightens this deadline from the actual compiled workload.
    next_due_ms_ = nowMs + macro::MACRO_AUTOMATION_UPDATE_PERIOD_MS;
    OC_PERF_SCOPE(perfUpdate, "macro.automation-playback");

    consumeRuntimeOwnerActivation_();
    consumed_project_track_revision_ = project_tracks_.revision.get();
    const uint8_t track = pages_.currentActiveTrack();
    const uint8_t page = pages_.currentActivePage();
    if (track != cached_track_ || page != cached_page_) {
        cached_track_ = track;
        cached_page_ = page;
        syncActivePageRuntimeUi_(track, page);
    }

    const auto time = midi_runtime_.projectControlTimeSnapshot();
    core::state::modulation::publishProjectControlTimeTelemetry(
        pages_.control.timeTelemetry,
        time
    );
    if (!ensureProjectRuntime_(time)) return;

    auto& control = pages_.control;
    control.triggerScratch.count = 0U;
    control.triggerScratch.droppedEventCount = 0U;
    (void)midi_runtime_.drainProjectModulationTriggers(
        control.triggerScratch
    );

    FramePublicationContext publication{
        .owner = this,
        .time = time,
    };
    const bool published = midi_runtime_.publishProjectFrame(
        produceProjectFrame_,
        &publication
    );
    if (published) {
        next_due_ms_ = nowMs +
            macro::projectControlUpdatePeriodMilliseconds(
                control.plan,
                control.authored.curves,
                control.timeTelemetry,
                activeAuthorCount_()
            );
    }
    control.triggerScratch.count = 0U;
}

}  // namespace core::handler
