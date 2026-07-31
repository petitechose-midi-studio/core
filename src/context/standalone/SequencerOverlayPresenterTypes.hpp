#pragma once

#include <array>
#include <cstdint>

#include "state/sequencer/SequencerStepEditRows.hpp"
#include "state/sequencer/SequencerUiState.hpp"
#include "ui/sequencer/SequencerStepEditOverlay.hpp"
#include "ui/sequencer/SequencerPresetLibraryPresentation.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::state {
struct StructureClipboardState;
}  // namespace core::state

namespace core::state::sequencer {
struct SequencerState;
struct SequencerTrackBankState;
}  // namespace core::state::sequencer

namespace core::context::standalone::sequencer_overlay_presenter {

struct StepEditKeyValueRow {
    const char* key = "";
    const char* value = "";
    const char* icon = "";
    const lv_font_t* iconFont = nullptr;
    uint32_t iconColor = 0;
};

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
    static constexpr size_t CHORD_FIELD_COUNT =
        static_cast<size_t>(core::state::sequencer::SequencerChordEditField::COUNT);

    std::array<std::array<char, 16>, ROW_COUNT> valueBuffers{};
    std::array<std::array<char, 12>, PROPERTY_COUNT> compactValueBuffers{};
    std::array<std::array<char, 16>, CHORD_FIELD_COUNT> chordValueBuffers{};
    std::array<std::array<char, 16>, 8> chordFormulaValueBuffers{};
    std::array<std::array<char, 8>, 8> chordFormulaIntervalBuffers{};
    std::array<std::array<char, 4>, 8> chordFormulaLabelBuffers{};
    std::array<std::array<char, 16>, 3> chordSourceValueBuffers{};
    std::array<StepEditKeyValueRow, ROW_COUNT> rows{};
    std::array<char, 8> stepBadge{};
    std::array<char, 24> summary{};
    std::array<char, 24> chordFieldTitle{};
    std::array<char, 24> chordName{};
    std::array<char, 32> chordDetail{};
    std::array<char, 16> chordFormula{};
    std::array<char, 16> chordContext{};
    std::array<char, 16> focusLabel{};
    core::ui::SequencerStepEditOverlayProps overlayProps{};
    std::array<char, 16> title{};
    std::array<char, 40> meta{};
    uint32_t dataRevision = 0;
    uint8_t stepIndex = 0;
    int selectedIndex = 0;
    int rowCount = 0;
    bool visible = false;
};

using PresetLibraryRenderData =
    core::ui::sequencer::SequencerPresetLibraryPresentation;

}  // namespace core::context::standalone::sequencer_overlay_presenter
