#include "handler/macro/MacroPerformanceDomainServices.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "app/ExtmemAllocator.hpp"
#include "diagnostics/MemoryFootprintReporter.hpp"
#include "midi/MidiUtils.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::handler {

namespace {
void bumpAutomationRecordingRevision(
    core::state::macro::MacroUiState& macroUi,
    uint8_t dirtyIndex =
        core::state::macro::kMacroAutomationRecordingDirtyAll
) {
    macroUi.automationRecordingRevision.set(
        core::state::macro::nextMacroAutomationRecordingRevision(
            macroUi.automationRecordingRevision.get(),
            dirtyIndex
        )
    );
}

int16_t packTakeMidi7(uint8_t value) {
    return static_cast<int16_t>(
        (static_cast<uint32_t>(std::min<uint8_t>(value, 127U)) * 32767U + 63U) /
        127U
    );
}

core::state::modulation::ProjectControlCurvePayload takeCurvePayload(
    uint16_t pointCount,
    uint16_t durationTicks,
    uint16_t windowOffsetTicks
) {
    return {
        .spec = {
            .sourceDurationTicks = std::max<uint16_t>(durationTicks, 1U),
            .durationTicks = std::max<uint16_t>(durationTicks, 1U),
            .windowOffsetTicks = windowOffsetTicks,
            .interpolation = core::state::modulation::
                ProjectCurveInterpolation::LINEAR,
            .valueDomain = core::state::modulation::
                ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR,
            .origin =
                core::state::modulation::ProjectCurveOrigin::NATIVE,
        },
        .pointOffset = 0U,
        .pointCount = pointCount,
        .enabled = pointCount > 0U,
    };
}

}  // namespace

FLASHMEM bool MacroPerformanceDomainServices::armAutomationTake() const {
    return armAutomationTake_(
        pages_->activePageData().activeMacroMask,
        0U
    );
}

FLASHMEM bool MacroPerformanceDomainServices::armAutomationTakeForMacro(
    uint8_t index,
    uint16_t durationTicks
) const {
    if (index >= core::state::macro::MACRO_COUNT ||
        !pages_->activePageData().isMacroActive(index) || durationTicks == 0U) {
        return false;
    }
    return armAutomationTake_(
        static_cast<uint16_t>(1U << index),
        durationTicks
    );
}

FLASHMEM bool MacroPerformanceDomainServices::armAutomationTake_(
    uint16_t candidates,
    uint16_t durationOverride
) const {
    using namespace core::state::macro;
    if (macro_ui_->automationTake.phase != MacroAutomationTakePhase::IDLE ||
        history_ == nullptr) {
        return false;
    }
    candidates = static_cast<uint16_t>(
        candidates & pages_->activePageData().activeMacroMask & 0x00FFU
    );
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
    if (durationOverride > 0U) {
        if (!macro_ui_->automationTake.overrideFixedDuration(durationOverride)) {
            macro_ui_->automationTake.reset();
            return false;
        }
    }
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
        macro_ui_->automationTake.circular = timing !=
            MacroAutomationTakeTiming::HOLD;
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
    OC_PERF_SCOPE(perfBegin, "macro.take.begin");
    using namespace core::state::macro;
    using namespace core::state::modulation;
    auto& take = macro_ui_->automationTake;
    const uint16_t activeMask = static_cast<uint16_t>(
        pages_->activePageData().activeMacroMask & 0x00FFU
    );
    if (take.phase != MacroAutomationTakePhase::ARMED || history_ == nullptr ||
        take.track != pages_->currentActiveTrack() ||
        take.page != pages_->currentActivePage() ||
        take.candidateMask == 0U ||
        (take.candidateMask & activeMask) != take.candidateMask) {
        return false;
    }

    core::state::macro::MacroHistoryChangePtr historyChange{};
    {
        OC_PERF_SCOPE(perfHistory, "macro.take.begin.history");
        historyChange = history_->prepareAutomationTake(
            *pages_,
            take.track,
            take.page,
            take.candidateMask
        );
        OC_PERF_UNITS(
            perfHistory,
            static_cast<uint32_t>(__builtin_popcount(take.candidateMask)),
            take.candidateMask
        );
    }
    core::app::ExtmemUniquePtr<ProjectControlDomainState> staged{};
    {
        OC_PERF_SCOPE(perfCopy, "macro.take.begin.copy-domain");
        staged = core::app::makeExtmemUniqueCopy(
            pages_->control.authored
        );
        OC_PERF_UNITS(
            perfCopy,
            sizeof(ProjectControlDomainState),
            staged ? 1U : 0U
        );
    }
    if (!historyChange || !historyChange->automationTake || !staged) {
        return false;
    }

    // Prove the conservative eight-lane maximum against the exact live arena.
    constexpr uint16_t PREFLIGHT_DURATION_TICKS = 24576U;
    {
        OC_PERF_SCOPE(perfPreflight, "macro.take.begin.preflight");
        for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
            const uint16_t bit = static_cast<uint16_t>(1U << macro);
            if ((take.candidateMask & bit) == 0U) continue;
            auto& snapshot = historyChange->automationTake->after[macro];
            for (uint16_t point = 0U;
                 point < MACRO_AUTOMATION_RECORDING_MAX_POINTS;
                 ++point) {
                snapshot.points[point] = {
                    static_cast<uint16_t>(
                        (static_cast<uint32_t>(point) *
                         PREFLIGHT_DURATION_TICKS) /
                        (MACRO_AUTOMATION_RECORDING_MAX_POINTS - 1U)
                    ),
                    packTakeMidi7(take.initialValues[macro]),
                };
            }
            snapshot.pointCount = MACRO_AUTOMATION_RECORDING_MAX_POINTS;
            snapshot.automation = takeCurvePayload(
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
        OC_PERF_UNITS(
            perfPreflight,
            static_cast<uint32_t>(__builtin_popcount(take.candidateMask)),
            MACRO_AUTOMATION_RECORDING_MAX_POINTS
        );
    }
    {
        OC_PERF_SCOPE(perfValidate, "macro.take.begin.validate");
        const bool valid = validProjectModulationDomain(
            staged->modulation,
            staged->curves,
            &staged->automation
        );
        OC_PERF_UNITS(
            perfValidate,
            staged->automation.entryCount,
            staged->curves.pointCount
        );
        if (!valid) return false;
    }
    // Keep both the proven allocation and an exact pre-take authored snapshot.
    // The authored revision guard at commit makes a second full-domain copy
    // redundant: this restored snapshot is the transaction's staging base.
    {
        OC_PERF_SCOPE(perfRestore, "macro.take.begin.restore-domain");
        *staged = pages_->control.authored;
        OC_PERF_UNITS(perfRestore, sizeof(ProjectControlDomainState), 1U);
    }
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
#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.macro-take.armed"
    );
#endif
    macro_ui_->automationRecordingStatus.set(
        MacroAutomationRecordingStatus::RECORDING
    );
    bumpAutomationRecordingRevision(*macro_ui_);
    OC_PERF_UNITS(
        perfBegin,
        static_cast<uint32_t>(take.candidateMask),
        take.sampleCount
    );
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
        static_cast<double>(UINT32_MAX)
    ));
}

FLASHMEM bool MacroPerformanceDomainServices::seedAutomationTakeColumn_(
    uint8_t index
) const {
    using namespace core::state::macro;
    using namespace core::state::modulation;
    auto& take = macro_ui_->automationTake;
    if (!take.fixedLength() || index >= MACRO_COUNT || take.activeFor(index)) {
        return true;
    }

    ProjectControlMacroDestinationView slot{};
    if (!readProjectControlMacroDestination(
            pages_->control,
            activeAddress_(index),
            slot
        ) || !slot.automation.stored()) {
        return true;
    }
    const float fallback = static_cast<float>(take.initialValues[index]) / 127.0f;
    for (uint16_t sample = 0U; sample < take.sampleCount; ++sample) {
        const float projectBeat = static_cast<float>(take.elapsedTicks[sample]) /
            static_cast<float>(MACRO_AUTOMATION_TICKS_PER_BEAT);
        const float value = evaluateProjectControlCurve(
            pages_->control,
            slot.automation.id,
            projectBeat,
            fallback
        );
        if (!take.seedFixedGridValue(
                index,
                sample,
                core::midi::toCC(macroAutomationClamp01(value))
            )) {
            return false;
        }
    }
    return true;
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
        resetAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
        return false;
    }
    if (take.phase != MacroAutomationTakePhase::RECORDING ||
        take.track != pages_->currentActiveTrack() ||
        take.page != pages_->currentActivePage() || index >= MACRO_COUNT) {
        return false;
    }
    if (!seedAutomationTakeColumn_(index)) {
        restoreAutomationTakeManual_();
        resetAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
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
    if (recorded) bumpAutomationRecordingRevision(*macro_ui_, index);
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
    return true;
}

FLASHMEM bool MacroPerformanceDomainServices::commitAutomationTake_(
    uint32_t nowMs
) const {
    OC_PERF_SCOPE(perfCommit, "macro.take.commit.total");
    using namespace core::state::macro;
    using namespace core::state::modulation;
    auto& take = macro_ui_->automationTake;
    const uint16_t completedMask = take.touchedMask;
    OC_PERF_UNITS(
        perfCommit,
        take.sampleCount,
        static_cast<uint32_t>(__builtin_popcount(take.touchedMask))
    );
    if (take.phase != MacroAutomationTakePhase::RECORDING ||
        !macro_ui_->automationTakeHistory ||
        !macro_ui_->automationTakeHistory->automationTake ||
        !macro_ui_->automationTakeDomain ||
        pages_->control.authoredRevision != take.authoredRevision) {
        restoreAutomationTakeManual_();
        resetAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
        return false;
    }
    bool finished = false;
    {
        OC_PERF_SCOPE(perfFinish, "macro.take.commit.finish");
        finished = take.finish(automationTakeElapsedTicks_(nowMs));
        OC_PERF_UNITS(perfFinish, take.sampleCount, take.durationTicks);
    }
    if (!finished) {
        restoreAutomationTakeManual_();
        resetAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
        return false;
    }
    if (take.changedMask == 0U) {
        // A real gesture that resolves byte-identically to the prefilled lane
        // is a valid no-op, not a failed recording.
        restoreAutomationTakeManual_();
        resetAutomationTake_(MacroAutomationRecordingStatus::IDLE);
        macro_ui_->armPostTakeInputGuard(completedMask, nowMs);
        return true;
    }

#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.macro-take.commit-begin"
    );
#endif
    auto& payload = *macro_ui_->automationTakeHistory->automationTake;
    payload.touchedMask = take.touchedMask;
    for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((take.touchedMask & bit) == 0U) continue;
        auto& snapshot = payload.after[macro];
        uint16_t written = 0U;
        bool built = false;
        {
            OC_PERF_SCOPE(perfBuild, "macro.take.commit.build-curve");
            built = take.buildPackedCurve(
                macro,
                snapshot.points.get(),
                MACRO_AUTOMATION_RECORDING_MAX_POINTS,
                written
            );
            OC_PERF_UNITS(perfBuild, take.sampleCount, written);
        }
        if (!built) {
            restoreAutomationTakeManual_();
            resetAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
            return false;
        }
        snapshot.pointCount = written;
        snapshot.automation = takeCurvePayload(
            written,
            take.durationTicks,
            take.playbackWindowOffsetTicks()
        );
        bool replaced = false;
        {
            OC_PERF_SCOPE(perfReplace, "macro.take.commit.replace");
            replaced = replaceProjectControlAutomationInDomain(
                *macro_ui_->automationTakeDomain,
                snapshot.address,
                snapshot.automation,
                snapshot.points.get(),
                snapshot.pointCount
            );
            OC_PERF_UNITS(perfReplace, snapshot.pointCount, macro);
        }
        if (!replaced) {
            restoreAutomationTakeManual_();
            resetAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
            return false;
        }
    }
    bool domainValid = false;
    {
        OC_PERF_SCOPE(perfValidate, "macro.take.commit.validate");
        domainValid = validProjectModulationDomain(
            macro_ui_->automationTakeDomain->modulation,
            macro_ui_->automationTakeDomain->curves,
            &macro_ui_->automationTakeDomain->automation
        );
        OC_PERF_UNITS(
            perfValidate,
            macro_ui_->automationTakeDomain->automation.entryCount,
            macro_ui_->automationTakeDomain->curves.pointCount
        );
    }
    if (!domainValid) {
        restoreAutomationTakeManual_();
        resetAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
        return false;
    }

    {
        OC_PERF_SCOPE(perfPublish, "macro.take.commit.publish-domain");
        pages_->control.authored = *macro_ui_->automationTakeDomain;
        OC_PERF_UNITS(
            perfPublish,
            sizeof(ProjectControlDomainState),
            take.touchedMask
        );
    }
    pages_->control.markAuthoredMutation();
    bool historyCommitted = false;
    {
        OC_PERF_SCOPE(perfHistory, "macro.take.commit.history");
        historyCommitted = history_->commitPreparedAutomationTake(
            *pages_,
            macro_ui_->automationTakeHistory
        );
        OC_PERF_UNITS(perfHistory, take.touchedMask, take.sampleCount);
    }
    if (!historyCommitted) {
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
        resetAutomationTake_(MacroAutomationRecordingStatus::COMMIT_FAILED);
        return false;
    }
#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.macro-take.committed"
    );
#endif
    refreshManualProjection_();
    resetAutomationTake_(MacroAutomationRecordingStatus::IDLE);
    macro_ui_->armPostTakeInputGuard(completedMask, nowMs);
    if (operations_.table != nullptr &&
        operations_.table->markProjectMutated != nullptr) {
        operations_.table->markProjectMutated(operations_.context);
    }
    return true;
}

FLASHMEM bool MacroPerformanceDomainServices::releaseAutomationTake(
    uint32_t nowMs
) const {
    using namespace core::state::macro;
    const auto phase = macro_ui_->automationTake.phase;
    if (phase == MacroAutomationTakePhase::ARMED) {
        resetAutomationTake_(MacroAutomationRecordingStatus::IDLE);
        return true;
    }
    if (phase != MacroAutomationTakePhase::RECORDING) return false;
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

FLASHMEM void MacroPerformanceDomainServices::resetAutomationTake_(
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
    resetAutomationTake_(MacroAutomationRecordingStatus::IDLE);
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
}  // namespace core::handler
