#pragma once

#include <array>
#include <cstdint>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>

#include "state/sequencer/SequencerState.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {

struct Source {
    core::state::sequencer::SequencerState& sequencer;
};

struct StepEditRenderData {
    static constexpr size_t ROW_COUNT = 7;

    std::array<std::array<char, 16>, ROW_COUNT> valueBuffers{};
    std::array<ms::ui::KeyValueRow, ROW_COUNT> rows{};
    std::array<char, 16> title{};
    std::array<char, 16> meta{};
    uint32_t dataRevision = 0;
    uint8_t stepIndex = 0;
    int selectedIndex = 0;
    int rowCount = 0;
    bool visible = false;
};

StepEditRenderData buildStepEditRenderData(const Source& source);

}  // namespace core::context::standalone::sequencer_overlay_presenter
