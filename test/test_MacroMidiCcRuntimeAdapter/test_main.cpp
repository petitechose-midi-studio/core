#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IMidi.hpp>
#include <oc/type/Result.hpp>

#include "handler/common/MidiCcRuntimeAggregator.hpp"
#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"

namespace {

using core::state::shared::MidiCcCandidateClass;
using core::state::shared::MidiCcResolutionMode;

class MockMidiTransport final : public oc::interface::IMidi {
public:
    struct CcMessage {
        uint8_t channel = 0;
        uint8_t controller = 0;
        uint8_t value = 0;
    };

    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update() override {}
    void sendCC(uint8_t channel, uint8_t cc, uint8_t value) override {
        assert(count < messages.size());
        messages[count++] = CcMessage{channel, cc, value};
    }
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override {}
    void sendSysEx(const uint8_t*, std::size_t) override {}
    void sendProgramChange(uint8_t, uint8_t) override {}
    void sendPitchBend(uint8_t, int16_t) override {}
    void sendChannelPressure(uint8_t, uint8_t) override {}
    void sendClock() override {}
    void sendStart() override {}
    void sendStop() override {}
    void sendContinue() override {}
    void setOnCC(CCCallback) override {}
    void setOnNoteOn(NoteCallback) override {}
    void setOnNoteOff(NoteCallback) override {}
    void setOnSysEx(SysExCallback) override {}
    void setOnClock(ClockCallback) override {}
    void setOnStart(RealtimeCallback) override {}
    void setOnStop(RealtimeCallback) override {}
    void setOnContinue(RealtimeCallback) override {}

    std::array<CcMessage, 16> messages{};
    uint8_t count = 0;
};

struct Harness {
    core::state::MacroState macros;
    core::state::macro::MacroPagesState pages;
    core::state::macro::MacroUiState macroUi;
    oc::state::Signal<uint32_t> configRevision{0};
    core::state::StatusBarState statusBar;
    MockMidiTransport transport;
    oc::api::MidiAPI midi;
    core::handler::MacroPerformanceDomainServices services;
    core::handler::MidiCcRuntimeAggregator aggregator;
    core::handler::MacroMidiCcRuntimeAdapter adapter;

    Harness()
        : midi(transport)
        , services(
              core::handler::MacroPerformanceDomainServices::StateRefs{
                  macros,
                  pages,
                  macroUi,
                  configRevision,
                  statusBar,
              },
              core::handler::MacroPerformanceDomainServices::Operations{}
          )
        , aggregator(midi)
        , adapter(
              core::handler::MacroMidiCcRuntimeAdapter::StateRefs{
                  pages,
                  macroUi,
              },
              services,
              aggregator
          ) {
        pages.setMacroSlotActive(0, true);
        pages.setMacroSlotActive(1, true);
        pages.activePageData().cc[0] = 74;
        pages.activePageData().cc[1] = 74;
        pages.updateActiveConfigs();
        macros[0].value.set(0.20f);
        macros[1].value.set(0.80f);
    }

    void addAutomation(uint8_t macroIndex) {
        auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
            pages.automation,
            core::state::macro::MacroAutomationSlotAddress{
                .track = pages.currentActiveTrack(),
                .page = pages.currentActivePage(),
                .macro = macroIndex,
            }
        );
        assert(slot != nullptr);
        core::state::macro::MacroAutomationLane lane;
        assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, 0.0f));
        assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, 1.0f));
        assert(core::state::macro::macroAutomationAssignAutomation(
            pages.automation,
            *slot,
            lane
        ));
    }

    void addModulation(uint8_t macroIndex, float depth) {
        auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
            pages.automation,
            core::state::macro::MacroAutomationSlotAddress{
                .track = pages.currentActiveTrack(),
                .page = pages.currentActivePage(),
                .macro = macroIndex,
            }
        );
        assert(slot != nullptr);
        core::state::macro::MacroModulationShape shape;
        assert(core::state::macro::macroModulationAppendPoint(shape, 0.0f, -0.2f));
        assert(core::state::macro::macroModulationAppendPoint(shape, 1.0f, 0.2f));
        assert(core::state::macro::macroAutomationAssignModulation(
            pages.automation,
            *slot,
            shape
        ));
        slot->modulationDepth = depth;
    }
};

void test_manual_publish_collects_every_active_macro_in_one_frame() {
    Harness h;

    const auto result = h.adapter.publishLiveManual(1, 100);
    assert(result.ok());
    assert(result.candidateCount == 2);
    assert(result.destinationCount == 1);
    assert(result.conflictCount == 1);
    assert(result.sentCount == 1);
    assert(h.transport.count == 1);
    assert(h.transport.messages[0].value == 100);

    const auto& telemetry = h.adapter.telemetry();
    assert(telemetry.destinations[0].winner.author.candidateClass ==
           MidiCcCandidateClass::LIVE_MANUAL);
    assert(telemetry.destinations[0].winner.author.stableAddress == 1);
    assert(telemetry.losers[0].author.candidateClass ==
           MidiCcCandidateClass::MACRO_STATIC);
    assert(telemetry.losers[0].author.stableAddress == 0);

    std::cout << "[PASS] test_manual_publish_collects_every_active_macro_in_one_frame\n";
}

void test_computed_macro_beats_static_duplicate_and_preview_is_silent() {
    Harness h;
    h.addAutomation(1);

    h.adapter.beginComputedFrame();
    assert(h.adapter.setComputedValue(1, 90));
    const auto live = h.adapter.publishComputedFrame();
    assert(live.ok());
    assert(live.candidateCount == 2);
    assert(live.sentCount == 1);
    assert(h.transport.messages[0].value == 90);
    assert(h.adapter.telemetry().destinations[0].winner.author.candidateClass ==
           MidiCcCandidateClass::MACRO_COMPUTED);

    h.adapter.beginComputedFrame();
    assert(h.adapter.setComputedValue(1, 91));
    const auto preview = h.adapter.publishPreview();
    assert(preview.ok());
    assert(preview.sentCount == 0);
    assert(h.transport.count == 1);
    assert(h.adapter.telemetry().mode == MidiCcResolutionMode::PREVIEW);
    assert(h.adapter.telemetry().destinations[0].finalValue == 91);

    std::cout << "[PASS] test_computed_macro_beats_static_duplicate_and_preview_is_silent\n";
}

void test_manual_override_keeps_computed_contribution_as_loser() {
    Harness h;
    h.addAutomation(1);
    assert(h.services.takeManualControl(1, 0.75f));

    h.adapter.beginComputedFrame();
    assert(h.adapter.setComputedValue(1, 20));
    const auto result = h.adapter.publishComputedFrame();
    assert(result.ok());
    assert(result.candidateCount == 3);
    assert(result.sentCount == 1);
    assert(h.transport.messages[0].value >= 95 && h.transport.messages[0].value <= 96);

    const auto& telemetry = h.adapter.telemetry();
    assert(telemetry.destinations[0].winner.author.candidateClass ==
           MidiCcCandidateClass::LIVE_MANUAL);
    assert(telemetry.destinations[0].loserCount == 2);
    assert(telemetry.losers[0].author.candidateClass ==
           MidiCcCandidateClass::MACRO_COMPUTED);
    assert(telemetry.losers[0].localValue == 20);
    assert(telemetry.losers[1].author.candidateClass ==
           MidiCcCandidateClass::MACRO_STATIC);

    std::cout << "[PASS] test_manual_override_keeps_computed_contribution_as_loser\n";
}

void test_modulation_only_depth_zero_is_classified_as_computed() {
    Harness h;
    h.pages.setMacroSlotActive(0, false);
    h.addModulation(1, 0.0f);

    h.adapter.beginComputedFrame();
    assert(h.adapter.setComputedValue(1, 32));
    const auto result = h.adapter.publishComputedFrame();
    assert(result.ok());
    assert(result.candidateCount == 1);
    assert(result.sentCount == 1);
    assert(h.transport.messages[0].value == 32);
    assert(h.adapter.telemetry().destinations[0].winner.author.candidateClass ==
           MidiCcCandidateClass::MACRO_COMPUTED);
    assert(h.services.computedSourcePlaybackActiveFor(1));

    std::cout << "[PASS] test_modulation_only_depth_zero_is_classified_as_computed\n";
}

void test_disabling_automation_restores_persisted_static_base_not_runtime_projection() {
    Harness h;
    h.pages.setMacroSlotActive(0, false);
    h.pages.activePageData().values[1] = 0.25f;
    h.macros[1].value.set(0.90f);
    h.addAutomation(1);

    h.adapter.beginComputedFrame();
    assert(h.adapter.setComputedValue(1, 100));
    assert(h.adapter.publishComputedFrame().sentCount == 1);
    assert(h.transport.messages[0].value == 100);

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = h.pages.currentActiveTrack(),
        .page = h.pages.currentActivePage(),
        .macro = 1,
    };
    auto* slot = core::state::macro::macroAutomationFindMutableSlot(
        h.pages.automation,
        address
    );
    assert(slot != nullptr);
    slot->automation.active = false;

    h.adapter.beginComputedFrame();
    const auto fallback = h.adapter.publishComputedFrame();
    assert(fallback.ok());
    assert(fallback.sentCount == 1);
    assert(h.transport.count == 2);
    assert(h.transport.messages[1].value >= 31 && h.transport.messages[1].value <= 32);
    assert(h.adapter.telemetry().destinations[0].winner.author.candidateClass ==
           MidiCcCandidateClass::MACRO_STATIC);

    std::cout << "[PASS] test_disabling_automation_restores_persisted_static_base_not_runtime_projection\n";
}

void test_disabled_page_flushes_to_an_empty_bounded_frame_without_midi() {
    Harness h;
    assert(h.adapter.publishLiveManual(0, 60).sentCount == 1);
    h.pages.setPageEnabled(h.pages.currentActivePage(), false);

    const auto disabled = h.adapter.publishLiveManual(0, 61);
    assert(disabled.ok());
    assert(disabled.candidateCount == 0);
    assert(disabled.destinationCount == 0);
    assert(disabled.sentCount == 0);
    assert(h.transport.count == 1);

    h.pages.setPageEnabled(h.pages.currentActivePage(), true);
    const auto restored = h.adapter.publishLiveManual(0, 60);
    assert(restored.sentCount == 1);
    assert(h.transport.count == 2);

    std::cout << "[PASS] test_disabled_page_flushes_to_an_empty_bounded_frame_without_midi\n";
}

void test_macro_stable_address_covers_full_v1_domain_without_collision() {
    std::array<bool,
        core::state::macro::TRACK_COUNT *
        core::state::macro::PAGE_COUNT *
        core::state::macro::MACRO_COUNT> seen{};
    for (uint8_t track = 0; track < core::state::macro::TRACK_COUNT; ++track) {
        for (uint8_t page = 0; page < core::state::macro::PAGE_COUNT; ++page) {
            for (uint8_t macro = 0; macro < core::state::macro::MACRO_COUNT; ++macro) {
                const uint16_t address =
                    core::handler::MacroMidiCcRuntimeAdapter::stableAddress(
                        track,
                        page,
                        macro
                    );
                assert(address < seen.size());
                assert(!seen[address]);
                seen[address] = true;
            }
        }
    }

    std::cout << "[PASS] test_macro_stable_address_covers_full_v1_domain_without_collision\n";
}

}  // namespace

int main() {
    test_manual_publish_collects_every_active_macro_in_one_frame();
    test_computed_macro_beats_static_duplicate_and_preview_is_silent();
    test_manual_override_keeps_computed_contribution_as_loser();
    test_modulation_only_depth_zero_is_classified_as_computed();
    test_disabling_automation_restores_persisted_static_base_not_runtime_projection();
    test_disabled_page_flushes_to_an_empty_bounded_frame_without_midi();
    test_macro_stable_address_covers_full_v1_domain_without_collision();

    std::cout << "\nAll MacroMidiCcRuntimeAdapter tests passed.\n";
    return 0;
}
