#include "validation/benchmark/HardwareBenchmarkFixture.hpp"

#include <array>

#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerClipRegionOps.hpp"
#include "validation/fixtures/MacroModulationFixture.hpp"

namespace core::validation::benchmark {

bool prepareHardwareBenchmarkFixture(core::state::CoreState& state) {
    using namespace core::state::sequencer;
    core::validation::fixtures::prepareMacroMultiModulationScenario(state);
    const auto& graph = state.pages.control.authored.modulation;
    if (graph.sourceCount != 3U || graph.outputBindingCount != 4U) return false;

    // Macro 1 exercises the focused Pulse Lift assignment; Macro 2 remains
    // available for a future source-creation workload, without modulation.
    auto& page = state.pages.tracks[0].pages[0];
    page.cc[1] = 71U;
    page.values[1] = 0.5f;
    page.setMacroActive(1U, true);
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(state.configRevision.get()));

    state.statusBar.playing.set(false);
    state.statusBar.tempo.set(120.0f);
    state.midiSync.mode.set(core::state::MidiSyncMode::MASTER);
    state.midiSync.followTransport.set(false);

    // 16 deterministic sixteenth notes, plus a non-conflicting CC1 lane on
    // Track 1's route. Track 2 is silent except its shared Slow Tide CC71.
    auto& pattern = state.sequencer.pattern;
    pattern.length.set(16U);
    pattern.stepsPerBeat.set(4U);
    resetClipToPattern(state.sequencer.clip, pattern);
    state.sequencer.bumpClipRevision();
    constexpr std::array<uint8_t, 4> notes{48U, 52U, 55U, 52U};
    for (uint8_t step = 0; step < 16U; ++step) {
        state.sequencer.setStepDataAt(step, notes[step % notes.size()], 96U, 50U, 0, 100U);
        if (!pattern.isEnabled(step)) pattern.toggle(step);
    }
    auto* lanes = ensureSequencerCcLaneBank(pattern);
    if (!lanes) return false;
    SequencerCcLaneDraft lane{};
    lane.destination.controller = 1U;
    lane.destination.routePolicy = SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    if (!createSequencerCcLane(*lanes, 0U, lane).changed()) return false;
    constexpr std::array<uint8_t, 4> values{24U, 104U, 40U, 88U};
    for (uint8_t index = 0; index < values.size(); ++index) {
        const uint8_t step = static_cast<uint8_t>(index * 4U);
        if (!setSequencerCcLaneEvent(*lanes, 0U, step, values[index]).changed() ||
            !setSequencerCcLaneTransition(*lanes, 0U, step, SequencerCcLaneTransition::LINEAR).changed()) {
            return false;
        }
    }
    pattern.bumpCcLaneRevision();
    return validSequencerCcLaneBank(*lanes);
}

}  // namespace core::validation::benchmark
