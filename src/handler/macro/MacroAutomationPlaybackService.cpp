#include "handler/macro/MacroAutomationPlaybackService.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/time/Time.hpp>
#include "handler/macro/MacroAutomationTiming.hpp"
#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "midi/MidiUtils.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationRuntimePlan.hpp"

namespace core::handler {

struct MacroAutomationPlaybackService::FramePublicationContext {
    MacroAutomationPlaybackService* owner = nullptr;
    core::state::modulation::ProjectControlTimeSnapshot time{};
    core::state::macro::MacroUiState::RuntimeProjectionFrameTransaction
        projection{};
    core::state::shared::MidiCcCandidate* candidates = nullptr;
    uint16_t capacity = 0;
    uint16_t count = 0;
    std::array<uint16_t, core::state::macro::TRACK_COUNT> computedMasks{};
};

FLASHMEM MacroAutomationPlaybackService::MacroAutomationPlaybackService(
    StateRefs state,
    MacroPerformanceDomainServices services,
    MacroMidiCcRuntimeAdapter& midiRuntime
)
    : pages_(state.pages)
    , macro_ui_(state.macroUi)
    , runtime_owner_revision_(state.runtimeOwnerRevision)
    , services_(services)
    , midi_runtime_(midiRuntime) {
    reset();
}

void MacroAutomationPlaybackService::reset() {
    update_scheduled_ = false;
    next_due_ms_ = 0;
    consumed_runtime_owner_revision_ =
        runtime_owner_revision_ != nullptr ? runtime_owner_revision_->get() : 0U;
    cached_track_ = 0xFF;
    cached_page_ = 0xFF;
    pages_.control.compiledRevision = 0;
    pages_.control.runtimeContextHash = 0;
    pages_.control.runtime = {};
    pages_.control.triggerScratch = {};
    invalidateComputedRuntime_();
}

void MacroAutomationPlaybackService::invalidateComputedRuntime_() {
    midi_runtime_.clearComputedValues();
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
    syncActivePageRuntimeUi_(cached_track_, cached_page_);
    invalidateComputedRuntime_();
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
        const auto compiled =
            core::state::modulation::compileProjectControlRuntimePlan(
                control.authored,
                context,
                control.plan
            );
        if (!compiled.compiled()) return false;
    }
    if (needsCompile || !control.runtime.initialized ||
        control.runtime.sourceCount != control.plan.sourceCount ||
        control.runtime.bindingCount != control.plan.bindingCount) {
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
        invalidateComputedRuntime_();
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
    const auto& recording = owner.macro_ui_.automationRecording;
    if (recording.active && recording.lane.pointCount > 0U &&
        core::state::macro::macroAutomationAddressEquals(
            recording.address,
            address
        )) {
        out.manualOverride = true;
        out.manualValue = core::state::macro::macroAutomationClamp01(
            recording.lane.points[recording.lane.pointCount - 1U].value
        );
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
    core::state::modulation::ProjectControlMacroSlotView authored{};
    const core::state::macro::MacroAutomationSlotAddress address{
        destination.track,
        destination.page,
        destination.macro,
    };
    (void)core::state::modulation::readProjectControlMacroSlot(
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
    services_.setResolvedValue(destination.macro, value.value);
    macro_ui_.stageRuntimeProjection(
        context.projection,
        destination.macro,
        core::state::macro::MacroResolvedValue{
            .base = value.base,
            .modulation = value.modulation,
            .resolved = value.value,
            .automationStored = authored.automationStored,
            .modulationStored = authored.modulationStored,
            .automationActive = automationActive,
            .modulationActive = modulationActive,
            .modulationPausedDepthZero = authored.modulationEnabled &&
                std::abs(authored.compatibility.modulationDepth) <= 0.000001f,
            .modulationSuspended = false,
        },
        authored.modulationStored
            ? core::state::macro::macroAutomationClamp01(
                  authored.compatibility.modulationDepth
              )
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
        frame->candidates == nullptr || frame->count >= frame->capacity) {
        return;
    }
    const auto& logical = value.destination;
    if (!core::state::modulation::modulationDestinationValid(logical)) return;
    auto& owner = *frame->owner;
    const auto& track = owner.pages_.tracks[logical.track];
    const auto& page = track.pages[logical.page];
    const bool live = (value.flags & core::state::modulation::
        PROJECT_LOGICAL_MACRO_FLAG_MANUAL_OVERRIDE) != 0U;
    const bool computed = (value.flags & static_cast<uint8_t>(
        core::state::modulation::PROJECT_LOGICAL_MACRO_FLAG_AUTOMATION_ACTIVE |
        core::state::modulation::PROJECT_LOGICAL_MACRO_FLAG_MODULATION_ACTIVE
    )) != 0U;
    frame->candidates[frame->count++] = {
        .destination = {
            .identity = {
                .port = MidiCcGlobalFrameCoordinator::OUTPUT_PORT,
                .channel = track.channel,
                .controller = page.cc[logical.macro],
            },
            .routeValidity = core::state::shared::MidiCcRouteValidity::VALID,
        },
        .author = {
            .candidateClass = live
                ? core::state::shared::MidiCcCandidateClass::LIVE_MANUAL
                : (computed
                    ? core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED
                    : core::state::shared::MidiCcCandidateClass::MACRO_STATIC),
            .stableAddress = MacroMidiCcRuntimeAdapter::stableAddress(
                logical.track,
                logical.page,
                logical.macro
            ),
        },
        .localValue = core::midi::toCC(value.value),
    };
    frame->computedMasks[logical.track] = static_cast<uint16_t>(
        frame->computedMasks[logical.track] |
        static_cast<uint16_t>(1U << logical.macro)
    );
    owner.stageVisibleProjection_(*frame, value);
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
            const auto& recording = macro_ui_.automationRecording;
            if (recording.active && recording.lane.pointCount > 0U &&
                core::state::macro::macroAutomationAddressEquals(
                    recording.address,
                    address
                )) {
                live = true;
                value = recording.lane.points[
                    recording.lane.pointCount - 1U
                ].value;
            }
            frame.candidates[frame.count++] = {
                .destination = {
                    .identity = {
                        .port = MidiCcGlobalFrameCoordinator::OUTPUT_PORT,
                        .channel = track.channel,
                        .controller = page.cc[macroIndex],
                    },
                    .routeValidity =
                        core::state::shared::MidiCcRouteValidity::VALID,
                },
                .author = {
                    .candidateClass = live
                        ? core::state::shared::MidiCcCandidateClass::LIVE_MANUAL
                        : core::state::shared::MidiCcCandidateClass::MACRO_STATIC,
                    .stableAddress = MacroMidiCcRuntimeAdapter::stableAddress(
                        trackIndex,
                        pageIndex,
                        macroIndex
                    ),
                },
                .localValue = core::midi::toCC(value),
            };
        }
    }
    return true;
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
    frame->computedMasks.fill(0);
    auto& owner = *frame->owner;
    frame->projection = owner.macro_ui_.beginRuntimeProjectionFrame();
    auto& control = owner.pages_.control;
    if (control.plan.destinationCount > 0U) {
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
            frame->count != evaluated.destinationEvaluationCount) {
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
    if (update_scheduled_ &&
        !oc::time::deadlineReachedMs(nowMs, next_due_ms_) &&
        !ownerActivationPending && !addressContextChanged &&
        !triggerEventsPending) {
        return;
    }
    update_scheduled_ = true;
    next_due_ms_ = nowMs + macro::MACRO_AUTOMATION_UPDATE_PERIOD_MS;
    OC_PERF_SCOPE(perfUpdate, "macro.automation-playback");

    consumeRuntimeOwnerActivation_();
    const uint8_t track = pages_.currentActiveTrack();
    const uint8_t page = pages_.currentActivePage();
    if (track != cached_track_ || page != cached_page_) {
        cached_track_ = track;
        cached_page_ = page;
        syncActivePageRuntimeUi_(track, page);
    }

    const auto time = midi_runtime_.projectControlTimeSnapshot();
    if (!ensureProjectRuntime_(time)) return;

    auto& control = pages_.control;
    control.triggerScratch.count = 0U;
    control.triggerScratch.reserved = 0U;
    (void)midi_runtime_.drainProjectModulationTriggers(
        control.triggerScratch
    );

    FramePublicationContext publication{
        .owner = this,
        .time = time,
    };
    (void)midi_runtime_.publishProjectFrame(
        produceProjectFrame_,
        &publication
    );
    control.triggerScratch.count = 0U;
}

}  // namespace core::handler
