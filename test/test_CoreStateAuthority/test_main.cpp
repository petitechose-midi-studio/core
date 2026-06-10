#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "../../src/handler/settings/DataManagerDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"

namespace {
using test_support::CoreStorages;

void test_hide_all_invokes_cleanup_for_stacked_overlays() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    std::vector<core::ui::OverlayType> cleaned;
    auto handle = state.overlays.setCleanupCallbackScoped(
        [&cleaned](core::ui::OverlayType type) { cleaned.push_back(type); }
    );

    state.overlays.show(core::ui::OverlayType::MACRO_EDIT, false);
    state.overlays.show(core::ui::OverlayType::MACRO_EDIT_SELECTOR, true);

    assert(state.macroEdit.visible.get());
    assert(state.macroEdit.selector.visible.get());

    state.overlays.hideAll();

    assert(state.overlays.current() == core::ui::OverlayType::NONE);
    assert(!state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());
    assert(cleaned.size() == 2);
    assert(cleaned[0] == core::ui::OverlayType::MACRO_EDIT);
    assert(cleaned[1] == core::ui::OverlayType::MACRO_EDIT_SELECTOR);

    std::cout << "[PASS] test_hide_all_invokes_cleanup_for_stacked_overlays\n";
}

void test_data_manager_reports_deferred_sequencer_pattern_load_while_playing() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    state.sequencer.pattern.length.set(8);
    state.sequencer.pattern.stepsPerBeat.set(2);
    state.sequencer.pattern.midiChannel.set(1);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(0, 61, 101, 80);
    state.sequencer.pattern.toggle(0);
    state.flush();
    const auto services = core::handler::DataManagerDomainServices::fromCoreState(state);

    assert(services.execute(
               core::state::DataManagerCommand::SEQ_SAVE_PATTERN_SLOT,
               1,
               core::state::DataManagerSetLoadMode::REPLACE
           ).success);

    state.sequencer.pattern.length.set(16);
    state.sequencer.pattern.stepsPerBeat.set(4);
    state.sequencer.pattern.midiChannel.set(6);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(0, 40, 55, 30);
    state.sequencer.pattern.toggle(0);

    state.statusBar.playing.set(true);
    state.sequencer.playheadStep.set(5);

    const auto result = services.execute(
        core::state::DataManagerCommand::SEQ_LOAD_PATTERN_SLOT,
        1,
        core::state::DataManagerSetLoadMode::REPLACE
    );

    assert(result.handled);
    assert(result.success);
    assert(result.isLoadOperation);
    assert(result.deferredApply);
    assert(result.loadStatus == core::persistence::SlotLoadStatus::OK);

    // Still live state until next step boundary.
    assert(state.sequencer.pattern.length.get() == 16);
    assert(state.sequencer.pattern.note[0] == 40);

    state.update();
    assert(state.sequencer.pattern.length.get() == 16);
    assert(state.sequencer.pattern.note[0] == 40);

    state.sequencer.playheadStep.set(6);
    state.update();
    assert(state.sequencer.pattern.length.get() == 8);
    assert(state.sequencer.pattern.stepsPerBeat.get() == 2);
    assert(state.sequencer.pattern.midiChannel.get() == 1);
    assert(state.sequencer.pattern.note[0] == 61);

    std::cout << "[PASS] test_data_manager_reports_deferred_sequencer_pattern_load_while_playing\n";
}

}  // namespace

int main() {
    test_hide_all_invokes_cleanup_for_stacked_overlays();
    test_data_manager_reports_deferred_sequencer_pattern_load_while_playing();
    std::cout << "\nAll CoreState authority tests passed.\n";
    return 0;
}
