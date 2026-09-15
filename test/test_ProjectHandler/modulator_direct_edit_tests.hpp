#pragma once

// Included beside the real ProjectHandler harness, not a second input simulator.
namespace direct_edit_tests {
using namespace core::state::modulation;
using Mode = ProjectModulatorSourceSessionMode;

ModulatorId openSource(ProjectHandlerHarness& h, ModulatorKind kind, Mode mode,
                       ModulatorTimingMode timing = ModulatorTimingMode::SYNC) {
    auto& pages = h.state.pages;
    pages.setMacroSlotActive(0U, true);
    ModulatorLfoDraft lfo{};
    lfo.name = "Direct LFO";
    lfo.parameters.timing = timing;
    ModulatorAdsrDraft adsr{};
    adsr.name = "Direct envelope";
    adsr.parameters.traits = withModulatorAdsrTiming(adsr.parameters.traits, timing);
    ModulationBindingDraft binding{};
    binding.destination = projectControlDestination({0U, 0U, 0U});
    binding.amountQ15 = 8192;
    ProjectModulationResult created{};
    if (mode == Mode::AUDITION_NEW) {
        created = kind == ModulatorKind::LFO
            ? h.state.macroHistory.beginLfoModulatorAudition(pages, {0, 0, 0}, lfo, binding)
            : h.state.macroHistory.beginAdsrModulatorAudition(pages, {0, 0, 0}, adsr, {}, binding);
    } else {
        created = kind == ModulatorKind::LFO
            ? createLfoModulator(pages.control.authored().modulation, lfo)
            : createAdsrModulator(pages.control.authored().modulation, adsr);
        if (mode == Mode::AUDITION_EXISTING) {
            assert(h.state.macroHistory.beginExistingModulatorAudition(
                pages, {0, 0, 0}, created.sourceId, binding).changed());
        }
    }
    assert(created.changed());
    assert(core::state::project::openProjectModulatorWorkspace(
        h.state.projectNavigation, created.sourceId));
    h.state.activeView.set(core::ui::ViewType::MODULATORS);
    h.handler.syncFocusedEncoder();
    return created.sourceId;
}

auto& source(ProjectHandlerHarness& h) {
    auto* result = findProjectModulator(h.state.pages.control.authored().modulation,
        h.state.projectNavigation.selectedModulator);
    assert(result);
    return *result;
}

void test_opt_equivalence() {
    unsigned comparisons = 0;
    for (auto kind : {ModulatorKind::LFO, ModulatorKind::ADSR}) {
        for (auto mode : {Mode::DURABLE_PROJECT, Mode::AUDITION_NEW, Mode::AUDITION_EXISTING}) {
          for (auto timing : {ModulatorTimingMode::SYNC, ModulatorTimingMode::FREE}) {
            ProjectHandlerHarness direct, opt;
            openSource(direct, kind, mode, timing);
            openSource(opt, kind, mode, timing);
            // Expected main-screen rows are deliberately independent of the resolver.
            const std::array<int, 8> rows = kind == ModulatorKind::LFO
                ? (mode == Mode::DURABLE_PROJECT
                    ? std::array<int, 8>{0, 2, 1, -1, -1, -1, -1, -1}
                    : std::array<int, 8>{0, 1, -1, -1, -1, -1, -1, 2})
                : std::array<int, 8>{0, 1, 2, 3, -1, -1, -1,
                    mode == Mode::DURABLE_PROJECT ? -1 : 6};
            for (uint8_t index = 0U; index < 8U; ++index) {
                if (rows[index] < 0 || (mode == Mode::AUDITION_EXISTING && index != 7U)) continue;
                opt.state.projectNavigation.focusedRow.set(static_cast<uint8_t>(rows[index]));
                opt.state.macroHistory.endCoalescing();
                opt.handler.syncFocusedEncoder();
                const auto encoder = Config::MACRO_ENCODERS[index];
                const auto directId = static_cast<oc::type::EncoderID>(encoder);
                const auto optId = static_cast<oc::type::EncoderID>(Config::EncoderID::OPT);
                assert(direct.encoderHw.getDiscreteSteps(directId) == opt.encoderHw.getDiscreteSteps(optId));
                assert(near(direct.encoders.getPosition(encoder), opt.encoders.getPosition(Config::EncoderID::OPT)));
                for (float value : {-0.2f, 0.0f, 0.13f, 0.5f, 0.87f, 1.0f, 1.2f}) {
                    direct.turn(encoder, value);
                    opt.turn(Config::EncoderID::OPT, value);
                    assert(std::memcmp(&source(direct), &source(opt), sizeof(ModulatorSourceState)) == 0);
                    const auto& a = direct.state.pages.control.authored().modulation;
                    const auto& b = opt.state.pages.control.authored().modulation;
                    assert(std::memcmp(a.outputBindings.data(), b.outputBindings.data(),
                        sizeof(a.outputBindings)) == 0);
                    assert(direct.state.projectNavigation.focusedRow.get() == rows[index]);
                    // OPT must immediately continue the field selected by the direct turn.
                    opt.handler.syncFocusedEncoder();
                    assert(near(direct.encoders.getPosition(Config::EncoderID::OPT),
                        opt.encoders.getPosition(Config::EncoderID::OPT)));
                    ++comparisons;
                }
            }
          }
        }
    }
    std::printf("[PASS] Direct encoder / OPT equivalence: %u mutations, bounds and positions\n", comparisons);
}

void test_history_and_rejection() {
    ProjectHandlerHarness h;
    openSource(h, ModulatorKind::LFO, Mode::DURABLE_PROJECT);
    const auto initial = source(h);
    h.turn(Config::EncoderID::MACRO_1, 0.5f);
    h.turn(Config::EncoderID::MACRO_1, 1.0f);
    const auto shape = source(h);
    assert(h.state.macroHistory.undoCount() == 1U);
    h.turn(Config::EncoderID::MACRO_2, 1.0f);
    const auto rate = source(h);
    h.turn(Config::EncoderID::MACRO_1, 0.0f);
    const auto final = source(h);
    assert(h.state.macroHistory.undoCount() == 3U);
    h.tap(Config::ButtonID::LEFT_TOP);
    for (const auto& expected : {rate, shape, initial}) {
        projectModulatorUndo(h);
        const auto* actual = findProjectModulator(h.state.pages.control.authored().modulation, initial.id);
        assert(actual && std::memcmp(actual, &expected, sizeof(expected)) == 0);
    }
    for (const auto& expected : {shape, rate, final}) {
        projectModulatorRedo(h);
        const auto* actual = findProjectModulator(h.state.pages.control.authored().modulation, initial.id);
        assert(actual && std::memcmp(actual, &expected, sizeof(expected)) == 0);
    }
    assert(core::state::project::openProjectModulatorWorkspace(h.state.projectNavigation, initial.id));
    h.handler.syncFocusedEncoder();
    const auto undoBefore = h.state.macroHistory.undoCount();
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
    }
    assert(std::memcmp(&source(h), &final, sizeof(final)) == 0);
    assert(h.state.macroHistory.undoCount() == undoBefore);
    assert(near(h.encoders.getPosition(Config::EncoderID::MACRO_1), 0.0f));
    std::puts("[PASS] E1/E1/E2/E1 history boundaries, exact Undo/Redo, allocation refusal rebases");
}

void test_permissions_and_lifecycle() {
    for (auto kind : {ModulatorKind::LFO, ModulatorKind::ADSR}) {
        ProjectHandlerHarness h;
        openSource(h, kind, Mode::AUDITION_EXISTING);
        const auto before = source(h);
        for (uint8_t i = 0U; i < 7U; ++i) h.turn(Config::MACRO_ENCODERS[i], 1.0f);
        assert(std::memcmp(&source(h), &before, sizeof(before)) == 0);
        h.turn(Config::EncoderID::MACRO_8, 0.75f);
        assert(h.state.pages.control.authored().modulation.outputBindings[0].amountQ15 == 16384);
        const auto audition = h.state.pages.control.audition;
        // Nonzero but wrong generation must be rejected too, not just zero.
        for (auto generation : {0U, audition.generation + 1U}) {
            h.state.pages.control.audition.generation = generation;
            h.turn(Config::EncoderID::MACRO_8, 1.0f);
            assert(h.state.pages.control.authored().modulation.outputBindings[0].amountQ15 == 16384);
        }
        h.state.pages.control.audition = audition;
        for (unsigned invalid = 0U; invalid < 3U; ++invalid) {
            h.state.pages.control.audition = audition;
            if (invalid == 0U) h.state.pages.control.audition.bindingId = {};
            if (invalid == 1U) ++h.state.pages.control.audition.destination.macro;
            if (invalid == 2U) h.state.pages.control.audition.mode = static_cast<Mode>(255U);
            h.turn(Config::EncoderID::MACRO_8, 1.0f);
            assert(h.state.pages.control.authored().modulation.outputBindings[0].amountQ15 == 16384);
        }
        h.state.pages.control.audition = audition;
        h.handler.syncFocusedEncoder();
        h.tap(Config::ButtonID::BOTTOM_RIGHT);
        assert(h.state.macroHistory.undoCount() == 1U);
        assert(std::memcmp(&h.state.pages.control.authored().modulation.sources[0], &before, sizeof(before)) == 0);
    }
    {
        ProjectHandlerHarness h;
        const auto graphBefore = h.state.pages.control.authored().modulation;
        openMacroCreatedLfoWorkspace(h);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        h.turn(Config::EncoderID::MACRO_2, 0.0f);
        h.turn(Config::EncoderID::MACRO_8, 0.25f);
        assert(source(h).parameters.lfo.shape == ModulatorLfoShape::SQUARE);
        h.tap(Config::ButtonID::LEFT_TOP);
        assert(h.state.activeView.get() == core::ui::ViewType::MACRO);
        assert(h.state.macroHistory.undoCount() == 0U);
        assert(std::memcmp(&h.state.pages.control.authored().modulation, &graphBefore, sizeof(graphBefore)) == 0);
    }
    {
        ProjectHandlerHarness h;
        const auto id = openSource(h, ModulatorKind::LFO, Mode::DURABLE_PROJECT);
        ModulationBindingDraft binding{};
        binding.sourceId = id;
        binding.amountQ15 = 8192;
        for (uint8_t macro = 0; macro < 2; ++macro) {
            binding.destination = projectControlDestination({0, 0, macro});
            assert(addProjectModulationBinding(h.state.pages.control.authored().modulation, binding).changed());
        }
        const auto before = h.state.pages.control.authored().modulation;
        h.turn(Config::EncoderID::MACRO_8, 1.0f);
        assert(std::memcmp(&h.state.pages.control.authored().modulation, &before, sizeof(before)) == 0);
    }
    std::puts("[PASS] Shared audition Depth only, generation admission, atomic Apply/Cancel, no implied live Depth");
}

void test_exclusions_and_resync() {
    ProjectHandlerHarness h;
    const auto id = openSource(h, ModulatorKind::LFO, Mode::DURABLE_PROJECT);
    const auto initial = source(h);
    for (auto button : {Config::ButtonID::NAV, Config::ButtonID::LEFT_CENTER, Config::ButtonID::LEFT_TOP,
                        Config::ButtonID::BOTTOM_LEFT, Config::ButtonID::BOTTOM_RIGHT}) {
        h.press(button);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(std::memcmp(&source(h), &initial, sizeof(initial)) == 0);
        // Release without performing the button action: this section tests gating.
        h.buttonHw.setPressed(static_cast<oc::type::ButtonID>(button), false);
        h.state.projectNavigation.physicalHoldActive.set(false);
        h.state.projectNavigation.modulatorGuard.set({});
        h.state.projectNavigation.modulatorClipboardGuard.set({});
        h.handler.update(g_now_ms);
        assert(near(h.encoders.getPosition(Config::EncoderID::MACRO_1), 0.0f));
    }
    for (auto node : {ProjectNodeId::MODULATOR_SOURCE_OPTIONS, ProjectNodeId::MODULATOR_TRIGGER,
        ProjectNodeId::MODULATOR_DESTINATIONS, ProjectNodeId::MODULATOR_DESTINATION_PICKER,
        ProjectNodeId::MODULATORS_ROOT}) {
        h.state.projectNavigation.currentNode.set(node);
        h.handler.syncFocusedEncoder();
        for (auto encoder : Config::MACRO_ENCODERS) h.turn(encoder, 1.0f);
        assert(std::memcmp(&source(h), &initial, sizeof(initial)) == 0);
    }
    assert(core::state::project::openProjectModulatorWorkspace(h.state.projectNavigation, id));
    for (auto view : {core::ui::ViewType::MACRO, core::ui::ViewType::CLIPS, core::ui::ViewType::PROJECT}) {
        h.state.activeView.set(view);
        h.encoders.setPosition(Config::EncoderID::MACRO_1, 0.37f);
        h.handler.update(g_now_ms);
        assert(near(h.encoders.getPosition(Config::EncoderID::MACRO_1), 0.37f));
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(std::memcmp(&source(h), &initial, sizeof(initial)) == 0);
    }
    h.state.activeView.set(core::ui::ViewType::MODULATORS);
    // Lifecycle sync also restores after an overlay opens/closes within a turn,
    // without relying on the periodic Project update having observed it.
    h.state.overlays.show(core::ui::OverlayType::VIEW_SELECTOR);
    h.encoders.setPosition(Config::EncoderID::MACRO_1, 0.8f);
    h.state.overlays.hide();
    h.handler.syncFocusedEncoder();
    assert(near(h.encoders.getPosition(Config::EncoderID::MACRO_1), 0.0f));
    for (auto overlay : {core::ui::OverlayType::VIEW_SELECTOR, core::ui::OverlayType::SEQ_STEP_EDIT,
                        core::ui::OverlayType::SEQ_CC_LANE, core::ui::OverlayType::MACRO_EDIT}) {
        h.state.overlays.show(overlay);
        h.handler.update(g_now_ms);
        h.turn(Config::EncoderID::MACRO_1, 1.0f);
        assert(std::memcmp(&source(h), &initial, sizeof(initial)) == 0);
        h.state.overlays.hide();
        h.handler.update(g_now_ms);
        assert(near(h.encoders.getPosition(Config::EncoderID::MACRO_1), 0.0f));
    }
    // An OPT change to Timing resynchronizes E2 before the next physical turn.
    h.state.projectNavigation.focusedRow.set(1U);
    h.handler.syncFocusedEncoder();
    h.turn(Config::EncoderID::OPT, 1.0f);
    const auto e2 = static_cast<oc::type::EncoderID>(Config::EncoderID::MACRO_2);
    assert(h.encoderHw.getDiscreteSteps(e2) == 13U);
    h.turn(Config::EncoderID::MACRO_2, 0.0f);
    assert(source(h).parameters.lfo.freePeriodMs == 8U);
    h.turn(Config::EncoderID::MACRO_3, 0.0f);
    assert(h.encoderHw.getDiscreteSteps(e2) != 13U);
    // A structural publication/source switch rebases a stale normalized event.
    ModulatorLfoDraft other{};
    other.parameters.shape = ModulatorLfoShape::SQUARE;
    const auto created = createLfoModulator(h.state.pages.control.authored().modulation, other);
    assert(created.changed());
    h.state.projectNavigation.selectedModulator = created.sourceId;
    h.turn(Config::EncoderID::MACRO_1, 0.0f);
    assert(source(h).parameters.lfo.shape == ModulatorLfoShape::SQUARE);
    assert(near(h.encoders.getPosition(Config::EncoderID::MACRO_1), 1.0f));
    std::puts("[PASS] Guards, held layer, overlays, excluded views/pages, Timing and source resynchronization");
}

void test_physical_ticks() {
    ProjectHandlerHarness h;
    h.encoderHw.enableTicks();
    openSource(h, ModulatorKind::LFO, Mode::DURABLE_PROJECT);
    for (int tick = 0; tick < 100; ++tick) {
        h.encoderHw.ticks(Config::EncoderID::MACRO_1, 1, h.eventBus);
        h.encoderHw.ticks(Config::EncoderID::MACRO_2, 1, h.eventBus);
        h.handler.update(g_now_ms);
    }
    assert(source(h).parameters.lfo.shape == ModulatorLfoShape::SQUARE);
    assert(near(h.encoders.getPosition(Config::EncoderID::MACRO_2), 1.0f));
    for (int tick = 0; tick < 100; ++tick) {
        h.encoderHw.ticks(Config::EncoderID::MACRO_1, -1, h.eventBus);
        h.encoderHw.ticks(Config::EncoderID::MACRO_2, -1, h.eventBus);
    }
    assert(source(h).parameters.lfo.shape == ModulatorLfoShape::SINE);
    assert(near(h.encoders.getPosition(Config::EncoderID::MACRO_2), 0.0f));
    assert(near(h.encoders.getPosition(Config::EncoderID::OPT), 0.0f));
    std::puts("[PASS] Physical ticks: interleaved E1/E2 cross all detents in both directions");
}

void run() {
    test_opt_equivalence();
    test_history_and_rejection();
    test_permissions_and_lifecycle();
    test_exclusions_and_resync();
    test_physical_ticks();
}
} // namespace direct_edit_tests
