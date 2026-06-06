#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"

#include <array>

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

#include "state/sequencer/StepPropertyDisplay.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {
namespace {

constexpr std::array<core::state::sequencer::StepProperty, 5> STEP_EDIT_PROPERTIES = {
    core::state::sequencer::StepProperty::NOTE,
    core::state::sequencer::StepProperty::VELOCITY,
    core::state::sequencer::StepProperty::GATE,
    core::state::sequencer::StepProperty::NUDGE,
    core::state::sequencer::StepProperty::PROBABILITY,
};

constexpr std::array<const char*, 5> STEP_EDIT_KEYS = {
    "Note",
    "Velocity",
    "Gate",
    "Nudge",
    "Probability",
};

}  // namespace

FLASHMEM StepEditRenderData buildStepEditRenderData(const Source& source) {
    StepEditRenderData data{};
    auto& sequencer = source.sequencer;
    data.visible = sequencer.stepEdit.visible.get();
    if (!data.visible) {
        return data;
    }

    const uint8_t step = sequencer.stepEdit.stepIndex.get();
    if (step >= core::state::sequencer::SequencerState::MAX_STEPS) {
        data.visible = false;
        return data;
    }

    data.stepIndex = step;
    data.selectedIndex = sequencer.stepEdit.focusedRow.get();

    const uint8_t len = sequencer.pattern.length.get();
    size_t titlePos = oc::type::text::appendString(data.title.data(), data.title.size(), 0, "STEP ");
    titlePos = oc::type::text::appendUnsigned(
        data.title.data(),
        data.title.size(),
        titlePos,
        static_cast<unsigned>(step) + 1U
    );
    oc::type::text::terminate(data.title.data(), data.title.size(), titlePos);

    if (len > 0) {
        oc::type::text::formatFraction(
            data.meta.data(),
            data.meta.size(),
            static_cast<unsigned>(step) + 1U,
            static_cast<unsigned>(len)
        );
    } else {
        oc::type::text::formatUnsigned(data.meta.data(), data.meta.size(), static_cast<unsigned>(step) + 1U);
    }

    const uint8_t note = sequencer.pattern.note[step];
    const uint8_t velocity = sequencer.pattern.velocity[step];
    const uint16_t gate = sequencer.pattern.gate[step];
    const int8_t nudge = sequencer.pattern.nudge[step];
    const uint8_t probability = sequencer.pattern.probability[step];

    for (size_t i = 0; i < data.rows.size(); ++i) {
        core::state::sequencer::formatStepPropertyValue(
            data.valueBuffers[i].data(),
            data.valueBuffers[i].size(),
            STEP_EDIT_PROPERTIES[i],
            note,
            velocity,
            gate,
            nudge,
            probability
        );
        data.rows[i] = {
            .key = STEP_EDIT_KEYS[i],
            .value = data.valueBuffers[i].data(),
        };
    }

    data.dataRevision =
        sequencer.pattern.stepDataRevision.get() ^
        (static_cast<uint32_t>(step) << 16) ^
        (static_cast<uint32_t>(len) << 24);
    return data;
}

}  // namespace core::context::standalone::sequencer_overlay_presenter
