#include "integration/CaptureScenarios.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <SDL2/SDL.h>

#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "state/DataManagerCatalog.hpp"
#include "state/macro/MacroAutomationDomain.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"

namespace sdl::integration {

namespace {

uint16_t curvePointTick(float beat, uint16_t durationTicks) {
    const float finiteBeat = std::isfinite(beat) ? std::max(beat, 0.0f) : 0.0f;
    return static_cast<uint16_t>(std::clamp<long>(
        std::lround(
            finiteBeat * static_cast<float>(
                core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
            )
        ),
        0L,
        durationTicks
    ));
}

void publishVariationTelemetry(
    core::state::CoreState& state,
    const oc::note::sequencer::StepSequencerResolvedVariation& variation
) {
    state.sequencer.lastResolvedVariation = variation;
    state.sequencer.cycleVariationTelemetry.reset();
    state.sequencer.cycleVariationTelemetry.cycleIndex = variation.cycleIndex;
    state.sequencer.cycleVariationTelemetry.ranges = variation.ranges;
    state.sequencer.cycleVariationTelemetry.scaleSettings = variation.scaleSettings;
    state.sequencer.cycleVariationTelemetry.store(variation);
    state.sequencer.variationTelemetryRevision.set(
        state.sequencer.variationTelemetryRevision.get() + 1U
    );
}

void prepareSequencerVariationScenario(core::state::CoreState& state,
                                       core::state::sequencer::StepProperty property) {
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.activeStepProperty.set(property);
    state.sequencer.setStepDataAt(0, 60, 100, 75, 0);
    if (!state.sequencer.pattern.isEnabled(0)) {
        state.sequencer.pattern.toggle(0);
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

    publishVariationTelemetry(state, variation);
}

void prepareSequencerScaleScenario(
    core::state::CoreState& state,
    oc::note::sequencer::StepSequencerScaleConstraintMode mode
) {
    using namespace oc::note::sequencer;

    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.activeStepProperty.set(core::state::sequencer::StepProperty::NOTE);
    state.sequencer.setPitchEditMode(core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES);
    state.sequencer.setStepDataAt(0, 61, 100, 75, 0);
    if (!state.sequencer.pattern.isEnabled(0)) {
        state.sequencer.pattern.toggle(0);
    }

    StepSequencerScaleSettings settings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = mode,
    };
    state.sequencerTracks.setProjectScaleSettings(settings);
    state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );

    const StepSequencerVariationRanges ranges{};
    const auto variation = resolveStepVariation(
        StepSequencerStepValues{.note = 61, .velocity = 100, .gate = 75, .nudge = 0},
        ranges,
        settings,
        core::state::sequencer::SequencerState::MAX_GATE_PERCENT,
        0U,
        1U,
        0U,
        true
    );
    publishVariationTelemetry(state, variation);
}

void enableSequencerStep(core::state::sequencer::SequencerState& sequencer, uint8_t step) {
    if (!sequencer.pattern.isEnabled(step)) {
        sequencer.pattern.toggle(step);
    }
}

void offsetFirstMicroStep(core::state::sequencer::SequencerState& sequencer,
                          uint16_t sequenceId,
                          int8_t noteOffset) {
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    const auto* sequence = graph ? graph->sequence(sequenceId) : nullptr;
    if (sequence == nullptr) return;
    core::state::sequencer::setNodeNoteOffset(
        sequencer.pattern,
        sequence->firstStepNode,
        noteOffset
    );
}

void offsetFirstCycleState(core::state::sequencer::SequencerState& sequencer,
                           uint16_t cycleSetId,
                           int8_t noteOffset) {
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    const auto* cycleSet = graph ? graph->cycleSet(cycleSetId) : nullptr;
    if (cycleSet == nullptr) return;
    core::state::sequencer::setNodeNoteOffset(
        sequencer.pattern,
        cycleSet->firstStateNode,
        noteOffset
    );
}

void prepareMacroAutomationCleanScenario(core::state::CoreState& state) {
    state.activeView.set(core::ui::ViewType::MACRO);
    state.overlays.hideAll();
    state.pages.initDefaults();
    state.pages.control.clear();
    state.macroUi.reset();
    state.macroEdit.reset();
    state.trackNavigation.reset();
    state.structureClipboard.clear();
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
}

core::state::modulation::ModulatorId addReusableLfo(
    core::state::CoreState& state,
    const char* name,
    core::state::modulation::ModulatorLfoShape shape,
    uint32_t periodTicks,
    uint8_t accent
) {
    using namespace core::state::modulation;
    ModulatorLfoDraft draft{};
    draft.name = name;
    draft.reach = {.kind = ModulatorReachKind::PROJECT};
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

void prepareMacroReusableModulatorsScenario(core::state::CoreState& state) {
    using namespace core::state::modulation;
    prepareMacroAutomationCleanScenario(state);
    state.setSharedTrackState(0x0003, 0);

    auto& track = state.pages.tracks[0];
    track.channel = 5;
    track.activePage = 0;
    track.enabledPageMask = 0x0001;
    auto& page = track.pages[0];
    page.cc[0] = 74;
    page.values[0] = 0.42f;
    page.setMacroActive(0, true);
    std::snprintf(page.name, sizeof(page.name), "%s", "Reusable LFOs");

    auto& remoteTrack = state.pages.tracks[1];
    remoteTrack.channel = 6;
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
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::TRACK);

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
    state.statusBar.pageName.set(state.pages.activePageData().name);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
}

core::state::modulation::ModulationBindingId bindReusableModulator(
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

void prepareMacroMultiModulationScenario(core::state::CoreState& state) {
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

void prepareMacroPerformanceRailScenario(core::state::CoreState& state) {
    prepareMacroAutomationCleanScenario(state);
    state.setSharedTrackState(0x0001, 0);

    auto& track = state.pages.tracks[0];
    track.channel = 5;
    track.activePage = 0;
    track.enabledPageMask = 0x0001;
    auto& page = track.pages[0];
    page.cc[0] = 74;
    page.values[0] = 0.35f;
    page.setMacroActive(0, true);
    std::snprintf(page.name, sizeof(page.name), "%s", "Mod Rail");

    state.pages.syncSharedTrackState(0x0001, 0);
    state.pages.setActivePage(0);
    state.macroUi.syncPreviewPage(0);
    state.trackNavigation.syncPreviewTrack(0);
    state.structureNavigationFocus.set(
        core::state::StructureNavigationFocus::TRACK
    );

    const auto source = addReusableLfo(
        state,
        "Bound Probe",
        core::state::modulation::ModulatorLfoShape::SQUARE,
        core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT * 32U,
        0
    );
    if (core::state::modulation::valid(source)) {
        // Leave enough headroom for the hardware-equivalent encoder mapper to
        // recover inside both bounds for either square-wave polarity.
        (void)bindReusableModulator(state, source, 12000);
        state.pages.control.markAuthoredMutation();
    }
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
        state.macros,
        state.pages
    );
    state.statusBar.pageName.set(state.pages.activePageData().name);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(
        state.configRevision.get()
    ));
}

void prepareMacroInitialProjectionScenario(core::state::CoreState& state) {
    prepareMacroMultiModulationScenario(state);
    auto& graph = state.pages.control.authored.modulation;
    if (graph.sourceCount < 2U) return;

    auto& track0 = state.pages.tracks[0];
    track0.setPageEnabled(1, true);
    track0.pages[1].values[0] = 0.68f;
    track0.pages[1].cc[0] = 71;
    track0.pages[1].setMacroActive(0, true);
    std::snprintf(
        track0.pages[1].name,
        sizeof(track0.pages[1].name),
        "%s",
        "Projection P2"
    );

    auto& track1 = state.pages.tracks[1];
    track1.channel = 6;
    track1.activePage = 0;
    track1.setPageEnabled(0, true);
    track1.pages[0].values[0] = 0.24f;
    track1.pages[0].cc[0] = 10;
    track1.pages[0].setMacroActive(0, true);
    std::snprintf(
        track1.pages[0].name,
        sizeof(track1.pages[0].name),
        "%s",
        "Projection T2"
    );

    (void)bindReusableModulator(
        state,
        graph.sources[0].id,
        9830,
        0,
        1,
        0
    );
    (void)bindReusableModulator(
        state,
        graph.sources[1].id,
        -6553,
        1,
        0,
        0
    );
    state.pages.control.markAuthoredMutation();
    state.setSharedTrackState(0x0003U, 0);
    state.pages.setActivePage(0);
    state.macroUi.syncPreviewPage(0);
    state.trackNavigation.syncPreviewTrack(0);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
        state.macros,
        state.pages
    );
    state.configRevision.set(
        core::state::macro::nextMacroConfigRevision(
            state.configRevision.get()
        )
    );
}

void prepareProjectModulatorsScenario(core::state::CoreState& state) {
    using namespace core::state::modulation;
    prepareMacroReusableModulatorsScenario(state);
    auto& page = state.pages.tracks[0].pages[0];
    auto& track2Page = state.pages.tracks[1].pages[0];
    page.cc[1] = 71;
    page.cc[2] = 10;
    page.values[1] = 0.58f;
    page.values[2] = 0.33f;
    page.setMacroActive(1, true);
    page.setMacroActive(2, true);
    track2Page.cc[1] = 74;
    track2Page.values[1] = 0.46f;
    track2Page.setMacroActive(1, true);

    auto& graph = state.pages.control.authored.modulation;
    if (graph.sourceCount >= 2U) {
        graph.sources[0].reach = projectModulatorGlobalReach();
        graph.sources[1].reach = projectModulatorGlobalReach();
        // The reusable-Macro scenario seeds one remote use. This Project
        // scenario authors its own exact two-destination shared graph.
        for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
            const auto& candidate = graph.outputBindings[index];
            if (candidate.sourceId == graph.sources[0].id &&
                candidate.destination == projectControlDestination(
                    {1U, 0U, 0U}
                )) {
                (void)removeProjectModulationBinding(graph, candidate.id);
                break;
            }
        }
        const auto addBinding = [&](ModulatorId sourceId,
                                    uint8_t track,
                                    uint8_t macro,
                                    int16_t depthQ15) {
            ModulationBindingDraft draft{};
            draft.sourceId = sourceId;
            draft.destination = projectControlDestination({
                .track = track,
                .page = 0,
                .macro = macro,
            });
            draft.amountQ15 = depthQ15;
            draft.application = ModulationApplication::NATURAL;
            draft.enabled = true;
            return addProjectModulationBinding(graph, draft).changed();
        };
        (void)addBinding(graph.sources[0].id, 0, 0, 8192);
        (void)addBinding(graph.sources[0].id, 1, 1, -4096);
        (void)addBinding(graph.sources[1].id, 0, 2, 2621);
        state.pages.control.markAuthoredMutation();
    }

    state.macroHistory.clear();
    state.structureClipboard.clear();
    state.projectNavigation.reset();
    state.activeView.set(core::ui::ViewType::PROJECT);
    state.overlays.hideAll();
    (void)state.setSharedTrackState(0x0003U, 0U);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(
        state.configRevision.get()
    ));
}

void prepareProjectModulatorDestinationScenario(core::state::CoreState& state) {
    state.pages.initDefaults();
    state.pages.control.clear();
    auto& page = state.pages.pageData(0, 0);
    page.cc[0] = 74;
    page.values[0] = 0.42f;
    page.setMacroActive(0, true);
    state.pages.updateActiveConfigs();
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
        state.macros,
        state.pages
    );
    state.macroHistory.clear();
    state.structureClipboard.clear();
    state.projectNavigation.reset();
    state.activeView.set(core::ui::ViewType::PROJECT);
    state.overlays.hideAll();
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(
        state.configRevision.get()
    ));
}

void prepareProjectModulatorWorkspaceScenario(core::state::CoreState& state) {
    using namespace core::state::modulation;
    prepareProjectModulatorsScenario(state);
    constexpr std::array<ProjectPackedCurvePoint, 5> points{{
        {0U, -24576},
        {96U, 8192},
        {192U, 28672},
        {288U, -4096},
        {384U, -24576},
    }};
    RecordedShapeDraft draft{};
    draft.name = "Breath Arc";
    draft.reach = projectModulatorGlobalReach();
    draft.curve = {
        .sourceDurationTicks = 384U,
        .durationTicks = 384U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    auto& control = state.pages.control;
    const auto created = createRecordedShapeModulator(
        control.authored.modulation,
        control.authored.curves,
        draft
    );
    if (created.changed()) {
        ModulationBindingDraft binding{};
        binding.sourceId = created.sourceId;
        binding.destination = projectControlDestination({0U, 0U, 1U});
        binding.amountQ15 = 6553;
        binding.application = ModulationApplication::NATURAL;
        (void)addProjectModulationBinding(
            control.authored.modulation,
            binding
        );
        control.markAuthoredMutation();
    }
    state.projectNavigation.notifyContentChanged();
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(
        state.configRevision.get()
    ));
}

void prepareMacroModulationAssignmentCopyScenario(
    core::state::CoreState& state
) {
    prepareMacroReusableModulatorsScenario(state);
    auto& page = state.pages.tracks[0].pages[0];
    page.cc[1] = 71;
    page.values[1] = 0.58f;
    page.setMacroActive(1, true);
    auto& graph = state.pages.control.authored.modulation;
    if (graph.sourceCount == 0U) return;
    const auto binding = bindReusableModulator(
        state,
        graph.sources[0].id,
        -13107
    );
    state.pages.control.markAuthoredMutation();
    if (core::state::modulation::valid(binding)) {
        (void)core::state::modulation::setProjectControlFocusedModulationBinding(
            state.pages.control,
            {.track = 0, .page = 0, .macro = 0},
            binding
        );
    }
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
        state.macros,
        state.pages
    );
    state.configRevision.set(
        core::state::macro::nextMacroConfigRevision(
            state.configRevision.get()
        )
    );
}

void configureMacroAutomation(core::state::CoreState& state,
                              uint8_t track,
                              uint8_t page,
                              uint8_t macro,
                              float value,
                              float durationBeats = 2.0f) {
    core::state::macro::MacroAutomationLane lane;
    lane.durationBeats = durationBeats;
    if (!core::state::macro::macroAutomationAppendPoint(lane, 0.0f, value)) return;
    if (!core::state::macro::macroAutomationAppendPoint(lane, durationBeats, value)) return;
    (void)core::state::modulation::assignProjectControlAutomation(
        state.pages.control,
        {.track = track, .page = page, .macro = macro},
        lane
    );
}

void configureMacroModulation(core::state::CoreState& state,
                              uint8_t track,
                              uint8_t page,
                              uint8_t macro,
                              float depth = 0.65f) {
    core::state::macro::MacroModulationShape shape;
    shape.durationBeats = 4.0f;
    if (!core::state::macro::macroModulationAppendPoint(shape, 0.0f, -0.25f)) return;
    if (!core::state::macro::macroModulationAppendPoint(shape, 2.0f, 0.35f)) return;
    if (!core::state::macro::macroModulationAppendPoint(shape, 4.0f, -0.25f)) return;
    const uint16_t durationTicks =
        core::state::macro::macroAutomationTicksFromBeats(shape.durationBeats);
    std::array<
        core::state::macro::MacroPackedCurvePoint,
        core::state::macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS> points{};
    for (uint16_t index = 0; index < shape.pointCount; ++index) {
        points[index] = {
            .tick = curvePointTick(shape.points[index].beat, durationTicks),
            .value = core::state::macro::macroAutomationPackValue(
                shape.points[index].value,
                true
            ),
        };
    }
    const core::state::macro::MacroAutomationCurveRef curve{
        .active = true,
        .playbackState = core::state::macro::MacroCurvePlaybackState::ACTIVE,
        .pointOffset = 0,
        .pointCount = shape.pointCount,
        .sourceDurationTicks = durationTicks,
        .durationTicks = durationTicks,
        .windowOffsetTicks = 0,
        .interpolation = shape.interpolation,
        .modulationOrigin = core::state::macro::MacroModulationOrigin::NATIVE,
    };
    (void)core::state::modulation::replaceProjectControlModulation(
        state.pages.control,
        {.track = track, .page = page, .macro = macro},
        curve,
        core::state::macro::macroAutomationClamp01(depth),
        points.data(),
        shape.pointCount
    );
}

void prepareMacroAutoModScenario(core::state::CoreState& state) {
    prepareMacroAutomationCleanScenario(state);
    state.setSharedTrackState(0x0001, 0);

    auto& track = state.pages.tracks[0];
    track.channel = 5;
    track.activePage = 0;
    track.enabledPageMask = 0x0001;
    auto& page = track.pages[0];
    page.cc[0] = 74;
    page.values[0] = 0.42f;
    page.setMacroActive(0, true);
    std::snprintf(page.name, sizeof(page.name), "%s", "Auto + Mod");

    state.pages.syncSharedTrackState(0x0001, 0);
    state.pages.setActivePage(0);
    state.macroUi.syncPreviewPage(0);
    state.trackNavigation.syncPreviewTrack(0);
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    configureMacroAutomation(state, 0, 0, 0, 0.42f, 4.0f);
    configureMacroModulation(state, 0, 0, 0, 0.65f);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
}

void prepareMacroCurvePreviewShapesScenario(core::state::CoreState& state) {
    using namespace core::state::modulation;
    prepareMacroAutomationCleanScenario(state);
    state.setSharedTrackState(0x0001, 0);

    auto& track = state.pages.tracks[0];
    track.channel = 5;
    track.activePage = 0;
    track.enabledPageMask = 0x0001;
    auto& page = track.pages[0];
    std::snprintf(page.name, sizeof(page.name), "%s", "Curve Preview");

    constexpr std::array<ModulatorLfoShape, 4> SHAPES = {
        ModulatorLfoShape::SINE,
        ModulatorLfoShape::TRIANGLE,
        ModulatorLfoShape::SAW_UP,
        ModulatorLfoShape::SQUARE,
    };
    constexpr std::array<const char*, 4> NAMES = {
        "Sine",
        "Triangle",
        "Saw",
        "Square",
    };
    for (uint8_t index = 0U; index < SHAPES.size(); ++index) {
        page.cc[index] = static_cast<uint8_t>(70U + index);
        page.values[index] = 0.5f;
        page.setMacroActive(index, true);
        const auto sourceId = addReusableLfo(
            state,
            NAMES[index],
            SHAPES[index],
            PROJECT_CONTROL_TICKS_PER_BEAT * 2U,
            static_cast<uint8_t>(index % 3U)
        );
        if (valid(sourceId)) {
            (void)bindReusableModulator(
                state,
                sourceId,
                24576,
                0,
                0,
                index
            );
        }
    }
    page.cc[4] = 74U;
    page.values[4] = 0.5f;
    page.setMacroActive(4, true);
    configureMacroModulation(state, 0, 0, 4, 0.75f);

    state.pages.control.markAuthoredMutation();
    state.pages.syncSharedTrackState(0x0001, 0);
    state.pages.setActivePage(0);
    state.macroUi.syncPreviewPage(0);
    state.trackNavigation.syncPreviewTrack(0);
    state.structureNavigationFocus.set(
        core::state::StructureNavigationFocus::TRACK
    );
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
        state.macros,
        state.pages
    );
    state.statusBar.pageName.set(state.pages.activePageData().name);
    state.configRevision.set(
        core::state::macro::nextMacroConfigRevision(
            state.configRevision.get()
        )
    );
}

void configureMacroAutomationShape(core::state::CoreState& state,
                                   uint8_t track,
                                   uint8_t page,
                                   uint8_t macro) {
    core::state::macro::MacroAutomationLane lane;
    lane.durationBeats = 8.0f;
    if (!core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.15f)) return;
    if (!core::state::macro::macroAutomationAppendPoint(lane, 2.0f, 0.90f)) return;
    if (!core::state::macro::macroAutomationAppendPoint(lane, 5.0f, 0.25f)) return;
    if (!core::state::macro::macroAutomationAppendPoint(lane, 8.0f, 0.70f)) return;
    (void)core::state::modulation::assignProjectControlAutomation(
        state.pages.control,
        {.track = track, .page = page, .macro = macro},
        lane
    );
}

void prepareMacroAutomationCurveShapeScenario(core::state::CoreState& state) {
    prepareMacroAutomationCleanScenario(state);
    state.setSharedTrackState(0x0001, 0);

    auto& track = state.pages.tracks[0];
    track.channel = 5;
    track.activePage = 0;
    track.enabledPageMask = 0x0001;
    track.pages[0].cc[0] = 21;
    track.pages[0].setMacroActive(0, true);
    std::snprintf(track.pages[0].name, sizeof(track.pages[0].name), "%s", "Curve P1");

    state.pages.syncSharedTrackState(0x0001, 0);
    state.pages.setActivePage(0);
    state.macroUi.syncPreviewPage(0);
    state.trackNavigation.syncPreviewTrack(0);
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    configureMacroAutomationShape(state, 0, 0, 0);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
}

void prepareMacroTrackMultipageAutomationScenario(core::state::CoreState& state) {
    prepareMacroAutomationCleanScenario(state);
    state.setSharedTrackState(0x0001, 0);

    auto& sourceTrack = state.pages.tracks[0];
    sourceTrack.channel = 5;
    sourceTrack.activePage = 2;
    sourceTrack.enabledPageMask = 0x0005;
    sourceTrack.pages[0].cc[0] = 21;
    sourceTrack.pages[2].cc[0] = 84;
    sourceTrack.pages[2].cc[1] = 85;
    sourceTrack.pages[2].setMacroActive(1, true);
    std::snprintf(sourceTrack.pages[0].name, sizeof(sourceTrack.pages[0].name), "%s", "Source P1");
    std::snprintf(sourceTrack.pages[2].name, sizeof(sourceTrack.pages[2].name), "%s", "Source P3");

    state.pages.syncSharedTrackState(0x0001, 0);
    state.pages.setActivePage(2);
    state.macroUi.syncPreviewPage(2);
    state.trackNavigation.syncPreviewTrack(0);
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::TRACK);
    configureMacroAutomation(state, 0, 0, 0, 0.25f, 4.0f);
    configureMacroAutomation(state, 0, 2, 1, 0.80f, 8.0f);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));
}

void prepareSequencerSemanticGridScenario(core::state::CoreState& state) {
    using namespace oc::note::sequencer;

    state.sequencer.reset();
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.pattern.length.set(8);
    state.sequencer.page.set(0);
    state.sequencer.focusedStep.set(0);
    state.sequencer.activeStepProperty.set(core::state::sequencer::StepProperty::NOTE);
    state.sequencerTracks.setProjectScaleSettings(StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Chromatic,
        .mode = StepSequencerScaleConstraintMode::Free,
    });
    state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );

    state.sequencer.setStepDataAt(0, 60, 104, 70, 0, 100);
    state.sequencer.setStepDataAt(1, 62, 96, 80, 0, 65);
    state.sequencer.setStepDataAt(2, 64, 100, 75, 0, 100);
    state.sequencer.setStepDataAt(3, 65, 100, 75, 0, 100);
    state.sequencer.setStepDataAt(4, 67, 110, 90, -10, 70);
    state.sequencer.setStepDataAt(5, 69, 84, 45, -18, 100);
    state.sequencer.setStepDataAt(6, 71, 122, 120, 12, 100);

    for (uint8_t step = 0; step < 7; ++step) {
        enableSequencerStep(state.sequencer, step);
    }

    const auto micro = core::state::sequencer::createMicroSequence(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(2),
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    if (micro.ok) {
        offsetFirstMicroStep(state.sequencer, micro.id, 3);
    }

    const auto cycle = core::state::sequencer::createCycleStateSet(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(3),
        core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT
    );
    if (cycle.ok) {
        offsetFirstCycleState(state.sequencer, cycle.id, 5);
    }

    const auto nestedMicro = core::state::sequencer::createMicroSequence(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(4),
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    if (nestedMicro.ok) {
        offsetFirstMicroStep(state.sequencer, nestedMicro.id, 4);
    }
    const auto nestedCycle = core::state::sequencer::createCycleStateSet(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(4),
        core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT
    );
    if (nestedCycle.ok) {
        offsetFirstCycleState(state.sequencer, nestedCycle.id, -2);
    }
}

void prepareSequencerLocalRandomGridScenario(core::state::CoreState& state) {
    using namespace oc::note::sequencer;
    using core::state::sequencer::StepProperty;

    state.sequencer.reset();
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.pattern.length.set(8);
    state.sequencer.page.set(0);
    state.sequencer.focusedStep.set(0);
    state.sequencer.activeStepProperty.set(StepProperty::NOTE);
    state.sequencerTracks.setProjectScaleSettings(StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Chromatic,
        .mode = StepSequencerScaleConstraintMode::Free,
    });
    state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );

    state.sequencer.setStepDataAt(0, 60, 104, 75, 0, 100);
    state.sequencer.setStepDataAt(1, 62, 96, 75, 0, 100);
    state.sequencer.setStepDataAt(2, 64, 100, 75, 0, 100);
    state.sequencer.setStepDataAt(3, 65, 84, 70, 0, 100);

    for (uint8_t step = 0; step < 4; ++step) {
        enableSequencerStep(state.sequencer, step);
    }

    core::state::sequencer::setNodeLocalVariationRange(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(0),
        StepProperty::NOTE,
        5
    );

    const auto cycle = core::state::sequencer::createCycleStateSet(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(1),
        2
    );
    if (cycle.ok) {
        const auto* graph = core::state::sequencer::graphView(state.sequencer.pattern);
        const auto* cycleSet = graph ? graph->cycleSet(cycle.id) : nullptr;
        if (cycleSet != nullptr) {
            const auto stateNode = cycleSet->firstStateNode;
            core::state::sequencer::setNodeNoteOffset(state.sequencer.pattern, stateNode, 2);
            core::state::sequencer::setNodeLocalVariationRange(
                state.sequencer.pattern,
                stateNode,
                StepProperty::NOTE,
                6
            );
        }
    }

    const auto micro = core::state::sequencer::createMicroSequence(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(2),
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    if (micro.ok) {
        const auto* graph = core::state::sequencer::graphView(state.sequencer.pattern);
        const auto* sequence = graph ? graph->sequence(micro.id) : nullptr;
        if (sequence != nullptr) {
            const auto microNode = sequence->firstStepNode;
            core::state::sequencer::setNodeNoteOffset(state.sequencer.pattern, microNode, 3);
            core::state::sequencer::setNodeLocalVariationRange(
                state.sequencer.pattern,
                microNode,
                StepProperty::NOTE,
                4
            );
        }
    }

    state.sequencer.probabilityCycleIndex = 0;
}

void setAllLocalVariationRanges(
    core::state::sequencer::SequencerPatternState& pattern,
    core::state::sequencer::SequencerGraphNodeId nodeId,
    const oc::note::sequencer::StepSequencerVariationRanges& ranges
) {
    using core::state::sequencer::StepProperty;

    core::state::sequencer::setNodeLocalVariationRange(
        pattern,
        nodeId,
        StepProperty::NOTE,
        ranges.pitchSemitones
    );
    core::state::sequencer::setNodeLocalVariationRange(
        pattern,
        nodeId,
        StepProperty::VELOCITY,
        ranges.velocity
    );
    core::state::sequencer::setNodeLocalVariationRange(
        pattern,
        nodeId,
        StepProperty::GATE,
        ranges.gatePercent
    );
    core::state::sequencer::setNodeLocalVariationRange(
        pattern,
        nodeId,
        StepProperty::NUDGE,
        ranges.nudge
    );
}

void prepareSequencerSummedLocalRandomScenario(
    core::state::CoreState& state,
    core::state::sequencer::StepProperty activeProperty
) {
    using namespace oc::note::sequencer;
    using core::state::sequencer::StepProperty;

    state.sequencer.reset();
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.pattern.length.set(8);
    state.sequencer.page.set(0);
    state.sequencer.focusedStep.set(0);
    state.sequencer.activeStepProperty.set(activeProperty);
    state.sequencerTracks.setProjectScaleSettings(StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Chromatic,
        .mode = StepSequencerScaleConstraintMode::Free,
    });
    state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );
    state.sequencer.setPatternVariationRanges({
        .pitchSemitones = 2,
        .velocity = 12,
        .gatePercent = 18,
        .nudge = 5,
    });

    state.sequencer.setStepDataAt(0, 60, 80, 70, 0, 100);
    state.sequencer.setStepDataAt(1, 62, 92, 85, 0, 100);
    state.sequencer.setStepDataAt(2, 64, 72, 60, -8, 100);
    state.sequencer.setStepDataAt(3, 65, 108, 95, 10, 100);

    for (uint8_t step = 0; step < 4; ++step) {
        enableSequencerStep(state.sequencer, step);
    }

    setAllLocalVariationRanges(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(0),
        {
            .pitchSemitones = 4,
            .velocity = 18,
            .gatePercent = 14,
            .nudge = 7,
        }
    );

    const auto cycle = core::state::sequencer::createCycleStateSet(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(1),
        2
    );
    if (cycle.ok) {
        const auto* graph = core::state::sequencer::graphView(state.sequencer.pattern);
        const auto* cycleSet = graph ? graph->cycleSet(cycle.id) : nullptr;
        if (cycleSet != nullptr) {
            const auto stateNode = cycleSet->firstStateNode;
            core::state::sequencer::setNodeNoteOffset(state.sequencer.pattern, stateNode, 2);
            core::state::sequencer::setNodeVelocityOffset(state.sequencer.pattern, stateNode, -8);
            core::state::sequencer::setNodeGateOffset(state.sequencer.pattern, stateNode, -12);
            core::state::sequencer::setNodeNudgeOffset(state.sequencer.pattern, stateNode, 5);
            setAllLocalVariationRanges(
                state.sequencer.pattern,
                stateNode,
                {
                    .pitchSemitones = 5,
                    .velocity = 16,
                    .gatePercent = 12,
                    .nudge = 6,
                }
            );
        }
    }

    const auto micro = core::state::sequencer::createMicroSequence(
        state.sequencer.pattern,
        core::state::sequencer::rootStepNodeId(2),
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    if (micro.ok) {
        const auto* graph = core::state::sequencer::graphView(state.sequencer.pattern);
        const auto* sequence = graph ? graph->sequence(micro.id) : nullptr;
        if (sequence != nullptr) {
            const auto microNode = sequence->firstStepNode;
            core::state::sequencer::setNodeNoteOffset(state.sequencer.pattern, microNode, -3);
            core::state::sequencer::setNodeVelocityOffset(state.sequencer.pattern, microNode, 10);
            core::state::sequencer::setNodeGateOffset(state.sequencer.pattern, microNode, 16);
            core::state::sequencer::setNodeNudgeOffset(state.sequencer.pattern, microNode, -6);
            setAllLocalVariationRanges(
                state.sequencer.pattern,
                microNode,
                {
                    .pitchSemitones = 3,
                    .velocity = 20,
                    .gatePercent = 15,
                    .nudge = 9,
                }
            );
        }
    }

    state.sequencer.probabilityCycleIndex = 0;
}

void prepareSequencerNestedLocalRandomRuntimeScenario(core::state::CoreState& state) {
    using namespace oc::note::sequencer;
    using core::state::sequencer::StepProperty;

    state.sequencer.reset();
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.sequencer.pattern.length.set(4);
    state.sequencer.page.set(0);
    state.sequencer.focusedStep.set(0);
    state.sequencer.activeStepProperty.set(StepProperty::NOTE);
    state.sequencer.setPitchEditMode(core::state::sequencer::SequencerPitchEditMode::SCALE_DEGREES);
    state.sequencerTracks.setProjectScaleSettings(StepSequencerScaleSettings{
        .root = 0,
        .type = StepSequencerScaleType::Major,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    });
    state.sequencer.setPatternScalePolicy(
        core::state::sequencer::SequencerPatternScalePolicy::INHERIT_PROJECT
    );
    state.sequencer.setPatternVariationRanges({
        .pitchSemitones = 2,
        .velocity = 10,
        .gatePercent = 12,
        .nudge = 4,
    });

    state.sequencer.setStepDataAt(0, 60, 96, 85, 0, 100);
    enableSequencerStep(state.sequencer, 0);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto cycle = core::state::sequencer::createCycleStateSet(
        state.sequencer.pattern,
        rootNode,
        2
    );
    if (!cycle.ok) return;

    core::state::sequencer::setCycleStateSetOffset(state.sequencer.pattern, cycle.id, -1);
    const auto* graph = core::state::sequencer::graphView(state.sequencer.pattern);
    const auto* cycleSet = graph ? graph->cycleSet(cycle.id) : nullptr;
    if (cycleSet == nullptr) return;

    const auto activeStateNode = static_cast<core::state::sequencer::SequencerGraphNodeId>(
        cycleSet->firstStateNode + 1U
    );
    core::state::sequencer::setNodeNoteOffset(state.sequencer.pattern, activeStateNode, 1);
    core::state::sequencer::setNodeVelocityOffset(state.sequencer.pattern, activeStateNode, -6);
    setAllLocalVariationRanges(
        state.sequencer.pattern,
        activeStateNode,
        {
            .pitchSemitones = 6,
            .velocity = 18,
            .gatePercent = 10,
            .nudge = 5,
        }
    );

    const auto micro = core::state::sequencer::createMicroSequence(
        state.sequencer.pattern,
        activeStateNode,
        2
    );
    if (micro.ok) {
        graph = core::state::sequencer::graphView(state.sequencer.pattern);
        const auto* sequence = graph ? graph->sequence(micro.id) : nullptr;
        if (sequence != nullptr) {
            core::state::sequencer::setNodeNoteOffset(
                state.sequencer.pattern,
                sequence->firstStepNode,
                0
            );
            core::state::sequencer::setNodeNoteOffset(
                state.sequencer.pattern,
                static_cast<core::state::sequencer::SequencerGraphNodeId>(
                    sequence->firstStepNode + 1U
                ),
                2
            );
        }
    }

    core::state::sequencer::enterCycleStatesContentView(
        state.sequencer,
        rootNode,
        cycle.id
    );
    state.sequencer.probabilityCycleIndex = 0;
}

void prepareStepPresetCaptureBase(
    core::state::CoreState& state,
    core::state::sequencer::SequencerStepPresetPickerMode mode =
        core::state::sequencer::SequencerStepPresetPickerMode::LOAD
) {
    using namespace core::state::sequencer;
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.overlays.hideAll();
    state.sequencer.stepEdit.visible.set(true);
    state.sequencer.stepEdit.stepIndex.set(5);
    state.sequencer.focusedStep.set(5);
    state.overlays.show(core::ui::OverlayType::SEQ_STEP_EDIT, false);

    auto& picker = state.sequencer.stepPresetPicker;
    picker.reset();
    picker.open(mode);
    picker.frozenTarget.valid = true;
    picker.frozenTarget.trackIndex = 0;
    picker.frozenTarget.stepIndex = 5;
    picker.frozenTarget.projectRevision = state.project.metadata.modifiedCounter;
    std::snprintf(
        picker.frozenTarget.contextLabel,
        sizeof(picker.frozenTarget.contextLabel),
        "%s",
        "Track 1 - Step 06"
    );
    picker.setEntry(0, "orbit-a", "Orbit", true);
    picker.setEntry(1, "orbit-b", "Orbit", true);
    picker.setEntry(2, "pulse-grid", "Pulse Grid", true);
    picker.setEntry(3, "legacy-shape", nullptr, false);
    picker.setEntry(4, "glass-cycle", "Glass Cycle", true);
    picker.setEntry(5, "human-hat", "Human Hat", true);
    picker.entryCount.set(6);
    picker.totalEntryCount.set(23);
    picker.truncated.set(true);
    picker.hasNextPage.set(true);
    picker.selectedIndex.set(mode == SequencerStepPresetPickerMode::SAVE ? 1 : 0);

    auto& descriptor = picker.descriptor;
    descriptor.valid = true;
    std::snprintf(
        descriptor.semanticName,
        sizeof(descriptor.semanticName),
        "%s",
        "Orbit"
    );
    std::snprintf(
        descriptor.technicalId,
        sizeof(descriptor.technicalId),
        "%s",
        "orbit-a"
    );
    descriptor.contentFlags = STEP_PRESET_CONTENT_STEP_VALUES |
        STEP_PRESET_CONTENT_GRAPH | STEP_PRESET_CONTENT_MICRO_SEQUENCE |
        STEP_PRESET_CONTENT_CYCLE;
    descriptor.stepNodeCount = 9;
    descriptor.sequenceCount = 2;
    descriptor.cycleSetCount = 1;
    descriptor.scalePolicy = SequencerStepPresetScalePolicy::SCALE_RELATIVE;
    descriptor.mixedPitchPolicy = true;
    descriptor.adaptation = SequencerStepPresetAdaptation::DESTINATION_SCALE;
    descriptor.footprint = SequencerStepPresetFootprint::REPLACE;
    descriptor.compatibility = SequencerStepPresetCompatibility::WARNING_ADAPTED;
    descriptor.previewStateIndex = 1;
    descriptor.previewStateCount = 4;
    descriptor.previewNote = 62;
    std::snprintf(
        descriptor.contentSummary,
        sizeof(descriptor.contentSummary),
        "%s",
        "Values + Micro + Cycle"
    );
    std::snprintf(
        descriptor.adaptationSummary,
        sizeof(descriptor.adaptationSummary),
        "%s",
        "C major -> D minor"
    );
    std::snprintf(
        descriptor.replaceFacts,
        sizeof(descriptor.replaceFacts),
        "%s",
        "Step values + child graph"
    );
    std::snprintf(
        descriptor.preserveFacts,
        sizeof(descriptor.preserveFacts),
        "%s",
        "Track route, scale, other steps"
    );
    std::snprintf(
        descriptor.compatibilityReason,
        sizeof(descriptor.compatibilityReason),
        "%s",
        "Pitch follows destination scale"
    );
    std::snprintf(
        descriptor.previewSummary,
        sizeof(descriptor.previewSummary),
        "%s",
        "Preview 2/4 - D4"
    );
    picker.bump();
    state.overlays.show(core::ui::OverlayType::SEQ_STEP_PRESET, true);
}

void setStepPresetOperationFeedback(
    core::state::CoreState& state,
    core::state::contextual::ContextActionId action,
    core::state::contextual::OperationFeedbackStatus status,
    core::state::contextual::ContextActionReason reason
) {
    core::state::contextual::OperationFeedbackState feedback{};
    feedback.active = true;
    feedback.action = action;
    feedback.status = status;
    feedback.reason = reason;
    feedback.expiryPolicy =
        core::state::contextual::OperationFeedbackExpiryPolicy::MANUAL;
    feedback.shownAtMs = SDL_GetTicks();
    state.sequencer.stepPresetPicker.operationFeedback.set(feedback);
    state.sequencer.stepPresetPicker.bump();
}

void prepareStepPresetOperationScenario(
    core::state::CoreState& state,
    core::state::contextual::OperationFeedbackStatus status,
    core::state::contextual::ContextActionReason reason,
    bool saveMode = false
) {
    using namespace core::state::sequencer;
    prepareStepPresetCaptureBase(
        state,
        saveMode ? SequencerStepPresetPickerMode::SAVE
                 : SequencerStepPresetPickerMode::LOAD
    );
    auto& picker = state.sequencer.stepPresetPicker;
    if (saveMode) {
        picker.selectedIndex.set(0);
        picker.descriptor = {};
    }
    setStepPresetOperationFeedback(
        state,
        saveMode ? core::state::contextual::ContextActionId::SAVE
                 : core::state::contextual::ContextActionId::APPLY,
        status,
        reason
    );
}

bool prepareStepPresetActivationScenario(
    core::state::CoreState& state,
    core::state::contextual::OperationFeedbackStatus status,
    core::state::contextual::ContextActionReason reason
) {
    using namespace core::state::sequencer;
    using FeedbackStatus =
        core::state::contextual::OperationFeedbackStatus;
    prepareStepPresetOperationScenario(state, status, reason);
    state.statusBar.playing.set(true);

    auto& queue = state.sequencerTrackActivations;
    queue.reset();
    SequencerTrackActivationBatch batch;
    if (!queue.prepare(
            0x0001,
            0x0001,
            0,
            true,
            batch,
            SequencerTrackActivationOrigin::STEP_PRESET
        ) || !queue.armPrepared(batch)) {
        return false;
    }
    queue.publishPrepared(batch);
    state.sequencer.stepPresetPicker.operationActivationGeneration =
        batch.generation;

    if (status == FeedbackStatus::APPLIED) {
        const auto publication = queue.captureRuntimePublication();
        queue.applyRuntimePublication(publication);
        const auto realtime = queue.realtimeView(0);
        if (!queue.markAppliedFromRealtime(0, realtime.generation) ||
            !queue.publishRealtimeTelemetry()) {
            return false;
        }
    } else if (status == FeedbackStatus::CANCELLED) {
        const SequencerTrackActivationHistoryRef reference{
            .trackMask = batch.trackMask,
            .operationId = batch.operationId,
            .origin = batch.origin,
        };
        SequencerTrackActivationHistoryTransition transition;
        if (!queue.prepareHistoryTransition(
                reference,
                SequencerTrackActivationTarget::BEFORE,
                0x0001,
                0,
                true,
                transition
            )) {
            return false;
        }
        queue.commitHistoryTransition(transition);
    }

    const auto telemetry = queue.telemetry(0);
    const bool statusMatches =
        (status == FeedbackStatus::QUEUED &&
         telemetry.status == SequencerTrackActivationStatus::QUEUED) ||
        (status == FeedbackStatus::APPLIED &&
         telemetry.status == SequencerTrackActivationStatus::APPLIED) ||
        (status == FeedbackStatus::CANCELLED &&
         telemetry.status == SequencerTrackActivationStatus::CANCELLED);
    return statusMatches && telemetry.generation == batch.generation &&
        telemetry.origin == SequencerTrackActivationOrigin::STEP_PRESET;
}

bool addSequencerCaptureLane(
    core::state::sequencer::SequencerPatternState& pattern,
    uint8_t laneIndex,
    uint8_t controller,
    core::state::sequencer::SequencerCcLaneRoutePolicy routePolicy,
    uint8_t pinnedChannel = 0
) {
    using namespace core::state::sequencer;
    auto* bank = ensureSequencerCcLaneBank(pattern);
    if (bank == nullptr) return false;
    SequencerCcLaneDraft draft;
    draft.destination.controller = controller;
    draft.destination.routePolicy = routePolicy;
    draft.destination.pinnedChannel = pinnedChannel;
    draft.initialValue = 64;
    return createSequencerCcLane(*bank, laneIndex, draft).changed();
}

bool prepareSequencerCcLaneMacroConflictScenario(core::state::CoreState& state) {
    using namespace core::state;
    using namespace core::state::sequencer;

    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.overlays.hideAll();
    state.statusBar.playing.set(false);
    state.sequencer.reset();
    state.sequencerTracks.reset();
    state.trackNavigation.reset();
    state.sequencer.ccLaneUi.reset();
    // The fresh scenario already owns T1; synchronizing the same state is a
    // valid no-op, not a preparation failure.
    (void)state.setSharedTrackState(0x0001, 0);
    state.structureNavigationFocus.set(StructureNavigationFocus::TRACK);

    state.sequencer.pattern.length.set(1);
    state.sequencer.pattern.midiChannel.set(4);  // Ch5.
    state.sequencer.setStepDataAt(0, 60, 100, 75, 0);
    enableSequencerStep(state.sequencer, 0);
    if (!addSequencerCaptureLane(
            state.sequencer.pattern,
            0,
            74,
            SequencerCcLaneRoutePolicy::INHERIT_TRACK
        )) {
        return false;
    }
    auto* lanes = ensureSequencerCcLaneBank(state.sequencer.pattern);
    if (lanes == nullptr ||
        !setSequencerCcLaneEvent(*lanes, 0, 0, 96).changed()) {
        return false;
    }
    state.sequencer.pattern.bumpCcLaneRevision();

    auto& macroTrack = state.pages.tracks[0];
    macroTrack.channel = 4;
    macroTrack.activePage = 0;
    macroTrack.enabledPageMask = 0x0001;
    auto& page = macroTrack.pages[0];
    page.cc[0] = 74;
    page.values[0] = 0.25f;
    page.setMacroActive(0, true);
    std::snprintf(page.name, sizeof(page.name), "%s", "CC conflict");
    state.pages.syncSharedTrackState(0x0001, 0);
    state.pages.setActivePage(0);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
        state.macros,
        state.pages
    );
    state.statusBar.pageName.set(page.name);
    return true;
}

bool prepareSequencerTrackPasteCaptureScenario(core::state::CoreState& state) {
    using namespace core::state;
    using namespace core::state::sequencer;

    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.overlays.hideAll();
    state.statusBar.playing.set(false);
    state.sequencerTrackActivations.reset();
    state.structureClipboard.clear();
    state.sequencer.structureUi.trackPaste.reset();
    state.sequencerTracks.reset();
    state.trackNavigation.reset();

    // Sources T1/T3, overwrite destination T5, and free destination T6.
    // This makes the compact and per-mapping projections prove both target
    // kinds without relying on persisted user state.
    if (!state.setSharedTrackState(0x0015, 0)) return false;
    state.structureNavigationFocus.set(StructureNavigationFocus::TRACK);
    state.trackNavigation.syncPreviewTrack(0);

    state.sequencer.pattern.midiChannel.set(1);
    state.sequencer.setStepDataAt(0, 60, 104, 75, 0);
    enableSequencerStep(state.sequencer, 0);
    if (!addSequencerCaptureLane(
            state.sequencer.pattern,
            0,
            74,
            SequencerCcLaneRoutePolicy::INHERIT_TRACK
        ) ||
        !addSequencerCaptureLane(
            state.sequencer.pattern,
            1,
            71,
            SequencerCcLaneRoutePolicy::PINNED,
            3
        )) {
        return false;
    }

    auto& sourceThree = state.sequencerTracks.track(2);
    sourceThree.midiChannel.set(4);
    sourceThree.note[0] = 67;
    sourceThree.velocity[0] = 88;
    sourceThree.gate[0] = 75;
    sourceThree.setEnabled(0, true);
    if (!addSequencerCaptureLane(
            sourceThree,
            0,
            1,
            SequencerCcLaneRoutePolicy::INHERIT_TRACK
        )) {
        return false;
    }

    state.sequencerTracks.track(4).midiChannel.set(8);   // T5 -> Ch9.
    state.sequencerTracks.track(5).midiChannel.set(10);  // T6 -> Ch11.
    state.sequencerTracks.setTrackMuted(4, true);

    auto& selection = state.trackNavigation.selection;
    selection.active.set(true);
    selection.scope.set(StructureSelectionScope::TRACK);
    selection.cursorIndex.set(0);
    selection.selectedMask.set(0x0005);
    return true;
}

bool projectSequencerTrackPasteActivationScenario(
    core::state::CoreState& state,
    core::state::contextual::OperationFeedbackStatus status
) {
    namespace contextual = core::state::contextual;
    auto& paste = state.sequencer.structureUi.trackPaste;
    if (paste.activationGeneration == 0 || paste.operationGeneration == 0 ||
        !paste.plan.hasEntries() ||
        (status != contextual::OperationFeedbackStatus::QUEUED &&
         status != contextual::OperationFeedbackStatus::APPLIED)) {
        return false;
    }

    // SDL can resolve a local-loop activation inside the capture frame. Keep
    // the already-committed operation and frozen plan, but detach runtime
    // telemetry and use one capture-only generation so both user-visible
    // lifecycle projections remain correlated and deterministic.
    if (status == contextual::OperationFeedbackStatus::QUEUED) {
        state.sequencerTrackActivations.reset();
        uint32_t projectedGeneration = paste.activationGeneration + 1U;
        if (projectedGeneration == 0) projectedGeneration = 1;
        paste.activationGeneration = projectedGeneration;
    }
    contextual::setOperationFeedback(
        paste.feedback,
        contextual::ContextActionId::PASTE,
        {
            .kind = contextual::ContextEntityKind::TRACK,
            .track = paste.plan.firstSource,
            .item = paste.plan.sourceMask,
        },
        {
            .kind = contextual::ContextEntityKind::TRACK,
            .track = paste.plan.firstTarget,
            .item = paste.plan.targetMask,
        },
        status,
        contextual::ContextActionReason::NONE,
        status == contextual::OperationFeedbackStatus::QUEUED
            ? contextual::OperationFeedbackExpiryPolicy::WHEN_RESOLVED
            : contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        SDL_GetTicks(),
        status == contextual::OperationFeedbackStatus::APPLIED ? 1400U : 0U
    );
    paste.bump();
    return true;
}

}  // namespace

bool applyCaptureScenario(core::state::CoreState& state, const char* scenario) {
    if (!scenario || scenario[0] == '\0' || std::strcmp(scenario, "macro") == 0) {
        return true;
    }

    if (std::strcmp(scenario, "macro-automation-clean") == 0) {
        prepareMacroAutomationCleanScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "macro-track-multipage-automation") == 0) {
        prepareMacroTrackMultipageAutomationScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "macro-automation-curve-shape") == 0) {
        prepareMacroAutomationCurveShapeScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "macro-auto-mod") == 0) {
        prepareMacroAutoModScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "macro-curve-preview-shapes") == 0) {
        prepareMacroCurvePreviewShapesScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "macro-reusable-modulators") == 0) {
        prepareMacroReusableModulatorsScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "macro-multi-modulation") == 0) {
        prepareMacroMultiModulationScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "macro-performance-rail") == 0) {
        prepareMacroPerformanceRailScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "macro-initial-projection") == 0) {
        prepareMacroInitialProjectionScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "macro-modulation-assignment-copy") == 0) {
        prepareMacroModulationAssignmentCopyScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "project-modulators") == 0) {
        prepareProjectModulatorsScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "project-modulator-destination") == 0) {
        prepareProjectModulatorDestinationScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "project-modulator-workspace") == 0) {
        prepareProjectModulatorWorkspaceScenario(state);
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

    if (std::strcmp(scenario, "seq-track-paste-multi") == 0) {
        return prepareSequencerTrackPasteCaptureScenario(state);
    }

    if (std::strcmp(scenario, "seq-cc-macro-conflict") == 0) {
        return prepareSequencerCcLaneMacroConflictScenario(state);
    }

    if (std::strcmp(scenario, "seq-track-paste-project-queued") == 0) {
        return projectSequencerTrackPasteActivationScenario(
            state,
            core::state::contextual::OperationFeedbackStatus::QUEUED
        );
    }

    if (std::strcmp(scenario, "seq-track-paste-project-applied") == 0) {
        return projectSequencerTrackPasteActivationScenario(
            state,
            core::state::contextual::OperationFeedbackStatus::APPLIED
        );
    }

    if (std::strcmp(scenario, "seq-step-edit") == 0) {
        state.activeView.set(core::ui::ViewType::SEQUENCER);
        state.sequencer.setStepDataAt(0, 60, 100, 75);
        if (!state.sequencer.pattern.isEnabled(0)) {
            state.sequencer.pattern.toggle(0);
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

    if (std::strcmp(scenario, "seq-scale-constrained-degree") == 0) {
        prepareSequencerScaleScenario(
            state,
            oc::note::sequencer::StepSequencerScaleConstraintMode::ConstrainNearest
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-scale-free-out-of-scale") == 0) {
        prepareSequencerScaleScenario(
            state,
            oc::note::sequencer::StepSequencerScaleConstraintMode::Free
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-semantic-grid") == 0) {
        prepareSequencerSemanticGridScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-grid") == 0) {
        prepareSequencerLocalRandomGridScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-summed-pitch") == 0) {
        prepareSequencerSummedLocalRandomScenario(
            state,
            core::state::sequencer::StepProperty::NOTE
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-summed-velocity") == 0) {
        prepareSequencerSummedLocalRandomScenario(
            state,
            core::state::sequencer::StepProperty::VELOCITY
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-summed-gate") == 0) {
        prepareSequencerSummedLocalRandomScenario(
            state,
            core::state::sequencer::StepProperty::GATE
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-summed-nudge") == 0) {
        prepareSequencerSummedLocalRandomScenario(
            state,
            core::state::sequencer::StepProperty::NUDGE
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-local-random-nested-runtime") == 0) {
        prepareSequencerNestedLocalRandomRuntimeScenario(state);
        return true;
    }

    if (std::strcmp(scenario, "seq-step-preset-browse") == 0) {
        prepareStepPresetCaptureBase(state);
        return true;
    }

    if (std::strcmp(scenario, "seq-step-preset-browse-page-2") == 0) {
        prepareStepPresetCaptureBase(state);
        auto& picker = state.sequencer.stepPresetPicker;
        picker.setEntry(0, "late-bloom", "Late Bloom", true);
        picker.setEntry(1, "mono-drift", "Mono Drift", true);
        picker.setEntry(2, "odd-pulse", "Odd Pulse", true);
        picker.setEntry(3, "rain-grid", "Rain Grid", true);
        picker.setEntry(4, "soft-ratchet", "Soft Ratchet", true);
        picker.setEntry(5, "wide-cycle", "Wide Cycle", true);
        picker.hasPreviousPage.set(true);
        picker.hasNextPage.set(false);
        std::snprintf(
            picker.descriptor.semanticName,
            sizeof(picker.descriptor.semanticName),
            "%s",
            "Late Bloom"
        );
        std::snprintf(
            picker.descriptor.technicalId,
            sizeof(picker.descriptor.technicalId),
            "%s",
            "late-bloom"
        );
        picker.bump();
        return true;
    }

    if (std::strcmp(scenario, "seq-step-preset-detail-mixed") == 0) {
        prepareStepPresetCaptureBase(state);
        state.sequencer.stepPresetPicker.detailVisible.set(true);
        state.sequencer.stepPresetPicker.detailFocus.set(3);
        state.sequencer.stepPresetPicker.bump();
        return true;
    }

    if (std::strcmp(scenario, "seq-step-preset-preview-next") == 0) {
        prepareStepPresetCaptureBase(state);
        auto& picker = state.sequencer.stepPresetPicker;
        picker.detailVisible.set(true);
        picker.detailFocus.set(4);
        picker.descriptor.previewStateIndex = 2;
        picker.descriptor.previewNote = 65;
        std::snprintf(
            picker.descriptor.previewSummary,
            sizeof(picker.descriptor.previewSummary),
            "%s",
            "Preview 3/4 - F4"
        );
        picker.bump();
        return true;
    }

    if (std::strcmp(scenario, "seq-step-preset-overwrite-ready") == 0) {
        prepareStepPresetCaptureBase(
            state,
            core::state::sequencer::SequencerStepPresetPickerMode::SAVE
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-step-preset-overwrite-pressed") == 0 ||
        std::strcmp(scenario, "seq-step-preset-overwrite-armed") == 0) {
        prepareStepPresetCaptureBase(
            state,
            core::state::sequencer::SequencerStepPresetPickerMode::SAVE
        );
        auto guard = state.sequencer.stepPresetPicker.actionGuard.get();
        guard.phase = std::strcmp(
            scenario,
            "seq-step-preset-overwrite-pressed"
        ) == 0
            ? core::state::contextual::GuardedActionPhase::PRESSED
            : core::state::contextual::GuardedActionPhase::ARMED;
        guard.pressedAtMs = SDL_GetTicks();
        guard.armedAtMs = SDL_GetTicks();
        guard.guardDurationMs = 1000;
        guard.progressPermille = guard.phase ==
                core::state::contextual::GuardedActionPhase::ARMED
            ? 450
            : 120;
        state.sequencer.stepPresetPicker.actionGuard.set(guard);
        setStepPresetOperationFeedback(
            state,
            core::state::contextual::ContextActionId::SAVE,
            guard.phase == core::state::contextual::GuardedActionPhase::ARMED
                ? core::state::contextual::OperationFeedbackStatus::ARMED
                : core::state::contextual::OperationFeedbackStatus::PRESSED,
            core::state::contextual::ContextActionReason::NONE
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-step-preset-cancelled") == 0) {
        return prepareStepPresetActivationScenario(
            state,
            core::state::contextual::OperationFeedbackStatus::CANCELLED,
            core::state::contextual::ContextActionReason::NONE
        );
    }

    if (std::strcmp(scenario, "seq-step-preset-applied") == 0) {
        return prepareStepPresetActivationScenario(
            state,
            core::state::contextual::OperationFeedbackStatus::APPLIED,
            core::state::contextual::ContextActionReason::ADAPTED
        );
    }

    if (std::strcmp(scenario, "seq-step-preset-queued") == 0) {
        return prepareStepPresetActivationScenario(
            state,
            core::state::contextual::OperationFeedbackStatus::QUEUED,
            core::state::contextual::ContextActionReason::PENDING
        );
    }

    if (std::strcmp(scenario, "seq-step-preset-save-capacity") == 0) {
        prepareStepPresetOperationScenario(
            state,
            core::state::contextual::OperationFeedbackStatus::FAILED,
            core::state::contextual::ContextActionReason::CAPACITY,
            true
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-step-preset-save-stale") == 0) {
        prepareStepPresetOperationScenario(
            state,
            core::state::contextual::OperationFeedbackStatus::FAILED,
            core::state::contextual::ContextActionReason::STALE_TARGET,
            true
        );
        return true;
    }

    if (std::strcmp(scenario, "seq-step-preset-save-storage-failed") == 0) {
        prepareStepPresetOperationScenario(
            state,
            core::state::contextual::OperationFeedbackStatus::FAILED,
            core::state::contextual::ContextActionReason::STORAGE_UNAVAILABLE,
            true
        );
        return true;
    }

    if (std::strcmp(scenario, "settings") == 0) {
        state.activeView.set(core::ui::ViewType::DEVICE_SETTINGS);
        state.deviceSettings.openView();
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
