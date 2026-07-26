#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

#include "../../src/ui/modulation/ModulatorSparklineModel.cpp"

namespace {

namespace mod = core::state::modulation;
namespace sparkline = core::ui::modulation::sparkline;

ms::ui::KeyValueSparklineSample sampleAt(
    const ms::ui::KeyValueSparkline& descriptor,
    uint16_t position,
    uint16_t previous = 0U,
    bool hasPrevious = false
) {
    ms::ui::KeyValueSparklineSample out{};
    assert(descriptor.sampleProvider != nullptr);
    assert(descriptor.sampleProvider(
        descriptor,
        position,
        previous,
        hasPrevious,
        out
    ));
    return out;
}

void testLfoSamplesAtPhysicalColumnsAndUsesSharedPhase() {
    auto control = std::make_unique<mod::ProjectControlState>();
    mod::ModulatorLfoDraft draft{};
    draft.name = "Shape";
    draft.parameters.shape = mod::ModulatorLfoShape::SQUARE;
    draft.parameters.phaseQ15 = 8192;
    const auto created = mod::createLfoModulator(
        control->authored.modulation,
        draft
    );
    assert(created.changed());
    const auto* source = mod::findProjectModulator(
        control->authored.modulation,
        created.sourceId
    );
    assert(source != nullptr);
    const auto descriptor = sparkline::buildSource(*control, *source);
    assert(descriptor.enabled);
    assert(descriptor.centerLine);
    for (const std::size_t width : {58U, 110U}) {
        std::size_t discontinuities = 0U;
        for (std::size_t column = 0U; column < width; ++column) {
            const uint16_t position = ms::ui::keyValueSparklinePositionQ16(
                column,
                width
            );
            const auto sample = sampleAt(
                descriptor,
                position,
                column > 0U
                    ? ms::ui::keyValueSparklinePositionQ16(column - 1U, width)
                    : 0U,
                column > 0U
            );
            assert(sample.valueQ16 == 0U || sample.valueQ16 == 65535U);
            if (sample.discontinuityBefore) ++discontinuities;
        }
        assert(discontinuities == 2U);
    }
    const auto authoredPhase = mod::projectLfoShapePositionQ16(0U, 8192);
    const auto first = sampleAt(descriptor, 0U);
    const float exact = mod::evaluateProjectLfoShape(
        mod::ModulatorLfoShape::SQUARE,
        static_cast<float>(authoredPhase) / 65535.0f
    );
    assert(first.valueQ16 == (exact > 0.0f ? 65535U : 0U));
}

void testGeometryRevisionExcludesNonGraphicalFacts() {
    auto control = std::make_unique<mod::ProjectControlState>();
    mod::ModulatorLfoDraft draft{};
    draft.name = "Stable";
    draft.parameters.shape = mod::ModulatorLfoShape::TRIANGLE;
    const auto created = mod::createLfoModulator(
        control->authored.modulation,
        draft
    );
    assert(created.changed());
    auto* source = mod::findProjectModulator(
        control->authored.modulation,
        created.sourceId
    );
    assert(source != nullptr);
    const auto initial = sparkline::buildSource(*control, *source);

    std::strncpy(source->name.data(), "Renamed", source->name.size() - 1U);
    source->flags = 0U;
    source->parameters.lfo.periodTicks = 3072U;
    source->parameters.lfo.freePeriodMs = 9876U;
    source->parameters.lfo.timing = mod::ModulatorTimingMode::FREE;
    source->parameters.lfo.retrigger =
        mod::ModulatorRetriggerPolicy::EXPLICIT_TRIGGER;
    control->authored.modulation.outputBindingCount = 1U;
    control->authored.modulation.outputBindings[0].sourceId = source->id;
    const auto nonGraphical = sparkline::buildSource(*control, *source);
    assert(nonGraphical.geometryRevision == initial.geometryRevision);

    source->parameters.lfo.phaseQ15 = -12000;
    const auto phaseChanged = sparkline::buildSource(*control, *source);
    assert(phaseChanged.geometryRevision != initial.geometryRevision);
    source->parameters.lfo.shape = mod::ModulatorLfoShape::SAW_UP;
    const auto shapeChanged = sparkline::buildSource(*control, *source);
    assert(shapeChanged.geometryRevision != phaseChanged.geometryRevision);
}

void testAdsrIsPositiveAndDescriptorFailsClosedWhenSourceDisappears() {
    auto control = std::make_unique<mod::ProjectControlState>();
    mod::ModulatorAdsrDraft draft{};
    draft.name = "Envelope";
    draft.parameters.delay = 0U;
    draft.parameters.attack = 64U;
    draft.parameters.hold = 0U;
    draft.parameters.decay = 64U;
    draft.parameters.sustainQ15 = 16384U;
    draft.parameters.release = 64U;
    const auto created = mod::createAdsrModulator(
        control->authored.modulation,
        draft
    );
    assert(created.changed());
    const auto* source = mod::findProjectModulator(
        control->authored.modulation,
        created.sourceId
    );
    assert(source != nullptr);
    const auto descriptor = sparkline::buildSource(*control, *source);
    assert(descriptor.enabled);
    assert(!descriptor.centerLine);
    assert(sampleAt(descriptor, 0U).valueQ16 == 0U);
    const auto peak = sampleAt(
        descriptor,
        core::ui::modulation::adsr::previewBoundaries(
            source->parameters.adsr
        ).attackEndQ16
    );
    assert(peak.valueQ16 == 65535U);

    control->authored.modulation.sourceCount = 0U;
    ms::ui::KeyValueSparklineSample missing{};
    assert(!descriptor.sampleProvider(
        descriptor,
        0U,
        0U,
        false,
        missing
    ));
    ms::ui::KeyValueSparklineMarker marker{.visible = true};
    assert(!descriptor.markerProvider(descriptor, 0U, marker));
    assert(!marker.visible);
}

}  // namespace

int main() {
    static_assert(sizeof(ms::ui::KeyValueSparkline) <= 40U);
    testLfoSamplesAtPhysicalColumnsAndUsesSharedPhase();
    testGeometryRevisionExcludesNonGraphicalFacts();
    testAdsrIsPositiveAndDescriptorFailsClosedWhenSourceDisappears();
    std::cout << "ModulatorSparklineModel tests passed\n";
    return 0;
}
