#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/sequencer/SequencerStructureTrackTransferTransaction.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace {

struct PublishRecorder {
    bool called = false;
};

void recordPreparedPublish(void* context, uint16_t, uint8_t) {
    auto* recorder = static_cast<PublishRecorder*>(context);
    assert(recorder != nullptr);
    recorder->called = true;
}

bool canRecordPreparedHistory(
    void* context,
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change
) {
    auto* history = static_cast<core::state::sequencer::SequencerHistoryService*>(context);
    return history != nullptr && history->canRecordStructure(change);
}

void recordPreparedHistory(
    void* context,
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    auto* history = static_cast<core::state::sequencer::SequencerHistoryService*>(context);
    assert(history != nullptr);
    history->recordPreparedStructure(std::move(change));
}

void storeSourceClipboard(
    core::state::StructureClipboardState& clipboard,
    const core::state::sequencer::SequencerState& editor
) {
    core::state::sequencer::SequencerPatternSnapshot snapshot;
    core::state::sequencer::captureSnapshot(editor.pattern, snapshot);
    assert(clipboard.storeSequencerTrack(snapshot, nullptr, 0));
}

void test_missing_prepared_publication_blocks_before_mutation() {
    core::state::sequencer::SequencerTrackBankState tracks;
    core::state::sequencer::SequencerState editor;
    tracks.reset();
    editor.pattern.note[0] = 77;
    editor.pattern.setEnabled(0, true);
    core::state::StructureClipboardState clipboard;
    storeSourceClipboard(clipboard, editor);

    oc::state::Signal<uint8_t, 8> activeTrack{0};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0001};
    const core::handler::SharedTrackDomainServices shared{
        {activeTrack, enabledMask}
    };
    const core::handler::SequencerHistoryDomainServices history;

    const auto prepared = core::handler::prepareSequencerTrackTransfer(
        tracks,
        editor,
        clipboard,
        shared,
        history,
        1
    );

    assert(prepared.status ==
           core::handler::SequencerTrackTransferStatus::PUBLICATION_UNAVAILABLE);
    assert(tracks.currentEnabledMask() == 0x0001);
    assert(tracks.activeTrackIndex() == 0);
    assert(tracks.track(1).note[0] ==
           core::state::sequencer::SequencerPatternState::DEFAULT_NOTE);
    assert(editor.pattern.note[0] == 77);

    std::cout << "[PASS] test_missing_prepared_publication_blocks_before_mutation\n";
}

void test_missing_prepared_history_blocks_before_mutation() {
    core::state::sequencer::SequencerTrackBankState tracks;
    core::state::sequencer::SequencerState editor;
    tracks.reset();
    editor.pattern.note[0] = 79;
    editor.pattern.setEnabled(0, true);
    tracks.track(1).midiChannel.set(8);
    core::state::StructureClipboardState clipboard;
    storeSourceClipboard(clipboard, editor);

    oc::state::Signal<uint8_t, 8> activeTrack{0};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0001};
    PublishRecorder recorder;
    const core::handler::SharedTrackDomainServices shared{
        {activeTrack, enabledMask},
        {
            .context = &recorder,
            .publishPreparedSequencerState = recordPreparedPublish,
        },
    };
    const core::handler::SequencerHistoryDomainServices history;

    const auto prepared = core::handler::prepareSequencerTrackTransfer(
        tracks,
        editor,
        clipboard,
        shared,
        history,
        1
    );

    assert(prepared.status ==
           core::handler::SequencerTrackTransferStatus::HISTORY_UNAVAILABLE);
    assert(!recorder.called);
    assert(tracks.currentEnabledMask() == 0x0001);
    assert(tracks.activeTrackIndex() == 0);
    assert(tracks.track(1).midiChannel.get() == 8);
    assert(tracks.track(1).note[0] ==
           core::state::sequencer::SequencerPatternState::DEFAULT_NOTE);
    assert(editor.pattern.note[0] == 79);

    std::cout << "[PASS] test_missing_prepared_history_blocks_before_mutation\n";
}

void test_outgoing_live_route_change_invalidates_preparation_atomically() {
    core::state::sequencer::SequencerTrackBankState tracks;
    core::state::sequencer::SequencerState editor;
    tracks.reset();
    editor.pattern.note[0] = 81;
    editor.pattern.setEnabled(0, true);
    core::state::StructureClipboardState clipboard;
    storeSourceClipboard(clipboard, editor);

    oc::state::Signal<uint8_t, 8> activeTrack{0};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0001};
    PublishRecorder recorder;
    const core::handler::SharedTrackDomainServices shared{
        {activeTrack, enabledMask},
        {
            .context = &recorder,
            .publishPreparedSequencerState = recordPreparedPublish,
        },
    };
    core::state::sequencer::SequencerHistoryService historyService;
    const core::handler::SequencerHistoryDomainServices history{
        {
            .context = &historyService,
            .canRecordStructure = canRecordPreparedHistory,
            .recordPreparedStructure = recordPreparedHistory,
        }
    };

    auto prepared = core::handler::prepareSequencerTrackTransfer(
        tracks,
        editor,
        clipboard,
        shared,
        history,
        1
    );
    assert(prepared.ready());

    editor.pattern.midiChannel.set(5);
    const auto result = core::handler::commitPreparedSequencerTrackTransfer(
        tracks,
        editor,
        clipboard,
        shared,
        history,
        std::move(prepared)
    );

    assert(result.status == core::handler::SequencerTrackTransferStatus::STALE);
    assert(!recorder.called);
    assert(historyService.undoCount() == 0);
    assert(tracks.currentEnabledMask() == 0x0001);
    assert(tracks.activeTrackIndex() == 0);
    assert(tracks.track(1).note[0] ==
           core::state::sequencer::SequencerPatternState::DEFAULT_NOTE);
    assert(editor.pattern.note[0] == 81);
    assert(editor.pattern.midiChannel.get() == 5);

    std::cout
        << "[PASS] test_outgoing_live_route_change_invalidates_preparation_atomically\n";
}

}  // namespace

int main() {
    test_missing_prepared_publication_blocks_before_mutation();
    test_missing_prepared_history_blocks_before_mutation();
    test_outgoing_live_route_change_invalidates_preparation_atomically();
    std::cout << "All SequencerStructureTrackTransferTransaction tests passed\n";
    return 0;
}
