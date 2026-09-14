#include <cassert>
#include <cstring>
#include <iostream>

#include <oc/api/ButtonAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "handler/sequencer/SequencerPresetLibraryWorkflow.hpp"
#include "state/CoreState.hpp"
#include "support/CoreStorages.hpp"
#include "support/InputTestHardware.hpp"

namespace {
namespace seq = core::state::sequencer;
using Workflow = core::handler::SequencerPresetLibraryWorkflow;
using Adapter = core::handler::SequencerPresetLibraryAdapter;
using Result = core::handler::SequencerPresetLibraryResult;
using Outcome = core::handler::SequencerPresetLibraryOutcome;
using Overlay = core::ui::OverlayType;
uint32_t now() { return 0U; }

struct Harness {
    test_support::CoreStorages storages;
    core::state::CoreState state{storages.settings};
    oc::core::event::EventBus bus;
    oc::core::input::InputBinding input{bus, now};
    test_support::TestButtonHardware hardware;
    oc::api::ButtonAPI buttons{input, hardware};
    oc::context::OverlayManager<Overlay> overlays{state.overlays, buttons};
    Workflow workflow{state.sequencer, overlays};
    Outcome writeOutcome = Outcome::BLOCKED;
    unsigned writes = 0;
    unsigned pageLoads = 0;
    bool navigationSucceeds = true;

    Harness() {
        overlays.setActiveViewProvider([]() { return oc::type::ScopeID{781U}; });
        overlays.registerCleanup(Overlay::SEQ_STEP_EDIT, 782U);
        overlays.registerCleanup(Overlay::PRESET_LIBRARY, 783U);
        overlays.show(Overlay::SEQ_STEP_EDIT, true);
        Adapter adapter{};
        adapter.context = this;
        adapter.kind = seq::SequencerPresetLibraryKind::PATTERN;
        adapter.beginSession = [](void*) { return true; };
        adapter.loadPage = [](void* context, Adapter::Entry*, uint8_t, const char*, Adapter::PageDirection,
                              core::persistence::ProductAssetFileListResult& page) {
            ++static_cast<Harness*>(context)->pageLoads;
            page = {};
            return core::handler::SequencerPresetLibraryPager::PageLoadStatus::READY;
        };
        adapter.clearInspection = [](void*) {};
        adapter.inspect = [](void*, const char*, bool) { return seq::SequencerPresetLibraryFeedback::NONE; };
        adapter.actionSpec = [](const void*, bool, bool, bool) { return core::state::contextual::ContextActionSpec{}; };
        adapter.execute = [](void*, Adapter::Mode, const char*, bool, bool) { return Result{}; };
        adapter.createFolder = write;
        adapter.renameManaged = write;
        adapter.enterFolder = [](void* context, const char*) { return static_cast<Harness*>(context)->navigationSucceeds; };
        assert(workflow.open(adapter));
    }

    static Result write(void* context, const char*) {
        auto& self = *static_cast<Harness*>(context);
        ++self.writes;
        return {.outcome = self.writeOutcome};
    }

    void createFolder() {
        workflow.toggleMode();
        state.sequencer.presetLibrary.selectedIndex.set(1U);
        (void)workflow.executeTap(0U);
        assert(workflow.textEditing());
    }
};

void test_preset_rejection_retry_and_cancel_preserve_parent() {
    for (const auto kind : {seq::SequencerPresetLibraryEntryKind::ASSET,
                            seq::SequencerPresetLibraryEntryKind::FOLDER}) {
        Harness h;
        auto& picker = h.state.sequencer.presetLibrary;
        auto& pattern = picker.pattern();
        // These are the target and panel captured by the management adapter.
        pattern.panel = seq::SequencerPatternPresetLibraryPanel::MANAGE;
        pattern.managementAction = seq::SequencerPatternPresetManagementAction::RENAME;
        pattern.managedEntryKind = kind;
        std::strcpy(pattern.managedEntryName.data(), "Original");
        std::strcpy(pattern.managedEntryId.data(), "original");
        (void)h.workflow.executeTap(0U);
        assert(h.workflow.textEditing());
        h.workflow.enterDetail(); // Insert one key in the local draft.
        const auto draft = pattern.textDraft;
        for (const auto outcome : {Outcome::BLOCKED, Outcome::RETRY_PENDING}) {
            h.writeOutcome = outcome;
            assert(h.workflow.executeTap(1U).outcome == outcome);
            assert(h.workflow.textEditing());
            assert(pattern.textDraft == draft);
            assert(std::strcmp(pattern.managedEntryName.data(), "Original") == 0);
            assert(h.overlays.isCurrent(Overlay::PRESET_LIBRARY));
            assert(h.state.projectHistory.undoCount() == 0U);
        }
        h.workflow.setTextShift(true);
        assert(!h.workflow.back(2U));
        assert(!h.workflow.textEditing());
        assert(pattern.panel == seq::SequencerPatternPresetLibraryPanel::MANAGE);
        assert(!pattern.textShiftActive && pattern.textDraft[0] == '\0');
        assert(h.writes == 2U);
        (void)h.workflow.executeTap(3U); // Reopen from the same managed target.
        h.writeOutcome = Outcome::SAVED;
        assert(h.workflow.executeTap(4U).outcome == Outcome::SAVED);
        assert(!h.workflow.textEditing());
        assert(h.overlays.isCurrent(Overlay::PRESET_LIBRARY));
        assert(pattern.panel == seq::SequencerPatternPresetLibraryPanel::BROWSE);
        assert(h.writes == 3U);
    }
}

void test_folder_validation_and_committed_write_are_separate_from_navigation() {
    for (const bool navigationSucceeds : {true, false}) {
        Harness h;
        h.createFolder();
        assert(h.workflow.executeTap(0U).outcome == Outcome::BLOCKED); // Empty name.
        assert(h.workflow.textEditing() && h.writes == 0U);
        h.workflow.setTextShift(true);
        h.workflow.enterDetail(); // Q
        h.navigationSucceeds = navigationSucceeds;
        h.writeOutcome = Outcome::SAVED;
        const auto pageLoads = h.pageLoads;
        assert(h.workflow.executeTap(1U).outcome == Outcome::SAVED);
        assert(h.pageLoads > pageLoads);
        assert(h.writes == 1U && !h.workflow.textEditing());
        assert(!h.state.sequencer.presetLibrary.pattern().textShiftActive);
        assert(h.overlays.isCurrent(Overlay::PRESET_LIBRARY));
        h.workflow.close();
        assert(h.overlays.isCurrent(Overlay::SEQ_STEP_EDIT));
    }
}

void test_drum_child_cancel_and_accept_preserve_parent_changes() {
    seq::DrumSequencerState ui;
    auto& editor = ui.laneEditor;
    editor.active = true;
    editor.field = seq::DrumLaneEditorField::NAME;
    (void)seq::setDrumLaneName(editor.draft, "Kick");
    const auto original = editor.draft;
    for (const bool parentDirty : {false, true}) {
        editor.dirty = parentDirty;
        ui.toggleLaneNameEditing();
        ui.insertLaneNameKey();
        assert(editor.dirty);
        ui.setLaneNameShift(true);
        ui.cancelLaneNameEditing();
        assert(editor.draft.name == original.name);
        assert(editor.draft.overrideMask == original.overrideMask);
        assert(editor.dirty == parentDirty);
        assert(!editor.textEditing && !editor.textShiftActive && editor.active);
        ui.toggleLaneNameEditing();
        ui.insertLaneNameKey();
        ui.backspaceLaneName(); // Revert the text before validating the child.
        ui.acceptLaneNameEditing();
        assert(editor.dirty == parentDirty);
        assert(editor.draft.name == original.name);
        assert(editor.draft.overrideMask == original.overrideMask);
        assert(!editor.textEditing && editor.active);
    }
    ui.toggleLaneNameEditing();
    ui.insertLaneNameKey();
    ui.acceptLaneNameEditing();
    assert(editor.dirty && editor.active && !editor.textEditing);
    assert(editor.draft.name != original.name);
}
} // namespace

int main() {
    test_preset_rejection_retry_and_cancel_preserve_parent();
    test_folder_validation_and_committed_write_are_separate_from_navigation();
    test_drum_child_cancel_and_accept_preserve_parent_changes();
    std::cout << "Text keyboard lifecycle: rejection, retry, parent drafts and storage passed.\n";
}
