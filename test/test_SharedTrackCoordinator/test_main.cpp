#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>

#include "../../src/state/shared/SharedTrackCoordinator.hpp"

namespace {

core::state::shared::SharedTrackCoordinator::StateRefs refsFor(
    oc::state::Signal<uint8_t, 8>& activeTrack,
    oc::state::Signal<uint16_t, 16>& enabledMask,
    core::state::macro::MacroPagesState& pages,
    core::state::sequencer::SequencerTrackBankState& sequencerTracks,
    core::state::sequencer::SequencerState& sequencer
) {
    return core::state::shared::SharedTrackCoordinator::StateRefs{
        activeTrack,
        enabledMask,
        pages,
        sequencerTracks,
        sequencer,
    };
}

void test_apply_sanitizes_and_syncs_all_domains() {
    oc::state::Signal<uint8_t, 8> activeTrack{1};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0002};
    auto pages = std::make_unique<core::state::macro::MacroPagesState>();
    auto sequencerTracks = std::make_unique<core::state::sequencer::SequencerTrackBankState>();
    auto sequencer = std::make_unique<core::state::sequencer::SequencerState>();

    const auto result = core::state::shared::SharedTrackCoordinator::apply(
        refsFor(activeTrack, enabledMask, *pages, *sequencerTracks, *sequencer),
        0x0000,
        9
    );

    assert(result.changed);
    assert(result.enabledMask == 0x0001);
    assert(result.activeTrack == 0);
    assert(enabledMask.get() == 0x0001);
    assert(activeTrack.get() == 0);
    assert(pages->currentTrackEnabledMask() == 0x0001);
    assert(pages->currentActiveTrack() == 0);
    assert(sequencerTracks->currentEnabledMask() == 0x0001);
    assert(sequencerTracks->activeTrackIndex() == 0);

    std::cout << "[PASS] test_apply_sanitizes_and_syncs_all_domains\n";
}

void test_apply_switches_to_first_enabled_track_when_active_is_disabled() {
    oc::state::Signal<uint8_t, 8> activeTrack{0};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0001};
    auto pages = std::make_unique<core::state::macro::MacroPagesState>();
    auto sequencerTracks = std::make_unique<core::state::sequencer::SequencerTrackBankState>();
    auto sequencer = std::make_unique<core::state::sequencer::SequencerState>();

    const auto result = core::state::shared::SharedTrackCoordinator::apply(
        refsFor(activeTrack, enabledMask, *pages, *sequencerTracks, *sequencer),
        0x000C,
        0
    );

    assert(result.changed);
    assert(result.enabledMask == 0x000C);
    assert(result.activeTrack == 2);
    assert(enabledMask.get() == 0x000C);
    assert(activeTrack.get() == 2);
    assert(pages->currentTrackEnabledMask() == 0x000C);
    assert(pages->currentActiveTrack() == 2);
    assert(sequencerTracks->currentEnabledMask() == 0x000C);
    assert(sequencerTracks->activeTrackIndex() == 2);

    std::cout << "[PASS] test_apply_switches_to_first_enabled_track_when_active_is_disabled\n";
}

void test_noop_still_keeps_domain_caches_aligned() {
    oc::state::Signal<uint8_t, 8> activeTrack{2};
    oc::state::Signal<uint16_t, 16> enabledMask{0x000C};
    auto pages = std::make_unique<core::state::macro::MacroPagesState>();
    auto sequencerTracks = std::make_unique<core::state::sequencer::SequencerTrackBankState>();
    auto sequencer = std::make_unique<core::state::sequencer::SequencerState>();

    pages->syncSharedTrackState(0x0001, 0);
    sequencerTracks->syncSharedTrackState(0x0001, 0);

    const auto result = core::state::shared::SharedTrackCoordinator::apply(
        refsFor(activeTrack, enabledMask, *pages, *sequencerTracks, *sequencer),
        0x000C,
        2
    );

    assert(!result.changed);
    assert(pages->currentTrackEnabledMask() == 0x000C);
    assert(pages->currentActiveTrack() == 2);
    assert(sequencerTracks->currentEnabledMask() == 0x000C);
    assert(sequencerTracks->activeTrackIndex() == 2);

    std::cout << "[PASS] test_noop_still_keeps_domain_caches_aligned\n";
}

void test_apply_clears_muted_bits_for_disabled_sequencer_tracks() {
    oc::state::Signal<uint8_t, 8> activeTrack{1};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0003};
    auto pages = std::make_unique<core::state::macro::MacroPagesState>();
    auto sequencerTracks = std::make_unique<core::state::sequencer::SequencerTrackBankState>();
    auto sequencer = std::make_unique<core::state::sequencer::SequencerState>();

    sequencerTracks->syncSharedTrackState(0x0003, 1);
    assert(sequencerTracks->setTrackMuted(1, true));

    const auto result = core::state::shared::SharedTrackCoordinator::apply(
        refsFor(activeTrack, enabledMask, *pages, *sequencerTracks, *sequencer),
        0x0001,
        0
    );

    assert(result.changed);
    assert(sequencerTracks->currentEnabledMask() == 0x0001);
    assert(sequencerTracks->currentMutedMask() == 0);
    assert(sequencerTracks->activeTrackIndex() == 0);

    std::cout << "[PASS] test_apply_clears_muted_bits_for_disabled_sequencer_tracks\n";
}

void test_publish_prepared_state_does_not_replace_preinstalled_editor_content() {
    oc::state::Signal<uint8_t, 8> activeTrack{0};
    oc::state::Signal<uint16_t, 16> enabledMask{0x0001};
    auto pages = std::make_unique<core::state::macro::MacroPagesState>();
    auto sequencerTracks = std::make_unique<core::state::sequencer::SequencerTrackBankState>();
    auto sequencer = std::make_unique<core::state::sequencer::SequencerState>();

    sequencer->pattern.note[0] = 91;
    sequencerTracks->track(2).note[0] = 72;
    const auto result =
        core::state::shared::SharedTrackCoordinator::publishPreparedSequencerState(
            refsFor(activeTrack, enabledMask, *pages, *sequencerTracks, *sequencer),
            0x0005,
            2
        );

    assert(result.ok);
    assert(result.changed);
    assert(activeTrack.get() == 2);
    assert(enabledMask.get() == 0x0005);
    assert(sequencerTracks->activeTrackIndex() == 2);
    assert(pages->currentActiveTrack() == 2);
    assert(sequencer->pattern.note[0] == 91);
    assert(sequencerTracks->track(2).note[0] == 72);

    std::cout
        << "[PASS] test_publish_prepared_state_does_not_replace_preinstalled_editor_content\n";
}

}  // namespace

int main() {
    test_apply_sanitizes_and_syncs_all_domains();
    test_apply_switches_to_first_enabled_track_when_active_is_disabled();
    test_noop_still_keeps_domain_caches_aligned();
    test_apply_clears_muted_bits_for_disabled_sequencer_tracks();
    test_publish_prepared_state_does_not_replace_preinstalled_editor_content();

    std::cout << "All SharedTrackCoordinator tests passed\n";
    return 0;
}
