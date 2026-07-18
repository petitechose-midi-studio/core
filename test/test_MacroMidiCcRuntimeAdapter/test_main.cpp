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

#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "sequencer/RealtimeMidiQueue.hpp"

namespace {

using core::state::shared::MidiCcCandidateClass;

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
    core::sequencer::RealtimeMidiQueue queue;
    core::handler::MidiCcGlobalFrameCoordinator coordinator;
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
        , coordinator(queue)
        , adapter(
              core::handler::MacroMidiCcRuntimeAdapter::StateRefs{
                  pages,
              },
              services,
              coordinator
          ) {
        pages.setMacroSlotActive(0, true);
        pages.setMacroSlotActive(1, true);
        pages.activePageData().cc[0] = 74;
        pages.activePageData().cc[1] = 74;
        pages.updateActiveConfigs();
        macros[0].value.set(0.20f);
        macros[1].value.set(0.80f);
    }

    core::handler::MidiCcGlobalFrameResult resolveAndDrain(uint32_t deadlineUs = 0) {
        const auto result = coordinator.resolveLive(deadlineUs);
        queue.drainDue(midi, deadlineUs, UINT32_MAX);
        return result;
    }

};

core::state::shared::MidiCcCandidate makeCandidate(
    uint8_t track,
    uint8_t page,
    uint8_t macro,
    uint8_t channel,
    uint8_t controller,
    uint8_t value,
    MidiCcCandidateClass candidateClass
) {
    return {
        .destination = {
            .identity = {
                .port = core::handler::MidiCcGlobalFrameCoordinator::OUTPUT_PORT,
                .channel = channel,
                .controller = controller,
            },
            .routeValidity = core::state::shared::MidiCcRouteValidity::VALID,
        },
        .author = {
            .candidateClass = candidateClass,
            .stableAddress =
                core::handler::MacroMidiCcRuntimeAdapter::stableAddress(
                    track, page, macro
                ),
        },
        .localValue = value,
    };
}

void test_manual_replace_preserves_complete_cross_track_frame() {
    Harness h;
    const std::array authors{
        makeCandidate(0U, 0U, 0U, 0U, 74U, 20U,
                      MidiCcCandidateClass::MACRO_STATIC),
        makeCandidate(0U, 0U, 1U, 0U, 74U, 80U,
                      MidiCcCandidateClass::MACRO_COMPUTED),
        makeCandidate(1U, 0U, 0U, 1U, 10U, 55U,
                      MidiCcCandidateClass::MACRO_STATIC),
    };
    assert(h.coordinator.publishPersistentAuthors(authors.data(), authors.size()));

    const auto result = h.adapter.publishLiveManual(1U, 100U);
    assert(result.ok());
    assert(result.candidateCount == authors.size());
    const auto resolved = h.resolveAndDrain();
    assert(resolved.destinationCount == 2U);
    assert(resolved.conflictCount == 1U);
    assert(resolved.queuedEmissionCount == 2U);

    auto telemetry = h.coordinator.readTelemetry();
    assert(telemetry);
    bool foundManual = false;
    bool foundRemote = false;
    for (uint16_t index = 0U; index < telemetry->destinationCount; ++index) {
        const auto& winner = telemetry->destinations[index].winner;
        if (winner.author.stableAddress ==
            core::handler::MacroMidiCcRuntimeAdapter::stableAddress(0U, 0U, 1U)) {
            foundManual = winner.author.candidateClass ==
                              MidiCcCandidateClass::LIVE_MANUAL &&
                          winner.localValue == 100U;
        }
        if (winner.author.stableAddress ==
            core::handler::MacroMidiCcRuntimeAdapter::stableAddress(1U, 0U, 0U)) {
            foundRemote = winner.localValue == 55U;
        }
    }
    assert(foundManual && foundRemote);
    std::cout << "[PASS] Manual replace preserves the complete Project frame\n";
}

void test_missing_stable_author_rejects_without_local_fallback() {
    Harness h;
    const auto remote = makeCandidate(
        1U, 0U, 0U, 1U, 10U, 55U, MidiCcCandidateClass::MACRO_STATIC
    );
    assert(h.coordinator.publishPersistentAuthors(&remote, 1U));

    const auto result = h.adapter.publishLiveManual(1U, 100U);
    assert(!result.ok());
    const auto resolved = h.resolveAndDrain();
    assert(resolved.destinationCount == 1U);
    assert(resolved.queuedEmissionCount == 1U);
    auto telemetry = h.coordinator.readTelemetry();
    assert(telemetry && telemetry->destinationCount == 1U);
    assert(telemetry->destinations[0].winner.author.stableAddress ==
           remote.author.stableAddress);
    std::cout << "[PASS] Missing author cannot trigger a lossy local fallback\n";
}

void test_inactive_context_rejects_manual_replace_without_mutation() {
    Harness h;
    const auto author = makeCandidate(
        0U, 0U, 0U, 0U, 74U, 20U, MidiCcCandidateClass::MACRO_STATIC
    );
    assert(h.coordinator.publishPersistentAuthors(&author, 1U));
    h.pages.setPageEnabled(h.pages.currentActivePage(), false);
    assert(!h.adapter.publishLiveManual(0U, 61U).ok());

    const auto resolved = h.resolveAndDrain();
    assert(resolved.destinationCount == 1U);
    auto telemetry = h.coordinator.readTelemetry();
    assert(telemetry && telemetry->destinations[0].winner.localValue == 20U);
    std::cout << "[PASS] Inactive context leaves the authoritative frame untouched\n";
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
    test_manual_replace_preserves_complete_cross_track_frame();
    test_missing_stable_author_rejects_without_local_fallback();
    test_inactive_context_rejects_manual_replace_without_mutation();
    test_macro_stable_address_covers_full_v1_domain_without_collision();

    std::cout << "\nAll MacroMidiCcRuntimeAdapter tests passed.\n";
    return 0;
}
