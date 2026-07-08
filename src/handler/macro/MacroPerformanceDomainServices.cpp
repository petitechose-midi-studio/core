#include "handler/macro/MacroPerformanceDomainServices.hpp"

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

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
        },
        Operations{
            &state,
            markProjectMutatedFromCoreState,
            setConfigFromCoreState,
            setTrackChannelFromCoreState,
            switchToPageFromCoreState,
        },
    };
}

float MacroPerformanceDomainServices::runtimeValue(uint8_t index) const {
    return core::state::macro::MacroWorkflow::runtimeValue(*macros_, index);
}

void MacroPerformanceDomainServices::setRuntimeValue(uint8_t index, float value) const {
    core::state::macro::MacroWorkflow::setRuntimeValue(*macros_, index, value);
}

bool MacroPerformanceDomainServices::beginAutomationRecording(uint8_t index,
                                                              uint32_t nowMs) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    if (!pages_->isMacroSlotActive(index)) return false;
    auto& recording = macro_ui_->automationRecording;
    if (recording.active) return false;

    recording.reset();
    recording.active = true;
    recording.address = {
        .track = pages_->currentActiveTrack(),
        .page = pages_->currentActivePage(),
        .macro = index,
    };
    const auto* existing = core::state::macro::macroAutomationFindSlot(
        pages_->automation,
        recording.address
    );
    if (existing != nullptr && existing->automation.active) {
        recording.preserveDuration = true;
        recording.targetDurationTicks = existing->automation.durationTicks;
    }
    setAutomationManualOverride(index, false);
    recording.startedAtMs = nowMs;
    const bool appended = core::state::macro::macroAutomationAppendPoint(
        recording.lane,
        0.0f,
        runtimeValue(index)
    );
    if (appended) {
        bumpAutomationRecordingRevision(*macro_ui_);
    }
    return appended;
}

bool MacroPerformanceDomainServices::recordAutomationPoint(uint8_t index,
                                                           uint32_t nowMs,
                                                           float value) const {
    auto& recording = macro_ui_->automationRecording;
    if (!recording.active || recording.address.macro != index) return false;

    const float beat = core::state::macro::macroAutomationElapsedBeats(
        recording.startedAtMs,
        nowMs,
        status_bar_->tempo.get()
    );
    const bool appended = core::state::macro::macroAutomationAppendPoint(
        recording.lane,
        beat,
        value
    );
    if (appended) {
        bumpAutomationRecordingRevision(*macro_ui_);
    }
    return appended;
}

bool MacroPerformanceDomainServices::commitAutomationRecording(uint32_t nowMs) const {
    auto& recording = macro_ui_->automationRecording;
    if (!recording.active) return false;
    if (recording.lane.pointCount < 2) {
        recording.reset();
        bumpAutomationRecordingRevision(*macro_ui_);
        return false;
    }

    const float duration = core::state::macro::macroAutomationElapsedBeats(
        recording.startedAtMs,
        nowMs,
        status_bar_->tempo.get()
    );
    auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
        pages_->automation,
        recording.address
    );
    if (slot == nullptr) {
        recording.reset();
        bumpAutomationRecordingRevision(*macro_ui_);
        return false;
    }

    if (recording.preserveDuration) {
        core::state::macro::macroAutomationFinalizeRecordingWithDuration(
            recording.lane,
            duration,
            core::state::macro::macroAutomationBeatsFromTicks(recording.targetDurationTicks)
        );
    } else {
        core::state::macro::macroAutomationFinalizeRecording(recording.lane, duration);
    }

    if (!core::state::macro::macroAutomationAssignAutomation(
            pages_->automation,
            *slot,
            recording.lane
        )) {
        recording.reset();
        bumpAutomationRecordingRevision(*macro_ui_);
        return false;
    }
    setAutomationManualOverride(recording.address.macro, false);
    recording.reset();
    bumpAutomationRecordingRevision(*macro_ui_);
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return true;
}

bool MacroPerformanceDomainServices::cancelAutomationRecording() const {
    if (!macro_ui_->automationRecording.active) return false;
    macro_ui_->automationRecording.reset();
    bumpAutomationRecordingRevision(*macro_ui_);
    return true;
}

bool MacroPerformanceDomainServices::automationRecordingActiveFor(uint8_t index) const {
    const auto& recording = macro_ui_->automationRecording;
    return recording.active && recording.address.macro == index;
}

bool MacroPerformanceDomainServices::automationActiveFor(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    const auto* slot = core::state::macro::macroAutomationFindSlot(
        pages_->automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = pages_->currentActiveTrack(),
            .page = pages_->currentActivePage(),
            .macro = index,
        }
    );
    return slot != nullptr && slot->automation.active;
}

bool MacroPerformanceDomainServices::automationManualOverrideActiveFor(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    const uint16_t bit = static_cast<uint16_t>(1U << index);
    return (macro_ui_->automationManualOverrideMask.get() & bit) != 0;
}

void MacroPerformanceDomainServices::setAutomationManualOverride(uint8_t index,
                                                                 bool active) const {
    if (index >= core::state::macro::MACRO_COUNT) return;
    const uint16_t bit = static_cast<uint16_t>(1U << index);
    uint16_t mask = macro_ui_->automationManualOverrideMask.get();
    const uint16_t next = active
        ? static_cast<uint16_t>(mask | bit)
        : static_cast<uint16_t>(mask & ~bit);
    if (next == mask) return;
    macro_ui_->automationManualOverrideMask.set(next);
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
