#pragma once

#include <array>
#include <cstdint>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/sequencer/SequencerStepEditOverlay.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {

struct Source {
    core::state::sequencer::SequencerState& sequencer;
    core::state::sequencer::SequencerTrackBankState& tracks;
};

struct ActionSource {
    core::state::sequencer::SequencerState& sequencer;
    core::state::sequencer::SequencerTrackBankState& tracks;
    core::state::StructureClipboardState& structureClipboard;
};

struct StepEditRenderData {
    static constexpr size_t ROW_COUNT = core::state::sequencer::step_edit_rows::COUNT;
    static constexpr size_t PROPERTY_COUNT =
        core::state::sequencer::step_edit_rows::PROPERTIES.size();

    std::array<std::array<char, 16>, ROW_COUNT> valueBuffers{};
    std::array<std::array<char, 12>, PROPERTY_COUNT> compactValueBuffers{};
    std::array<ms::ui::KeyValueRow, ROW_COUNT> rows{};
    std::array<char, 8> stepBadge{};
    std::array<char, 24> summary{};
    std::array<char, 16> focusLabel{};
    core::ui::SequencerStepEditOverlayProps overlayProps{};
    std::array<char, 16> title{};
    std::array<char, 16> meta{};
    uint32_t dataRevision = 0;
    uint8_t stepIndex = 0;
    int selectedIndex = 0;
    int rowCount = 0;
    bool visible = false;
};

StepEditRenderData buildStepEditRenderData(const Source& source);
core::ui::ContextActionStripProps buildStepEditActionStripProps(const ActionSource& source);

}  // namespace core::context::standalone::sequencer_overlay_presenter
