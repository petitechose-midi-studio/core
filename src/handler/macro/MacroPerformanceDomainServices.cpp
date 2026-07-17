#include "handler/macro/MacroPerformanceDomainServices.hpp"

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"

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

void MacroPerformanceDomainServices::restoreManualAfterFailedRecording_(
    const core::state::macro::MacroUiState::AutomationRecordingState& recording
) const {
    if (recording.restoreManualOnFailure) {
        const auto status = macro_ui_->manualOverrides.activate(
            recording.address,
            recording.previousManualValue
        );
        if (status != core::state::macro::MacroManualOverrideState::ActivateStatus::INVALID_ADDRESS &&
            status != core::state::macro::MacroManualOverrideState::ActivateStatus::CAPACITY_EXHAUSTED &&
            core::state::macro::macroAutomationAddressEquals(
                recording.address,
                activeAddress_(recording.address.macro)
            )) {
            setResolvedValue(recording.address.macro, recording.previousManualValue);
        }
    }
    refreshManualProjection_();
}

float MacroPerformanceDomainServices::runtimeValue(uint8_t index) const {
    return core::state::macro::MacroWorkflow::runtimeValue(*macros_, index);
}

float MacroPerformanceDomainServices::absoluteBaseValue(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return 0.0f;
    const auto& recording = macro_ui_->automationRecording;
    if (recording.active &&
        core::state::macro::macroAutomationAddressEquals(
            recording.address,
            activeAddress_(index)
        ) && recording.lane.pointCount > 0) {
        return recording.lane.points[recording.lane.pointCount - 1U].value;
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
            slot.legacy.modulationDepth
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
            slot.modulationEnabled && slot.legacy.modulationDepth <= 0.0f;
        out.modulationActive =
            slot.modulationEnabled && slot.legacy.modulationDepth > 0.0f;
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

FLASHMEM bool MacroPerformanceDomainServices::beginAutomationRecording(
    uint8_t index,
    uint32_t nowMs
) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    if (!pages_->isMacroSlotActive(index)) return false;
    auto& recording = macro_ui_->automationRecording;
    if (recording.active) return false;

    recording.reset();
    recording.address = activeAddress_(index);
    recording.restoreManualOnFailure = macro_ui_->manualOverrides.valueFor(
        recording.address,
        recording.previousManualValue
    );
    core::state::modulation::ProjectControlMacroSlotView existing{};
    if (core::state::modulation::readProjectControlMacroSlot(
            pages_->control,
            recording.address,
            existing
        ) && existing.automationStored) {
        recording.preserveDuration = true;
        recording.targetDurationTicks = existing.legacy.automation.durationTicks;
    }
    // Recording owns the absolute source for the duration of the gesture.
    // Modulation remains audible and is never suspended by recording.
    (void)macro_ui_->manualOverrides.resume(recording.address);
    refreshManualProjection_();
    recording.active = true;
    macro_ui_->automationRecordingStatus.set(
        core::state::macro::MacroAutomationRecordingStatus::RECORDING
    );
    recording.startedAtMs = nowMs;
    const bool appended = core::state::macro::macroAutomationAppendPoint(
        recording.lane,
        0.0f,
        recording.restoreManualOnFailure
            ? recording.previousManualValue
            : absoluteBaseValue(index)
    );
    if (appended) {
        bumpAutomationRecordingRevision(*macro_ui_);
    } else {
        restoreManualAfterFailedRecording_(recording);
        recording.reset();
        macro_ui_->automationRecordingStatus.set(
            core::state::macro::MacroAutomationRecordingStatus::COMMIT_FAILED
        );
    }
    return appended;
}

bool MacroPerformanceDomainServices::recordAutomationPoint(uint8_t index,
                                                           uint32_t nowMs,
                                                           float value) const {
    auto& recording = macro_ui_->automationRecording;
    if (!recording.active ||
        !core::state::macro::macroAutomationAddressEquals(
            recording.address,
            activeAddress_(index)
        )) {
        return false;
    }

    const float beat = core::state::macro::macroAutomationElapsedBeats(
        recording.startedAtMs,
        nowMs,
        status_bar_->tempo.get()
    );
    bool reduced = false;
    const bool appended = core::state::macro::macroAutomationAppendPoint(
        recording.lane,
        beat,
        value,
        &reduced
    );
    if (reduced) {
        macro_ui_->automationRecordingStatus.set(
            core::state::macro::MacroAutomationRecordingStatus::REDUCED
        );
    }
    if (appended) {
        bumpAutomationRecordingRevision(*macro_ui_);
    }
    return appended;
}

FLASHMEM bool MacroPerformanceDomainServices::commitAutomationRecording(
    uint32_t nowMs
) const {
    auto& recording = macro_ui_->automationRecording;
    if (!recording.active) return false;
    if (recording.lane.pointCount < 2) {
        restoreManualAfterFailedRecording_(recording);
        recording.reset();
        macro_ui_->automationRecordingStatus.set(
            core::state::macro::MacroAutomationRecordingStatus::TOO_SHORT
        );
        bumpAutomationRecordingRevision(*macro_ui_);
        return false;
    }

    const float duration = core::state::macro::macroAutomationElapsedBeats(
        recording.startedAtMs,
        nowMs,
        status_bar_->tempo.get()
    );
    if (recording.preserveDuration) {
        core::state::macro::macroAutomationFinalizeRecordingWithDuration(
            recording.lane,
            duration,
            core::state::macro::macroAutomationBeatsFromTicks(recording.targetDurationTicks)
        );
    } else {
        core::state::macro::macroAutomationFinalizeRecording(recording.lane, duration);
    }

    auto historyChange = history_ != nullptr
        ? history_->prepareAutomationRecording(
              *pages_,
              recording.address
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !historyChange) {
        restoreManualAfterFailedRecording_(recording);
        recording.reset();
        macro_ui_->automationRecordingStatus.set(
            core::state::macro::MacroAutomationRecordingStatus::COMMIT_FAILED
        );
        bumpAutomationRecordingRevision(*macro_ui_);
        return false;
    }

    if (!core::state::modulation::assignProjectControlAutomation(
            pages_->control,
            recording.address,
            recording.lane
        )) {
        if (historyChange && historyChange->automation) {
            (void)core::state::macro::applyMacroAutomationHistorySnapshot(
                *pages_,
                historyChange->automation->before
            );
        }
        restoreManualAfterFailedRecording_(recording);
        recording.reset();
        macro_ui_->automationRecordingStatus.set(
            core::state::macro::MacroAutomationRecordingStatus::COMMIT_FAILED
        );
        bumpAutomationRecordingRevision(*macro_ui_);
        return false;
    }
    if (history_ != nullptr && !history_->commitPrepared(
            *pages_,
            std::move(historyChange)
        )) {
        restoreManualAfterFailedRecording_(recording);
        recording.reset();
        macro_ui_->automationRecordingStatus.set(
            core::state::macro::MacroAutomationRecordingStatus::COMMIT_FAILED
        );
        bumpAutomationRecordingRevision(*macro_ui_);
        return false;
    }
    (void)macro_ui_->manualOverrides.resume(recording.address);
    refreshManualProjection_();
    recording.reset();
    macro_ui_->automationRecordingStatus.set(
        core::state::macro::MacroAutomationRecordingStatus::IDLE
    );
    bumpAutomationRecordingRevision(*macro_ui_);
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

bool MacroPerformanceDomainServices::cancelAutomationRecording() const {
    if (!macro_ui_->automationRecording.active) return false;
    restoreManualAfterFailedRecording_(macro_ui_->automationRecording);
    macro_ui_->automationRecording.reset();
    macro_ui_->automationRecordingStatus.set(
        core::state::macro::MacroAutomationRecordingStatus::IDLE
    );
    bumpAutomationRecordingRevision(*macro_ui_);
    return true;
}

bool MacroPerformanceDomainServices::automationRecordingActiveFor(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    const auto& recording = macro_ui_->automationRecording;
    return recording.active &&
           core::state::macro::macroAutomationAddressEquals(
               recording.address,
               activeAddress_(index)
           );
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
