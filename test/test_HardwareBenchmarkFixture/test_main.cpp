#include <cassert>
#include <cstring>
#include <iostream>

#include "persistence/DeviceSettingsStorageLayout.hpp"
#include "state/CoreState.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "validation/benchmark/BenchStorage.hpp"
#include "validation/benchmark/BenchInput.hpp"
#include "validation/benchmark/HardwareBenchmarkFixture.hpp"
#include "support/NotificationTestUtils.hpp"

namespace {
class FakeButtons final : public oc::interface::IButton {
public:
    bool physicalDown = false;
    unsigned polls = 0;
    oc::type::ButtonCallback callback;
    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update(uint32_t) override { ++polls; }
    bool isPressed(oc::type::ButtonID id) const override { return id == 3U && physicalDown; }
    void setCallback(oc::type::ButtonCallback cb) override { callback = std::move(cb); }
};
}

int main() {
    using namespace core::validation::benchmark;
    using namespace core::state::modulation;
    using namespace core::state::sequencer;
    assert(!allPhysicalReleased() && !setSyntheticButton(3U, true));
    {
        auto physical = std::make_unique<FakeButtons>();
        auto* raw = physical.get();
        BenchButtons buttons(std::move(physical));
        assert(buttons.init() && allPhysicalReleased());
        assert(setSyntheticButton(3U, true) && buttons.isPressed(3U));
        assert(allPhysicalReleased()); // Synthetic holds must not look physical.
        assert(!setSyntheticButton(64U, true));
        unsigned callbacks = 0;
        buttons.setCallback([&](auto, auto) { ++callbacks; });
        raw->physicalDown = true;
        raw->callback(3U, oc::type::ButtonEvent::PRESSED);
        buttons.update(5U);
        clearSyntheticButtons();
        assert(buttons.isPressed(3U) && !allPhysicalReleased());
        assert(raw->polls == 1U && callbacks == 1U);
        raw->physicalDown = false;
        assert(!buttons.isPressed(3U) && allPhysicalReleased());
    }
    assert(!allPhysicalReleased());
    BenchSettingsStorage settingsProbe;
    uint8_t probe = 0;
    assert(settingsProbe.init() && settingsProbe.available());
    assert(settingsProbe.read(0U, &probe, 1U) == 1U && probe == 0xFF);
    probe = 41U;
    assert(settingsProbe.write(settingsProbe.capacity() - 1U, &probe, 2U) == 1U);
    assert(settingsProbe.read(settingsProbe.capacity() - 1U, &probe, 1U) == 1U && probe == 41U);
    assert(settingsProbe.read(settingsProbe.capacity(), &probe, 1U) == 0U);
    assert(settingsProbe.write(settingsProbe.capacity(), &probe, 1U) == 0U);
    assert(settingsProbe.erase(0U, settingsProbe.capacity()) && settingsProbe.commit());
    assert(settingsProbe.read(settingsProbe.capacity() - 1U, &probe, 1U) == 1U && probe == 0xFF);
    UnavailableFileSystem fs;
    uint8_t sentinel = 42U;
    assert(!fs.available() && !fs.init());
    assert(!fs.stat("/current.mspj"));
    assert(!fs.list("/", nullptr, nullptr));
    assert(!fs.createDirectory("/bench"));
    assert(!fs.remove("/", oc::interface::RemoveMode::RECURSIVE));
    assert(!fs.rename("/current.mspj", "/old.mspj"));
    assert(!fs.read("/current.mspj", 0U, &sentinel, 1U));
    assert(sentinel == 42U);
    assert(!fs.write("/current.mspj", 0U, &sentinel, 1U));
    assert(!fs.flush("/current.mspj"));
    assert(!fs.beginWrite("/current.mspj", 1U));
    assert(!fs.appendWrite(&sentinel, 1U) && !fs.finishWrite());
    fs.abortWrite();

    // Two fresh boots must produce identical authored routes and deterministic
    // musical content. Never reapply the fixture to a running CoreState.
    for (int boot = 0; boot < 2; ++boot) {
        BenchSettingsStorage settings;
        core::state::CoreState state(settings);
        assert(prepareHardwareBenchmarkFixture(state));
        test_support::drainNotifications();
        assert(std::strcmp(VERSION, "bench-macro-v1") == 0);
        assert(!state.statusBar.playing.get() && state.statusBar.tempo.get() == 120.0f);
        assert(state.midiSync.mode.get() == core::state::MidiSyncMode::MASTER);
        assert(!state.midiSync.followTransport.get());
        assert(state.activeView.get() == core::ui::ViewType::MACRO);
        assert(state.currentSharedTrackEnabledMask() == 3U);
        assert(state.currentSharedActiveTrack() == 0U);
        const auto& graph = state.pages.control.authored().modulation;
        assert(graph.sourceCount == 3U && graph.outputBindingCount == 4U);
        assert(graph.outputBindings[0].amountQ15 == 4096);
        assert(graph.outputBindings[1].amountQ15 == 8192);
        assert(graph.outputBindings[2].amountQ15 == -13107);
        assert(graph.outputBindings[3].amountQ15 == 4915);
        assert(projectControlFocusedModulationBinding(state.pages.control, {0U, 0U, 0U}) ==
               graph.outputBindings[2].id);
        ProjectControlMacroDestinationView macro{};
        assert(readProjectControlMacroDestination(state.pages.control, {0U, 0U, 0U}, macro));
        assert(macro.modulationCount == 3U);
        assert(readProjectControlMacroDestination(state.pages.control, {0U, 0U, 1U}, macro));
        assert(macro.modulationCount == 0U);
        assert(state.pages.tracks[0].pages[0].isMacroActive(1U));
        assert(state.projectTracks.authored.midiChannels[0] == 5U);
        assert(state.projectTracks.authored.midiChannels[1] == 6U);
        const auto& pattern = state.sequencer.pattern;
        assert(pattern.length.get() == 16U && pattern.stepsPerBeat.get() == 4U);
        assert(state.sequencer.clip.loopEndTick == 4U * oc::note::clock::PPQN);
        constexpr uint8_t notes[] = {48U, 52U, 55U, 52U};
        for (uint8_t step = 0; step < 16U; ++step) {
            assert(pattern.isEnabled(step));
            assert(pattern.note[step] == notes[step % 4U]);
            assert(pattern.velocity[step] == 96U && pattern.gate[step] == 50U);
            assert(pattern.nudge[step] == 0 && pattern.probability[step] == 100U);
        }
        const auto* lanes = sequencerCcLaneView(pattern);
        assert(lanes && validSequencerCcLaneBank(*lanes));
        assert(sequencerCcLaneCount(*lanes) == 1U);
        assert(lanes->lanes[0].destination.controller == 1U);
        constexpr uint8_t values[] = {24U, 104U, 40U, 88U};
        for (uint8_t index = 0; index < 4U; ++index) {
            assert(lanes->lanes[0].values[index * 4U] == values[index]);
            assert(sequencerCcLaneTransition(lanes->lanes[0], index * 4U) ==
                   SequencerCcLaneTransition::LINEAR);
        }
        test_support::drainNotifications();
    }
    std::cout << "[PASS] bench-macro-v1 deterministic cold fixture; all filesystem operations fail closed\n";
}
