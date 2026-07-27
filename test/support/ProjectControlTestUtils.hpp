#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "../../src/state/macro/MacroAutomationDomain.hpp"
#include "../../src/state/modulation/ProjectControlMacroOps.hpp"
#include "../../src/state/modulation/ProjectModulationDomainOps.hpp"

namespace test_support::project_control {

namespace macro = core::state::macro;
namespace modulation = core::state::modulation;

struct ModulationShape {
    bool active = false;
    float durationBeats = 1.0f;
    uint16_t pointCount = 0U;
    std::array<
        macro::MacroCurvePoint,
        macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS
    > points{};
};

inline bool appendModulationPoint(
    ModulationShape& shape,
    float beat,
    float value
) {
    if (shape.pointCount >= shape.points.size() ||
        !std::isfinite(beat) ||
        beat < 0.0f ||
        (shape.pointCount > 0U &&
         beat < shape.points[shape.pointCount - 1U].beat)) {
        return false;
    }
    shape.points[shape.pointCount++] = {
        beat,
        macro::macroAutomationClampSigned(value),
    };
    shape.active = true;
    return true;
}

inline modulation::ProjectControlMacroDestinationView readSlot(
    const modulation::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
) {
    modulation::ProjectControlMacroDestinationView out{};
    assert(modulation::readProjectControlMacroDestination(
        control,
        address,
        out
    ));
    return out;
}

inline modulation::ProjectCurveRecord* mutableCurve(
    modulation::ProjectControlState& control,
    modulation::ProjectCurveId curveId
) {
    for (uint16_t index = 0;
         index < control.authored.curves.recordCount;
         ++index) {
        auto& record = control.authored.curves.records[index];
        if (record.id == curveId) return &record;
    }
    return nullptr;
}

inline bool assignAutomation(
    modulation::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationLane& lane
) {
    return modulation::assignProjectControlAutomation(control, address, lane);
}

inline modulation::ModulatorId addLocalLfo(
    modulation::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const char* name = "Test LFO"
) {
    modulation::ModulatorLfoDraft source{};
    source.name = name;
    source.parameters.periodTicks =
        modulation::PROJECT_CONTROL_TICKS_PER_BEAT;
    const auto created = modulation::createLfoModulator(
        control.authored.modulation,
        source
    );
    assert(created.changed());

    modulation::ModulationBindingDraft binding{};
    binding.sourceId = created.sourceId;
    binding.destination =
        modulation::projectControlDestination(address);
    binding.amountQ15 = 16384;
    assert(modulation::addProjectModulationBinding(
        control.authored.modulation,
        binding
    ).changed());
    control.markAuthoredMutation();
    return created.sourceId;
}

inline uint8_t outputBindingCountAt(
    const modulation::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
) {
    const auto destination =
        modulation::projectControlDestination(address);
    uint8_t count = 0U;
    for (uint16_t index = 0U;
         index < control.authored.modulation.outputBindingCount;
         ++index) {
        if (control.authored.modulation
                .outputBindings[index]
                .destination == destination) {
            ++count;
        }
    }
    return count;
}

inline bool assignModulation(
    modulation::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const ModulationShape& shape,
    float amount = 1.0f
) {
    if (!shape.active || shape.pointCount == 0U) {
        const auto current = readSlot(control, address);
        return current.modulationCount == 0U ||
               modulation::clearProjectControlModulation(control, address);
    }

    std::array<
        modulation::ProjectPackedCurvePoint,
        macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS> points{};
    const uint16_t durationTicks = macro::macroAutomationTicksFromBeats(
        shape.durationBeats
    );
    for (uint16_t index = 0; index < shape.pointCount; ++index) {
        const float beat = std::isfinite(shape.points[index].beat)
            ? std::max(shape.points[index].beat, 0.0f)
            : 0.0f;
        points[index] = {
            .tick = static_cast<uint16_t>(std::clamp<long>(
                std::lround(
                    beat * static_cast<float>(
                        macro::MACRO_AUTOMATION_TICKS_PER_BEAT
                    )
                ),
                0L,
                durationTicks
            )),
            .value = macro::macroAutomationPackValue(
                shape.points[index].value,
                true
            ),
        };
    }
    const modulation::ProjectControlCurvePayload curve{
        .spec = {
            .sourceDurationTicks = durationTicks,
            .durationTicks = durationTicks,
            .windowOffsetTicks = 0U,
            .interpolation =
                modulation::ProjectCurveInterpolation::LINEAR,
            .valueDomain = modulation::ProjectCurveValueDomain::BIPOLAR,
            .origin = modulation::ProjectCurveOrigin::NATIVE,
        },
        .pointOffset = 0,
        .pointCount = shape.pointCount,
        .enabled = true,
    };
    return modulation::replaceProjectControlRecordedShape(
        control,
        address,
        curve,
        amount,
        points.data(),
        shape.pointCount
    );
}

inline modulation::ProjectControlCurvePoint readCurvePoint(
    const modulation::ProjectControlState& control,
    modulation::ProjectCurveId curveId,
    uint16_t pointIndex,
    bool signedOutput
) {
    modulation::ProjectControlCurvePoint out{};
    assert(modulation::readProjectControlCurvePoint(
        control,
        curveId,
        pointIndex,
        signedOutput,
        out
    ));
    return out;
}

}  // namespace test_support::project_control
