#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "../../src/state/macro/MacroAutomationDomain.hpp"
#include "../../src/state/modulation/ProjectControlMacroOps.hpp"

namespace test_support::project_control {

namespace macro = core::state::macro;
namespace modulation = core::state::modulation;

inline modulation::ProjectControlMacroSlotView readSlot(
    const modulation::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
) {
    modulation::ProjectControlMacroSlotView out{};
    assert(modulation::readProjectControlMacroSlot(control, address, out));
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

inline bool assignModulation(
    modulation::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroModulationShape& shape,
    float amount = 1.0f
) {
    if (!shape.active || shape.pointCount == 0U) {
        const auto current = readSlot(control, address);
        return !current.modulationStored ||
               modulation::clearProjectControlModulation(control, address);
    }

    std::array<
        macro::MacroPackedCurvePoint,
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
    const macro::MacroAutomationCurveRef curve{
        .active = true,
        .playbackState = macro::MacroCurvePlaybackState::ACTIVE,
        .pointOffset = 0,
        .pointCount = shape.pointCount,
        .sourceDurationTicks = durationTicks,
        .durationTicks = durationTicks,
        .windowOffsetTicks = 0,
        .interpolation = shape.interpolation,
        .modulationOrigin = macro::MacroModulationOrigin::NATIVE,
    };
    return modulation::replaceProjectControlModulation(
        control,
        address,
        curve,
        amount,
        points.data(),
        shape.pointCount
    );
}

inline macro::MacroCurvePoint readCurvePoint(
    const modulation::ProjectControlState& control,
    modulation::ProjectCurveId curveId,
    uint16_t pointIndex,
    bool signedOutput
) {
    macro::MacroCurvePoint out{};
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
