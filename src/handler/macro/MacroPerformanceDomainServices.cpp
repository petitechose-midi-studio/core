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
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"

namespace core::handler {

namespace {

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

bool setMacroValueFromCoreState(void* context, uint8_t index, float value) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr && state->setMacroValueWithHistory(index, value);
}

bool setManualOverrideFromCoreState(
    void* context,
    uint8_t index,
    float value,
    bool coalesceValue
) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr && state->takeMacroManualControlWithHistory(
        index,
        value,
        coalesceValue
    );
}

bool resumeComputedSourcesFromCoreState(void* context, uint8_t index) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr &&
           state->resumeMacroComputedSourcesWithHistory(index);
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

bool setTrackChannelGestureActiveFromCoreState(void* context, bool active) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return false;
    auto services =
        core::state::project::ProjectTrackDomainServices::fromCoreState(*state);
    return active
        ? services.beginGesture(
              core::state::project::ProjectTrackHistoryActionKind::MidiChannel,
              state->pages.currentActiveTrack()
          )
        : services.endGesture();
}

void switchToPageFromCoreState(void* context, uint8_t pageIndex) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    core::state::macro::MacroWorkflow::switchToPage(*state, pageIndex);
}

const MacroPerformanceDomainServices::OperationTable kCoreStateOperations{
    .markProjectMutated = markProjectMutatedFromCoreState,
    .markMacroValueEdited = markMacroValueEditedFromCoreState,
    .setConfig = setConfigFromCoreState,
    .setTrackChannel = setTrackChannelFromCoreState,
    .setTrackChannelGestureActive =
        setTrackChannelGestureActiveFromCoreState,
    .switchToPage = switchToPageFromCoreState,
    .setMacroValue = setMacroValueFromCoreState,
    .setManualOverride = setManualOverrideFromCoreState,
    .resumeComputedSources = resumeComputedSourcesFromCoreState,
};

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
    , project_tracks_(&state.projectTracks)
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
            state.projectTracks,
            &state.macroHistory,
        },
        Operations{
            .context = &state,
            .table = &kCoreStateOperations,
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

float MacroPerformanceDomainServices::currentPlaybackBaseValue(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return 0.0f;
    if (macro_ui_->runtimeProjectionValidFor(
            pages_->currentActiveTrack(),
            pages_->currentActivePage(),
            index
        )) {
        return core::state::macro::macroAutomationClamp01(
            macro_ui_->runtimeProjections[index].base
        );
    }
    return absoluteBaseValue(index);
}

void MacroPerformanceDomainServices::setManualValue(uint8_t index, float value) const {
    if (index >= core::state::macro::MACRO_COUNT) return;
    const float manualValue = core::state::macro::macroAutomationClamp01(value);
    auto& page = pages_->activePageData();
    if (page.values[index] != manualValue) {
        if (operations_.table != nullptr &&
            operations_.table->setMacroValue != nullptr) {
            if (!operations_.table->setMacroValue(
                    operations_.context,
                    index,
                    manualValue
                )) {
                core::state::macro::MacroWorkflow::setRuntimeValue(
                    *macros_,
                    index,
                    page.values[index]
                );
                return;
            }
        } else {
            page.values[index] = manualValue;
            if (operations_.table != nullptr &&
                operations_.table->markMacroValueEdited != nullptr) {
                operations_.table->markMacroValueEdited(
                    operations_.context,
                    index
                );
            }
        }
    }
    core::state::macro::MacroWorkflow::setRuntimeValue(*macros_, index, manualValue);
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
    core::state::modulation::ProjectControlMacroDestinationView slot{};
    if (core::state::modulation::readProjectControlMacroDestination(
            pages_->control,
            activeAddress_(index),
            slot
        ) && slot.primaryModulation.isRecordedShape()) {
        depth = core::state::macro::macroAutomationClamp01(
            slot.primaryModulation.amount
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
    core::state::modulation::ProjectControlMacroDestinationView slot{};
    if (core::state::modulation::readProjectControlMacroDestination(
            pages_->control,
            activeAddress_(index),
            slot
        ) && slot.present()) {
        out.automationStored = slot.automation.stored();
        out.modulationStored = slot.modulationCount > 0U;
        out.modulationPausedDepthZero =
            slot.primaryModulation.enabled &&
            slot.primaryModulation.amount <= 0.0f;
        out.modulationActive =
            slot.primaryModulation.enabled &&
            slot.primaryModulation.amount > 0.0f;
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

bool MacroPerformanceDomainServices::computedSourcePlaybackActiveFor(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    core::state::modulation::ProjectControlMacroDestinationView slot{};
    return core::state::modulation::readProjectControlMacroDestination(
               pages_->control,
               activeAddress_(index),
               slot
           ) && (slot.automation.enabled ||
                 slot.activeModulationCount > 0U);
}

bool MacroPerformanceDomainServices::automationActiveFor(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    core::state::modulation::ProjectControlMacroDestinationView slot{};
    return core::state::modulation::readProjectControlMacroDestination(
               pages_->control,
               activeAddress_(index),
               slot
           ) && slot.automation.stored();
}

bool MacroPerformanceDomainServices::automationPlaybackActiveFor(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    core::state::modulation::ProjectControlMacroDestinationView slot{};
    return core::state::modulation::readProjectControlMacroDestination(
               pages_->control,
               activeAddress_(index),
               slot
           ) && slot.automation.stored() && slot.automation.enabled &&
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

bool MacroPerformanceDomainServices::takeManualControl(
    uint8_t index,
    float value,
    bool coalesceValue
) const {
    if (index >= core::state::macro::MACRO_COUNT ||
        !pages_->isMacroSlotActive(index)) {
        return false;
    }
    const bool continuingOverride = manualOverrideActiveFor(index);
    if (!continuingOverride && !automationPlaybackActiveFor(index)) return false;
    const auto address = activeAddress_(index);
    bool committed = false;
    if (operations_.table != nullptr &&
        operations_.table->setManualOverride != nullptr) {
        committed = operations_.table->setManualOverride(
            operations_.context,
            index,
            value,
            coalesceValue
        );
    } else if (history_ != nullptr) {
        const float beforeBase = pages_->pageData(address.track, address.page)
            .values[address.macro];
        committed = history_->setManualOverrideCoalesced(
            *pages_,
            macro_ui_->manualOverrides,
            address,
            value,
            coalesceValue
        );
        if (committed && beforeBase != pages_->pageData(address.track, address.page)
                                      .values[address.macro] &&
            operations_.table != nullptr &&
            operations_.table->markMacroValueEdited != nullptr) {
            operations_.table->markMacroValueEdited(
                operations_.context,
                index
            );
        }
    } else {
        const auto status = macro_ui_->manualOverrides.activate(address, value);
        if (status != core::state::macro::MacroManualOverrideState::ActivateStatus::INVALID_ADDRESS &&
            status != core::state::macro::MacroManualOverrideState::ActivateStatus::CAPACITY_EXHAUSTED) {
            setManualValue(index, value);
            committed = true;
        }
    }
    if (!committed) return false;
    float sanitized = value;
    (void)manualOverrideValueFor(index, sanitized);
    // Commit the physical absolute value only after the address-scoped
    // Automation override is guaranteed to exist. Every later manual delta
    // updates the same runtime authority; otherwise the playback frame would
    // keep publishing the takeover value and visually snap the knob back.
    // Modulation remains live and is resolved around this Base below.
    setResolvedValue(index, resolveManualValue(index, sanitized));
    refreshManualProjection_();
    return true;
}

bool MacroPerformanceDomainServices::resumeComputedSources(uint8_t index) const {
    if (index >= core::state::macro::MACRO_COUNT) return false;
    const auto address = activeAddress_(index);
    const bool resumed = operations_.table != nullptr &&
                         operations_.table->resumeComputedSources != nullptr
        ? operations_.table->resumeComputedSources(operations_.context, index)
        : history_ != nullptr
            ? history_->resumeManualOverride(
                  *pages_,
                  macro_ui_->manualOverrides,
                  address
              )
            : macro_ui_->manualOverrides.resume(address);
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
    const auto address = activeAddress_(index);
    auto change = history_ != nullptr
        ? history_->prepare(
              *pages_,
              address,
              core::state::macro::MacroHistoryActionKind::CREATE_SLOT
          )
        : core::state::macro::MacroHistoryChangePtr{};
    if (history_ != nullptr && !change) return false;
    if (!core::state::macro::MacroWorkflow::activateMacroSlot(*macros_, *pages_, index)) {
        return false;
    }
    if (history_ != nullptr &&
        !history_->commitPrepared(*pages_, std::move(change))) {
        core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
            *macros_,
            *pages_
        );
        return false;
    }

    if (config_revision_ != nullptr) {
        config_revision_->set(core::state::macro::nextMacroConfigRevision(
            config_revision_->get(),
            index
        ));
    }
    if (operations_.table != nullptr &&
        operations_.table->markProjectMutated != nullptr) {
        operations_.table->markProjectMutated(operations_.context);
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
    return operations_.table != nullptr &&
           operations_.table->setConfig != nullptr &&
           operations_.table->setConfig(
               operations_.context,
               index,
               channel,
               cc
           );
}

uint8_t MacroPerformanceDomainServices::activeTrackChannel() const {
    return core::state::project::projectTrackMidiChannel(
        *project_tracks_,
        pages_->currentActiveTrack()
    );
}

bool MacroPerformanceDomainServices::setTrackChannel(uint8_t channel) const {
    return operations_.table != nullptr &&
           operations_.table->setTrackChannel != nullptr &&
           operations_.table->setTrackChannel(operations_.context, channel);
}

bool MacroPerformanceDomainServices::beginTrackChannelGesture() const {
    return operations_.table != nullptr &&
           operations_.table->setTrackChannelGestureActive != nullptr &&
           operations_.table->setTrackChannelGestureActive(
               operations_.context,
               true
           );
}

bool MacroPerformanceDomainServices::endTrackChannelGesture() const {
    return operations_.table != nullptr &&
           operations_.table->setTrackChannelGestureActive != nullptr &&
           operations_.table->setTrackChannelGestureActive(
               operations_.context,
               false
           );
}

bool MacroPerformanceDomainServices::isActivePageEnabled() const {
    return pages_->isPageEnabled(pages_->currentActivePage());
}

void MacroPerformanceDomainServices::switchToPage(uint8_t pageIndex) const {
    if (operations_.table != nullptr &&
        operations_.table->switchToPage != nullptr) {
        operations_.table->switchToPage(operations_.context, pageIndex);
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

void MacroPerformanceDomainServices::pulseNoteIn() const {
    status_bar_->pulseNoteIn();
}

}  // namespace core::handler
