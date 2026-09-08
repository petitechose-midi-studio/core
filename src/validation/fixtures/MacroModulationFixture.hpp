#pragma once

// Pure state fixture shared by SDL capture and the opt-in hardware benchmark.
#include <cstdio>

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"

namespace core::validation::fixtures {

inline void prepareMacroAutomationCleanScenario(core::state::CoreState& state) {
    state.activeView.set(core::ui::ViewType::MACRO);
    state.overlays.hideAll();
    state.pages.initDefaults();
    state.pages.control.clear();
    state.macroUi.resetInteraction();
    state.macroUi.resetProjectRuntime();
    state.macroEdit.reset();
    state.trackNavigation.reset();
    state.structureClipboard.clear();
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
}

inline core::state::modulation::ModulatorId addReusableLfo(
    core::state::CoreState& state,
    const char* name,
    core::state::modulation::ModulatorLfoShape shape,
    uint32_t periodTicks,
    uint8_t accent
) {
    using namespace core::state::modulation;
    ModulatorLfoDraft draft{};
    draft.name = name;
    draft.parameters.periodTicks = periodTicks;
    draft.parameters.shape = shape;
    draft.parameters.retrigger = ModulatorRetriggerPolicy::TRANSPORT;
    draft.parameters.timing = ModulatorTimingMode::SYNC;
    draft.accent = accent;
    return createLfoModulator(
        state.pages.control.authored.modulation,
        draft
    ).sourceId;
}

inline void prepareMacroReusableModulatorsScenario(core::state::CoreState& state) {
    using namespace core::state::modulation;
    prepareMacroAutomationCleanScenario(state);
    state.setSharedTrackState(0x0003, 0);

    auto& track = state.pages.tracks[0];
    (void)core::state::project::setProjectTrackMidiChannel(
        state.projectTracks,
        0U,
        5U
    );
    track.activePage = 0;
    track.enabledPageMask = 0x0001;
    auto& page = track.pages[0];
    page.cc[0] = 74;
    page.values[0] = 0.42f;
    page.setMacroActive(0, true);
    std::snprintf(page.name, sizeof(page.name), "%s", "Reusable LFOs");

    auto& remoteTrack = state.pages.tracks[1];
    (void)core::state::project::setProjectTrackMidiChannel(
        state.projectTracks,
        1U,
        6U
    );
    remoteTrack.activePage = 0;
    remoteTrack.enabledPageMask = 0x0001;
    auto& remotePage = remoteTrack.pages[0];
    remotePage.cc[0] = 71;
    remotePage.values[0] = 0.5f;
    remotePage.setMacroActive(0, true);
    std::snprintf(remotePage.name, sizeof(remotePage.name), "%s", "Remote use");

    state.pages.syncSharedTrackState(0x0003, 0);
    state.pages.setActivePage(0);
    state.macroUi.syncPreviewPage(0);
    state.trackNavigation.syncPreviewTrack(0);
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);

    const auto slowTide = addReusableLfo(
        state,
        "Slow Tide",
        core::state::modulation::ModulatorLfoShape::SINE,
        core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT * 4U,
        0
    );
    const auto pulseLift = addReusableLfo(
        state,
        "Pulse Lift",
        core::state::modulation::ModulatorLfoShape::TRIANGLE,
        core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT,
        1
    );
    if (core::state::modulation::valid(slowTide) ||
        core::state::modulation::valid(pulseLift)) {
        state.pages.control.markAuthoredMutation();
    }
    if (valid(slowTide)) {
        ModulationBindingDraft remote{};
        remote.sourceId = slowTide;
        remote.destination = projectControlDestination({1U, 0U, 0U});
        remote.amountQ15 = 4096;
        remote.application = ModulationApplication::NATURAL;
        (void)addProjectModulationBinding(
            state.pages.control.authored.modulation,
            remote
        );
        state.pages.control.markAuthoredMutation();
    }

    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
}

inline core::state::modulation::ModulationBindingId bindReusableModulator(
    core::state::CoreState& state,
    core::state::modulation::ModulatorId sourceId,
    int16_t amountQ15,
    uint8_t track = 0,
    uint8_t page = 0,
    uint8_t macro = 0
) {
    using namespace core::state::modulation;
    ModulationBindingDraft draft{};
    draft.sourceId = sourceId;
    draft.destination = projectControlDestination({
        .track = track,
        .page = page,
        .macro = macro,
    });
    draft.amountQ15 = amountQ15;
    draft.application = ModulationApplication::NATURAL;
    draft.enabled = true;
    return addProjectModulationBinding(
        state.pages.control.authored.modulation,
        draft
    ).bindingId;
}

inline void prepareMacroMultiModulationScenario(core::state::CoreState& state) {
    prepareMacroReusableModulatorsScenario(state);
    auto& graph = state.pages.control.authored.modulation;
    if (graph.sourceCount < 2U) return;
    const auto drift = addReusableLfo(
        state,
        "Drift",
        core::state::modulation::ModulatorLfoShape::SAW_DOWN,
        core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT * 2U,
        2
    );
    const auto slowBinding = bindReusableModulator(
        state,
        graph.sources[0].id,
        8192
    );
    const auto pulseBinding = bindReusableModulator(
        state,
        graph.sources[1].id,
        -13107
    );
    (void)slowBinding;
    if (core::state::modulation::valid(drift)) {
        (void)bindReusableModulator(state, drift, 4915);
    }
    state.pages.control.markAuthoredMutation();
    if (core::state::modulation::valid(pulseBinding)) {
        (void)core::state::modulation::setProjectControlFocusedModulationBinding(
            state.pages.control,
            {.track = 0, .page = 0, .macro = 0},
            pulseBinding
        );
    }
}

}  // namespace core::validation::fixtures
