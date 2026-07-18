#include "handler/macro/MacroPerformanceDomainServices.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "midi/MidiUtils.hpp"
#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::handler {

namespace {

bool setTrackConfigsImpl(
    MacroPerformanceDomainServices::StateRefs state,
    MacroPerformanceDomainServices::Operations operations,
    const std::array<core::state::macro::MacroConfig, core::state::macro::MACRO_COUNT>& configs
) {
    const uint8_t targetChannel = configs[0].channel;
    for (uint8_t i = 1; i < core::state::macro::MACRO_COUNT; ++i) {
        if (configs[i].channel != targetChannel) {
            return false;
        }
    }

    const bool channelChanged = state.pages.activeTrackChannel() != targetChannel;
    bool anyCcChanged = false;

    auto& page = state.pages.activePageData();
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        if (page.cc[i] == configs[i].cc) continue;
        page.cc[i] = configs[i].cc;
        anyCcChanged = true;
    }

    if (!channelChanged && !anyCcChanged) {
        return false;
    }

    if (channelChanged) {
        state.pages.setActiveTrackChannel(targetChannel);
    } else {
        state.pages.updateActiveConfigs();
    }

    state.configRevision.set(
        core::state::macro::nextMacroConfigRevision(state.configRevision.get())
    );
    if (operations.markProjectMutated != nullptr) {
        operations.markProjectMutated(operations.context);
    }
    return true;
}

void markProjectMutatedFromCoreState(void* context) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    state->markProjectMutated();
}

void markMacroValueEditedFromCoreState(void* context, uint8_t index) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    state->markMacroValueEdited(index);
}

bool setConfigFromCoreState(void* context, uint8_t index, uint8_t channel, uint8_t cc) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr &&
           core::state::macro::MacroWorkflow::setConfig(*state, index, channel, cc);
}

bool setTrackChannelFromCoreState(void* context, uint8_t channel) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr && core::state::macro::MacroWorkflow::setTrackChannel(*state, channel);
}

void switchToPageFromCoreState(void* context, uint8_t pageIndex) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    core::state::macro::MacroWorkflow::switchToPage(*state, pageIndex);
}

void bumpAutomationRecordingRevision(core::state::macro::MacroUiState& macroUi) {
    macroUi.automationRecordingRevision.set(macroUi.automationRecordingRevision.get() + 1U);
}

int16_t packTakeMidi7(uint8_t value) {
    return static_cast<int16_t>(
        (static_cast<uint32_t>(std::min<uint8_t>(value, 127U)) * 32767U + 63U) /
        127U
    );
}

core::state::macro::MacroAutomationCurveRef takeCurveRef(
    uint16_t pointCount,
    uint16_t durationTicks,
    uint16_t windowOffsetTicks
) {
    return {
        .active = pointCount > 0U,
        .playbackState = core::state::macro::MacroCurvePlaybackState::ACTIVE,
        .pointOffset = 0U,
        .pointCount = pointCount,
        .sourceDurationTicks = std::max<uint16_t>(durationTicks, 1U),
        .durationTicks = std::max<uint16_t>(durationTicks, 1U),
        .windowOffsetTicks = windowOffsetTicks,
        .interpolation =
            core::state::macro::MacroAutomationInterpolation::LINEAR,
        .modulationOrigin = core::state::macro::MacroModulationOrigin::NATIVE,
    };
}

}  // namespace

MacroPerformanceDomainServices::MacroPerformanceDomainServices(
    StateRefs state,
    Operations operations
)
    : macros_(&state.macros)
    , pages_(&state.pages)
    , macro_ui_(&state.macroUi)
    , config_revision_(&state.configRevision)
    , status_bar_(&state.statusBar)
    , history_(state.history)
    , operations_(operations) {}

MacroPerformanceDomainServices MacroPerformanceDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return MacroPerformanceDomainServices{
        StateRefs{
            state.macros,
            state.pages,
            state.macroUi,
            state.configRevision,
            state.statusBar,
            &state.macroHistory,
        },
        Operations{
            .context = &state,
            .markProjectMutated = markProjectMutatedFromCoreState,
            .markMacroValueEdited = markMacroValueEditedFromCoreState,
            .setConfig = setConfigFromCoreState,
            .setTrackChannel = setTrackChannelFromCoreState,
            .switchToPage = switchToPageFromCoreState,
        },
    };
}

core::state::macro::MacroAutomationSlotAddress
MacroPerformanceDomainServices::activeAddress_(uint8_t index) const {
    return core::state::macro::MacroAutomationSlotAddress{
        .track = pages_->currentActiveTrack(),
        .page = pages_->currentActivePage(),
        .macro = index,
    };
}

void MacroPerformanceDomainServices::refreshManualProjection_() const {
    macro_ui_->refreshManualOverrideMask(
        pages_->currentActiveTrack(),
        pages_->currentActivePage()
    );
}

float MacroPerformanceDomainServices::runtimeValue(uint8_t index) const {
    return core::state::macro::MacroWorkflow::runtimeValue(*macros_, index);
}

float MacroPerformanceDomainServices::absoluteBaseValue(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return 0.0f;
    const auto& take = macro_ui_->automationTake;
    if (take.phase == core::state::macro::MacroAutomationTakePhase::RECORDING &&
        take.track == pages_->currentActiveTrack() &&
        take.page == pages_->currentActivePage() && take.activeFor(index)) {
        return take.latestBase(index);
    }
    float manualValue = 0.0f;
    if (manualOverrideValueFor(index, manualValue)) return manualValue;
    return core::state::macro::macroAutomationClamp01(
        pages_->activePageData().values[index]
    );
}

void MacroPerformanceDomainServices::setManualValue(uint8_t index, float value) const {
    if (index >= core::state::macro::MACRO_COUNT) return;
    core::state::macro::MacroWorkflow::setRuntimeValue(*macros_, index, value);
    const float manualValue = runtimeValue(index);
    auto& page = pages_->activePageData();
    if (page.values[index] == manualValue) return;
    page.values[index] = manualValue;
    if (operations_.markMacroValueEdited != nullptr) {
        operations_.markMacroValueEdited(operations_.context, index);
    }
}

void MacroPerformanceDomainServices::setResolvedValue(uint8_t index, float value) const {
    core::state::macro::MacroWorkflow::setRuntimeValue(*macros_, index, value);
}

void MacroPerformanceDomainServices::setResolvedValue(
    uint8_t index,
    const core::state::macro::MacroResolvedValue& value
) const {
    if (index >= core::state::macro::MACRO_COUNT) return;
    core::state::macro::MacroWorkflow::setRuntimeValue(*macros_, index, value.resolved);
    float depth = 0.0f;
    core::state::modulation::ProjectControlMacroSlotView slot{};
    if (core::state::modulation::readProjectControlMacroSlot(
            pages_->control,
            activeAddress_(index),
            slot
        ) && slot.modulationStored) {
        depth = core::state::macro::macroAutomationClamp01(
            slot.compatibility.modulationDepth
        );
    }
    const auto address = activeAddress_(index);
    macro_ui_->setRuntimeProjection(
        address.track,
        address.page,
        index,
        value,
        depth
    );
}

core::state::macro::MacroResolvedValue
MacroPerformanceDomainServices::resolveManualValue(uint8_t index, float value) const {
    core::state::macro::MacroResolvedValue out{};
    out.base = core::state::macro::macroAutomationClamp01(value);
    if (index >= core::state::macro::MACRO_COUNT) {
        out.resolved = out.base;
        return out;
    }
    core::state::modulation::ProjectControlMacroSlotView slot{};
    if (core::state::modulation::readProjectControlMacroSlot(
            pages_->control,
            activeAddress_(index),
            slot
        ) && slot.present) {
        out.automationStored = slot.automationStored;
        out.modulationStored = slot.modulationStored;
        out.modulationPausedDepthZero =
            slot.modulationEnabled && slot.compatibility.modulationDepth <= 0.0f;
        out.modulationActive =
            slot.modulationEnabled && slot.compatibility.modulationDepth > 0.0f;
    }
    const auto& projection = macro_ui_->runtimeProjections[index];
    if (out.modulationActive && projection.valid && projection.modulationActive) {
        out.modulation = projection.modulation;
    }
    out.resolved = core::state::macro::macroAutomationClamp01(
        out.base + out.modulation
    );
    return out;
}

FLASHMEM bool MacroPerformanceDomainServices::armAutomationTake() const {
    using namespace core::state::macro;
    if (macro_ui_->automationTake.phase != MacroAutomationTakePhase::IDLE ||
        history_ == nullptr) {
        return false;
    }
    const uint16_t candidates = pages_->activePageData().activeMacroMask;
    if (candidates == 0U) return false;
    std::array<uint8_t, MacroAutomationTakeState::VALUE_COLUMN_COUNT> bases{};
    for (uint8_t macro = 0U; macro < bases.size(); ++macro) {
        bases[macro] = core::midi::toCC(absoluteBaseValue(macro));
    }
    macro_ui_->automationTake.arm(
        macro_ui_->automationTakeTiming.get(),
        candidates,
        bases
    );
    macro_ui_->automationTake.track = pages_->currentActiveTrack();
    macro_ui_->automationTake.page = pages_->currentActivePage();
    macro_ui_->performanceOverlayMode.set(
        MacroPerformanceOverlayMode::AUTOMATION_TAKE
    );
    macro_ui_->automationRecordingStatus.set(
        MacroAutomationRecordingStatus::ARMED
    );
    bumpAutomationRecordingRevision(*macro_ui_);
    return true;
}

FLASHMEM bool MacroPerformanceDomainServices::setAutomationTakeTiming(
    core::state::macro::MacroAutomationTakeTiming timing
) const {
    using namespace core::state::macro;
    if (macro_ui_->automationTake.phase == MacroAutomationTakePhase::RECORDING) {
        return false;
    }
    if (static_cast<uint8_t>(timing) >=
        MACRO_AUTOMATION_TAKE_TIMING_COUNT) {
        return false;
    }
    const bool changed = macro_ui_->automationTakeTiming.get() != timing;
    macro_ui_->automationTakeTiming.set(timing);
    if (macro_ui_->automationTake.phase == MacroAutomationTakePhase::ARMED) {
        macro_ui_->automationTake.timing = timing;
        macro_ui_->automationTake.durationTicks =
            macroAutomationTakeFixedDurationTicks(timing);
    }
    if (changed) bumpAutomationRecordingRevision(*macro_ui_);
    return changed;
}

FLASHMEM bool MacroPerformanceDomainServices::navigateAutomationTakeTiming(
    int delta
) const {
    if (delta == 0) return false;
    return setAutomationTakeTiming(
        core::state::macro::nextMacroAutomationTakeTiming(
            macro_ui_->automationTakeTiming.get(),
            delta > 0 ? 1 : -1
        )
    );
}

FLASHMEM bool MacroPerformanceDomainServices::beginAutomationTake_(
    uint32_t nowMs
) const {
    using namespace core::state::macro;
    using namespace core::state::modulation;
    auto& take = macro_ui_->automationTake;
    if (take.phase != MacroAutomationTakePhase::ARMED || history_ == nullptr ||
        take.track != pages_->currentActiveTrack() ||
        take.page != pages_->currentActivePage() ||
        take.candidateMask != pages_->activePageData().activeMacroMask) {
        return false;
    }

    auto historyChange = history_->prepareAutomationTake(
        *pages_,
        take.track,
        take.page,
        take.candidateMask
    );
    auto staged = core::app::makeExtmemUnique<ProjectControlDomainState>();
    if (!historyChange || !historyChange->automationTake || !staged) {
        return false;
    }
    *staged = pages_->control.authored;

    // Prove the conservative eight-lane maximum against the exact live arena.
    constexpr uint16_t PREFLIGHT_DURATION_TICKS = 24576U;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((take.candidateMask & bit) == 0U) continue;
        auto& snapshot = historyChange->automationTake->after[macro];
        for (uint16_t point = 0U;
             point < MACRO_AUTOMATION_RECORDING_MAX_POINTS;
             ++point) {
            snapshot.points[point] = {
                static_cast<uint16_t>(
                    (static_cast<uint32_t>(point) * PREFLIGHT_DURATION_TICKS) /
                    (MACRO_AUTOMATION_RECORDING_MAX_POINTS - 1U)
                ),
                packTakeMidi7(take.initialValues[macro]),
            };
        }
        snapshot.pointCount = MACRO_AUTOMATION_RECORDING_MAX_POINTS;
        snapshot.automation = takeCurveRef(
            snapshot.pointCount,
            PREFLIGHT_DURATION_TICKS,
            0U
        );
        if (!replaceProjectControlAutomationInDomain(
                *staged,
                snapshot.address,
                snapshot.automation,
                snapshot.points.get(),
                snapshot.pointCount
            )) {
            return false;
        }
    }
    if (!validProjectModulationDomain(
            staged->modulation,
            staged->curves,
            &staged->automation
        )) {
        return false;
    }
    // Keep the proven allocation but restore the exact pre-take authored bytes.
    *staged = pages_->control.authored;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((take.candidateMask & bit) == 0U) continue;
        auto& snapshot = historyChange->automationTake->after[macro];
        snapshot.automation = {};
        snapshot.pointCount = 0U;
    }

    const auto time = extrapolateProjectControlTime(
        pages_->control.timeTelemetry,
        nowMs
    );
    uint32_t projectPhase = 0U;
    if (pages_->control.runtime.initialized && time.playing) {
        projectPhase =
            time.musicalTick - pages_->control.runtime.activationMusicalTick;
    }
    if (!take.begin(
            nowMs,
            time.musicalTick,
            projectPhase,
            time.transportGeneration,
            pages_->control.authoredRevision
        )) {
        return false;
    }
    macro_ui_->automationTakeHistory = std::move(historyChange);
    macro_ui_->automationTakeDomain = std::move(staged);
    macro_ui_->automationRecordingStatus.set(
        MacroAutomationRecordingStatus::RECORDING
    );
    bumpAutomationRecordingRevision(*macro_ui_);
    return true;
}

FLASHMEM uint32_t MacroPerformanceDomainServices::automationTakeElapsedTicks_(
    uint32_t nowMs
) const {
    using namespace core::state::macro;
    using namespace core::state::modulation;
    const auto& take = macro_ui_->automationTake;
    if (take.phase != MacroAutomationTakePhase::RECORDING) return 0U;
    const auto time = extrapolateProjectControlTime(
        pages_->control.timeTelemetry,
        nowMs
    );
    if (pages_->control.timeTelemetry.revision > 0U && time.playing &&
        time.transportGeneration == take.transportGeneration &&
        time.musicalTick >= take.startedMusicalTick) {
        return time.musicalTick - take.startedMusicalTick;
    }
    const uint32_t elapsedMs = nowMs - take.startedAtMs;
    const float tempo = status_bar_->tempo.get() > 0.0f
        ? status_bar_->tempo.get()
        : 120.0f;
    return static_cast<uint32_t>(std::min<double>(
        std::llround(
            static_cast<double>(elapsedMs) * tempo *
            MACRO_AUTOMATION_TICKS_PER_BEAT / 60000.0
        ),
        static_cast<double>(UINT16_MAX)
    ));
}

FLASHMEM bool MacroPerformanceDomainServices::recordAutomationTakeValue(
    uint8_t index,
    uint32_t nowMs,
    float value
) const {
    using namespace core::state::macro;
    auto& take = macro_ui_->automationTake;
    if (take.phase == MacroAutomationTakePhase::ARMED &&
        !beginAutomationTake_(nowMs)) {
        clearAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
        return false;
    }
    if (take.phase != MacroAutomationTakePhase::RECORDING ||
        take.track != pages_->currentActiveTrack() ||
        take.page != pages_->currentActivePage() || index >= MACRO_COUNT) {
        return false;
    }
    const uint16_t bit = static_cast<uint16_t>(1U << index);
    if ((take.touchedMask & bit) == 0U) {
        float previous = 0.0f;
        const auto address = activeAddress_(index);
        if (macro_ui_->manualOverrides.valueFor(address, previous)) {
            take.manualRestoreMask = static_cast<uint16_t>(
                take.manualRestoreMask | bit
            );
            take.previousManualValues[index] = previous;
        }
        (void)macro_ui_->manualOverrides.resume(address);
        refreshManualProjection_();
    }
    const bool recorded = take.touch(
        index,
        core::midi::toCC(macroAutomationClamp01(value)),
        automationTakeElapsedTicks_(nowMs)
    );
    if (take.reduced) {
        macro_ui_->automationRecordingStatus.set(
            MacroAutomationRecordingStatus::REDUCED
        );
    }
    if (recorded) bumpAutomationRecordingRevision(*macro_ui_);
    return recorded;
}

FLASHMEM bool MacroPerformanceDomainServices::updateAutomationTake(uint32_t nowMs) const {
    using namespace core::state::macro;
    auto& take = macro_ui_->automationTake;
    if (take.phase != MacroAutomationTakePhase::RECORDING) return false;
    const uint32_t elapsed = automationTakeElapsedTicks_(nowMs);
    if (!take.sample(elapsed)) return false;
    if (take.reduced) {
        macro_ui_->automationRecordingStatus.set(
            MacroAutomationRecordingStatus::REDUCED
        );
    }
    bumpAutomationRecordingRevision(*macro_ui_);
    if (take.completeAt(elapsed)) return commitAutomationTake_(nowMs);
    return true;
}

FLASHMEM bool MacroPerformanceDomainServices::commitAutomationTake_(
    uint32_t nowMs
) const {
    using namespace core::state::macro;
    using namespace core::state::modulation;
    auto& take = macro_ui_->automationTake;
    if (take.phase != MacroAutomationTakePhase::RECORDING ||
        !macro_ui_->automationTakeHistory ||
        !macro_ui_->automationTakeHistory->automationTake ||
        !macro_ui_->automationTakeDomain ||
        pages_->control.authoredRevision != take.authoredRevision ||
        !take.finish(automationTakeElapsedTicks_(nowMs))) {
        restoreAutomationTakeManual_();
        clearAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
        return false;
    }

    *macro_ui_->automationTakeDomain = pages_->control.authored;
    auto& payload = *macro_ui_->automationTakeHistory->automationTake;
    payload.touchedMask = take.touchedMask;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((take.touchedMask & bit) == 0U) continue;
        auto& snapshot = payload.after[macro];
        uint16_t written = 0U;
        if (!take.buildPackedCurve(
                macro,
                snapshot.points.get(),
                MACRO_AUTOMATION_RECORDING_MAX_POINTS,
                written
            )) {
            restoreAutomationTakeManual_();
            clearAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
            return false;
        }
        snapshot.pointCount = written;
        snapshot.automation = takeCurveRef(
            written,
            take.durationTicks,
            take.playbackWindowOffsetTicks()
        );
        if (!replaceProjectControlAutomationInDomain(
                *macro_ui_->automationTakeDomain,
                snapshot.address,
                snapshot.automation,
                snapshot.points.get(),
                snapshot.pointCount
            )) {
            restoreAutomationTakeManual_();
            clearAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
            return false;
        }
    }
    if (!validProjectModulationDomain(
            macro_ui_->automationTakeDomain->modulation,
            macro_ui_->automationTakeDomain->curves,
            &macro_ui_->automationTakeDomain->automation
        )) {
        restoreAutomationTakeManual_();
        clearAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
        return false;
    }

    pages_->control.authored = *macro_ui_->automationTakeDomain;
    pages_->control.markAuthoredMutation();
    if (!history_->commitPreparedAutomationTake(
            *pages_,
            macro_ui_->automationTakeHistory
        )) {
        // This path is reachable only if an internal invariant changed after
        // the detached-domain validation. Restore every exact preflight lane.
        for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
            const uint16_t bit = static_cast<uint16_t>(1U << macro);
            if ((payload.touchedMask & bit) == 0U) continue;
            (void)applyMacroAutomationHistorySnapshot(
                *pages_,
                payload.before[macro]
            );
        }
        restoreAutomationTakeManual_();
        clearAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
        return false;
    }
    refreshManualProjection_();
    clearAutomationTake_(MacroAutomationRecordingStatus::IDLE);
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

FLASHMEM bool MacroPerformanceDomainServices::releaseAutomationTake(
    uint32_t nowMs
) const {
    using namespace core::state::macro;
    const auto phase = macro_ui_->automationTake.phase;
    if (phase == MacroAutomationTakePhase::ARMED) {
        clearAutomationTake_(MacroAutomationRecordingStatus::IDLE);
        return true;
    }
    if (phase != MacroAutomationTakePhase::RECORDING) return false;
    if (macro_ui_->automationTake.fixedLength()) return true;
    return commitAutomationTake_(nowMs);
}

FLASHMEM void MacroPerformanceDomainServices::restoreAutomationTakeManual_() const {
    using namespace core::state::macro;
    auto& take = macro_ui_->automationTake;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((take.manualRestoreMask & bit) == 0U) continue;
        const MacroAutomationSlotAddress address{
            .track = take.track,
            .page = take.page,
            .macro = macro,
        };
        (void)macro_ui_->manualOverrides.activate(
            address,
            take.previousManualValues[macro]
        );
        if (macroAutomationAddressEquals(address, activeAddress_(macro))) {
            setResolvedValue(
                macro,
                resolveManualValue(macro, take.previousManualValues[macro])
            );
        }
    }
    refreshManualProjection_();
}

FLASHMEM void MacroPerformanceDomainServices::clearAutomationTake_(
    core::state::macro::MacroAutomationRecordingStatus status
) const {
    macro_ui_->automationTake.reset();
    macro_ui_->automationTakeHistory.reset();
    macro_ui_->automationTakeDomain.reset();
    macro_ui_->performanceOverlayMode.set(
        core::state::macro::MacroPerformanceOverlayMode::NONE
    );
    macro_ui_->automationRecordingStatus.set(status);
    bumpAutomationRecordingRevision(*macro_ui_);
}

FLASHMEM bool MacroPerformanceDomainServices::cancelAutomationTake() const {
    using namespace core::state::macro;
    if (macro_ui_->automationTake.phase == MacroAutomationTakePhase::IDLE) {
        return false;
    }
    if (macro_ui_->automationTake.phase == MacroAutomationTakePhase::RECORDING) {
        restoreAutomationTakeManual_();
    }
    clearAutomationTake_(MacroAutomationRecordingStatus::IDLE);
    return true;
}

FLASHMEM bool MacroPerformanceDomainServices::automationTakeArmed() const {
    return macro_ui_->automationTake.phase ==
        core::state::macro::MacroAutomationTakePhase::ARMED;
}

bool MacroPerformanceDomainServices::automationTakeRecording() const {
    return macro_ui_->automationTake.phase ==
        core::state::macro::MacroAutomationTakePhase::RECORDING;
}

bool MacroPerformanceDomainServices::automationTakeActiveFor(
    uint8_t index
) const {
    const auto& take = macro_ui_->automationTake;
    return take.phase == core::state::macro::MacroAutomationTakePhase::RECORDING &&
           take.track == pages_->currentActiveTrack() &&
           take.page == pages_->currentActivePage() && take.activeFor(index);
}

bool MacroPerformanceDomainServices::computedSourcePlaybackActiveFor(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    core::state::modulation::ProjectControlMacroSlotView slot{};
    return core::state::modulation::readProjectControlMacroSlot(
               pages_->control,
               activeAddress_(index),
               slot
           ) && (slot.automationEnabled || slot.modulationEnabled);
}

bool MacroPerformanceDomainServices::automationActiveFor(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    core::state::modulation::ProjectControlMacroSlotView slot{};
    return core::state::modulation::readProjectControlMacroSlot(
               pages_->control,
               activeAddress_(index),
               slot
           ) && slot.automationStored;
}

bool MacroPerformanceDomainServices::automationPlaybackActiveFor(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    core::state::modulation::ProjectControlMacroSlotView slot{};
    return core::state::modulation::readProjectControlMacroSlot(
               pages_->control,
               activeAddress_(index),
               slot
           ) && slot.automationEnabled &&
           !manualOverrideActiveFor(index);
}

bool MacroPerformanceDomainServices::manualOverrideActiveFor(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    return macro_ui_->manualOverrides.activeFor(activeAddress_(index));
}

bool MacroPerformanceDomainServices::manualOverrideValueFor(
    uint8_t index,
    float& outValue
) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    return macro_ui_->manualOverrides.valueFor(activeAddress_(index), outValue);
}

bool MacroPerformanceDomainServices::takeManualControl(uint8_t index, float value) const {
    if (index >= core::state::macro::MACRO_COUNT ||
        !pages_->isMacroSlotActive(index) ||
        !automationPlaybackActiveFor(index)) {
        return false;
    }
    const auto status = macro_ui_->manualOverrides.activate(activeAddress_(index), value);
    if (status == core::state::macro::MacroManualOverrideState::ActivateStatus::INVALID_ADDRESS ||
        status == core::state::macro::MacroManualOverrideState::ActivateStatus::CAPACITY_EXHAUSTED) {
        return false;
    }
    float sanitized = value;
    (void)manualOverrideValueFor(index, sanitized);
    // Commit the physical absolute value only after the address-scoped
    // Automation override is guaranteed to exist. Modulation remains live and
    // is resolved around this newly-authored Base below.
    setManualValue(index, sanitized);
    setResolvedValue(index, resolveManualValue(index, sanitized));
    refreshManualProjection_();
    return true;
}

bool MacroPerformanceDomainServices::resumeComputedSources(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    const auto address = activeAddress_(index);
    const bool resumed = macro_ui_->manualOverrides.resume(address);
    if (resumed) refreshManualProjection_();
    return resumed;
}

bool MacroPerformanceDomainServices::isMacroSlotActive(uint8_t index) const {
    return pages_ != nullptr && pages_->isMacroSlotActive(index);
}

bool MacroPerformanceDomainServices::isMacroAddSlot(uint8_t index) const {
    return pages_ != nullptr && pages_->isMacroAddSlot(index);
}

bool MacroPerformanceDomainServices::activateMacroSlot(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    if (pages_->activePageData().isMacroActive(index)) return true;
    if (!core::state::macro::MacroWorkflow::activateMacroSlot(*macros_, *pages_, index)) {
        return false;
    }

    if (config_revision_ != nullptr) {
        config_revision_->set(core::state::macro::nextMacroConfigRevision(
            config_revision_->get(),
            index
        ));
    }
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

const core::state::macro::MacroConfig& MacroPerformanceDomainServices::activeConfig(
    uint8_t index
) const {
    return core::state::macro::MacroWorkflow::activeConfig(*pages_, index);
}

bool MacroPerformanceDomainServices::setConfig(uint8_t index,
                                               uint8_t channel,
                                               uint8_t cc) const {
    return operations_.setConfig != nullptr &&
           operations_.setConfig(operations_.context, index, channel, cc);
}

bool MacroPerformanceDomainServices::setTrackConfigs(
    const std::array<core::state::macro::MacroConfig, core::state::macro::MACRO_COUNT>& configs
) const {
    return setTrackConfigsImpl(
        StateRefs{*macros_, *pages_, *macro_ui_, *config_revision_, *status_bar_},
        operations_,
        configs
    );
}

uint8_t MacroPerformanceDomainServices::activeTrackChannel() const {
    return pages_->activeTrackChannel();
}

bool MacroPerformanceDomainServices::setTrackChannel(uint8_t channel) const {
    return operations_.setTrackChannel != nullptr &&
           operations_.setTrackChannel(operations_.context, channel);
}

bool MacroPerformanceDomainServices::isActivePageEnabled() const {
    return pages_->isPageEnabled(pages_->currentActivePage());
}

void MacroPerformanceDomainServices::switchToPage(uint8_t pageIndex) const {
    if (operations_.switchToPage != nullptr) {
        operations_.switchToPage(operations_.context, pageIndex);
        refreshManualProjection_();
        for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
            float manualValue = 0.0f;
            if (manualOverrideValueFor(i, manualValue)) {
                setResolvedValue(i, manualValue);
            }
        }
    }
}

void MacroPerformanceDomainServices::pulseCcIn() const {
    status_bar_->pulseCcIn();
}

void MacroPerformanceDomainServices::pulseCcOut() const {
    status_bar_->pulseCcOut();
}

void MacroPerformanceDomainServices::pulseNoteIn() const {
    status_bar_->pulseNoteIn();
}

}  // namespace core::handler
