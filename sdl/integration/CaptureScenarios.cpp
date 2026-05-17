#include "integration/CaptureScenarios.hpp"

#include <cstdio>
#include <cstring>

#include <SDL2/SDL.h>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "state/DataManagerCatalog.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"

namespace sdl::integration {

namespace {

void prepareSequencerVariationScenario(core::state::CoreState& state,
                                       core::state::sequencer::StepProperty property) {
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.activeStepProperty.set(property);
    state.sequencer.setStepDataAt(0, 60, 100, 75, 0);
    if (!state.sequencer.isEnabled(0)) {
        state.sequencer.toggle(0);
    }

    oc::note::sequencer::StepSequencerVariationRanges ranges{
        .pitchSemitones = 12,
        .velocity = 24,
        .gatePercent = 30,
        .nudge = 20,
    };
    state.sequencer.setPatternVariationRanges(ranges);

    oc::note::sequencer::StepSequencerResolvedVariation variation{};
    variation.stepIndex = 0;
    variation.cycleIndex = 1;
    variation.triggered = true;
    variation.base = {.note = 60, .velocity = 100, .gate = 75, .nudge = 0};
    variation.resolved = variation.base;
    variation.ranges = ranges;
    variation.pitchDelta = 4;
    variation.velocityDelta = -12;
    variation.gateDelta = 18;
    variation.nudgeDelta = -8;
    variation.resolved.note = 64;
    variation.resolved.velocity = 88;
    variation.resolved.gate = 93;
    variation.resolved.nudge = -8;

    state.sequencer.lastResolvedVariation = variation;
    state.sequencer.cycleVariationTelemetry.reset();
    state.sequencer.cycleVariationTelemetry.cycleIndex = variation.cycleIndex;
    state.sequencer.cycleVariationTelemetry.ranges = ranges;
    state.sequencer.cycleVariationTelemetry.store(variation);
    state.sequencer.variationTelemetryRevision.set(0);
}

}  // namespace

bool applyCaptureScenario(core::state::CoreState& state, const char* scenario) {
    if (!scenario || scenario[0] == '\0' || std::strcmp(scenario, "macro") == 0) {
        return true;
    }

    if (std::strcmp(scenario, "macro-edit") == 0) {
        const auto& config = core::state::macro::MacroWorkflow::activeConfig(state.pages, 0);
        state.overlays.show(core::ui::OverlayType::MACRO_EDIT, false);
        state.macroEdit.openEditor(0, config.channel, config.cc, SDL_GetTicks());
        return true;
    }

    if (std::strcmp(scenario, "macro-page-selector") == 0) {
        state.overlays.show(core::ui::OverlayType::PAGE_SELECTOR, false);
        state.pages.selector.selectedIndex.set(state.pages.currentActivePage());
        return true;
    }

    if (std::strcmp(scenario, "view-selector") == 0) {
        state.overlays.show(core::ui::OverlayType::VIEW_SELECTOR, false);
        state.viewSelector.selectedIndex.set(static_cast<int>(state.activeView.get()));
        return true;
    }

    if (std::strcmp(scenario, "sequencer") == 0) {
        state.activeView.set(core::ui::ViewType::SEQUENCER);
        return true;
    }

    if (std::strcmp(scenario, "seq-step-edit") == 0) {
        state.activeView.set(core::ui::ViewType::SEQUENCER);
        state.sequencer.setStepDataAt(0, 60, 100, 75);
        if (!state.sequencer.isEnabled(0)) {
            state.sequencer.toggle(0);
        }
        state.overlays.show(core::ui::OverlayType::SEQ_STEP_EDIT, false);
        state.sequencer.stepEdit.stepIndex.set(0);
        state.sequencer.stepEdit.focusedRow.set(0);
        return true;
    }

    if (std::strcmp(scenario, "seq-property-selector") == 0) {
        state.activeView.set(core::ui::ViewType::SEQUENCER);
        state.sequencer.stepPropertyInlineSelector.selecting.set(true);
        state.sequencer.stepPropertyInlineSelector.selectedIndex.set(
            static_cast<int>(core::state::sequencer::StepProperty::GATE)
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-quick-controls") == 0) {
        state.activeView.set(core::ui::ViewType::SEQUENCER);
        state.sequencer.patternQuickControls.selecting.set(true);
        state.sequencer.activeStepProperty.set(core::state::sequencer::StepProperty::NOTE);
        return true;
    }

    if (std::strcmp(scenario, "seq-variation-pitch") == 0) {
        prepareSequencerVariationScenario(state, core::state::sequencer::StepProperty::NOTE);
        return true;
    }

    if (std::strcmp(scenario, "seq-variation-velocity") == 0) {
        prepareSequencerVariationScenario(state, core::state::sequencer::StepProperty::VELOCITY);
        return true;
    }

    if (std::strcmp(scenario, "seq-variation-gate") == 0) {
        prepareSequencerVariationScenario(state, core::state::sequencer::StepProperty::GATE);
        return true;
    }

    if (std::strcmp(scenario, "seq-variation-nudge") == 0) {
        prepareSequencerVariationScenario(state, core::state::sequencer::StepProperty::NUDGE);
        return true;
    }

    if (std::strcmp(scenario, "settings") == 0) {
        state.overlays.show(core::ui::OverlayType::GLOBAL_SETTINGS, false);
        state.globalSettings.openOverlay();
        return true;
    }

    if (std::strcmp(scenario, "data-manager") == 0) {
        state.overlays.show(core::ui::OverlayType::DATA_MANAGER, false);
        state.dataManager.openSession(
            state.activeView.get() == core::ui::ViewType::SEQUENCER
                ? core::state::DataManagerContext::SEQUENCER
                : core::state::DataManagerContext::MACRO
        );
        return true;
    }

    if (std::strcmp(scenario, "data-manager-dialog") == 0) {
        state.overlays.show(core::ui::OverlayType::DATA_MANAGER, false);
        state.dataManager.openSession(core::state::DataManagerContext::MACRO);
        state.overlays.show(core::ui::OverlayType::DATA_MANAGER_DIALOG, true);
        state.dataManager.showDialog(core::state::DataManagerDialogMode::COMMAND_PALETTE, 0);
        return true;
    }

    std::fprintf(stderr, "Unknown capture scenario: %s\n", scenario);
    return false;
}

void tickFrames(::sdl::SdlEnvironment& env,
                oc::app::OpenControlApp& app,
                core::state::CoreState& state,
                int frames) {
    if (frames <= 0) frames = 1;
    for (int i = 0; i < frames; ++i) {
        env.processEvents();
        app.update();
        state.update();
        env.refresh();
        SDL_Delay(16);
    }
}

::sdl::ScreenshotScope captureScopeFromArg(const char* value) {
    if (value && std::strcmp(value, "screen") == 0) {
        return ::sdl::ScreenshotScope::Screen;
    }
    return ::sdl::ScreenshotScope::Controller;
}

}  // namespace sdl::integration
