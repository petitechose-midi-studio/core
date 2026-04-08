#include "context/standalone/SequencerOverlayPresenter.hpp"

#include <array>
#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <oc/type/TextFormat.hpp>

#include "state/sequencer/StepPropertyDisplay.hpp"

namespace core::context::standalone {

namespace {

constexpr std::array<core::state::sequencer::StepProperty, 5> SEQUENCER_STEP_EDIT_PROPERTIES = {
    core::state::sequencer::StepProperty::NOTE,
    core::state::sequencer::StepProperty::VELOCITY,
    core::state::sequencer::StepProperty::GATE,
    core::state::sequencer::StepProperty::NUDGE,
    core::state::sequencer::StepProperty::PROBABILITY,
};

constexpr std::array<const char*, 5> SEQUENCER_STEP_EDIT_KEYS = {
    "Note",
    "Velocity",
    "Gate",
    "Nudge",
    "Probability",
};

template <size_t N>
FLASHMEM void formatSequencerStepEditRows(
    std::array<std::array<char, N>, 5>& valueBuffers,
    std::array<ms::ui::KeyValueRow, 5>& rows,
    uint8_t note,
    uint8_t velocity,
    uint16_t gate,
    int8_t nudge,
    uint8_t probability
) {
    for (size_t i = 0; i < rows.size(); ++i) {
        core::state::sequencer::formatStepPropertyValue(
            valueBuffers[i].data(),
            valueBuffers[i].size(),
            SEQUENCER_STEP_EDIT_PROPERTIES[i],
            note,
            velocity,
            gate,
            nudge,
            probability
        );
        rows[i] = {
            .key = SEQUENCER_STEP_EDIT_KEYS[i],
            .value = valueBuffers[i].data(),
        };
    }
}

}  // namespace

SequencerOverlayPresenter::SequencerOverlayPresenter(
    StateRefs stateRefs,
    ms::ui::VirtualListKeyValueOverlay& stepEditOverlay
)
    : state_refs_(stateRefs)
    , step_edit_overlay_(stepEditOverlay) {}

FLASHMEM void SequencerOverlayPresenter::bind() {
    step_edit_watcher_.watchAll(
        [this]() { renderStepEdit(); },
        state_refs_.sequencer.stepEdit.visible,
        state_refs_.sequencer.stepEdit.stepIndex,
        state_refs_.sequencer.stepEdit.focusedRow,
        state_refs_.sequencer.stepDataRevision
    );
}

FLASHMEM void SequencerOverlayPresenter::renderStepEdit() {
    const bool visible = state_refs_.sequencer.stepEdit.visible.get();
    if (!visible) {
        step_edit_overlay_.render({.visible = false});
        return;
    }

    const uint8_t abs = state_refs_.sequencer.stepEdit.stepIndex.get();
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    const uint8_t len = state_refs_.sequencer.length.get();

    char title[16];
    size_t titlePos = oc::type::text::appendString(title, sizeof(title), 0, "STEP ");
    titlePos = oc::type::text::appendUnsigned(title, sizeof(title), titlePos, static_cast<unsigned>(abs) + 1U);
    oc::type::text::terminate(title, sizeof(title), titlePos);

    char meta[16];
    if (len > 0) {
        oc::type::text::formatFraction(
            meta,
            sizeof(meta),
            static_cast<unsigned>(abs) + 1U,
            static_cast<unsigned>(len)
        );
    } else {
        oc::type::text::formatUnsigned(meta, sizeof(meta), static_cast<unsigned>(abs) + 1U);
    }

    const uint8_t note = state_refs_.sequencer.note[abs];
    const uint8_t vel = state_refs_.sequencer.velocity[abs];
    const uint16_t gate = state_refs_.sequencer.gate[abs];
    const int8_t nudge = state_refs_.sequencer.nudge[abs];
    const uint8_t probability = state_refs_.sequencer.probability[abs];

    const uint32_t dataRevision =
        state_refs_.sequencer.stepDataRevision.get() ^
        (static_cast<uint32_t>(abs) << 16) ^
        (static_cast<uint32_t>(len) << 24);

    std::array<std::array<char, 12>, 5> valueBuffers{};
    std::array<ms::ui::KeyValueRow, 5> rows{};
    formatSequencerStepEditRows(valueBuffers, rows, note, vel, gate, nudge, probability);

    step_edit_overlay_.render({
        .title = title,
        .meta = meta,
        .rows = rows.data(),
        .rowCount = static_cast<int>(rows.size()),
        .selectedIndex = state_refs_.sequencer.stepEdit.focusedRow.get(),
        .visible = true,
        .dataRevision = dataRevision,
    });
}

}  // namespace core::context::standalone
