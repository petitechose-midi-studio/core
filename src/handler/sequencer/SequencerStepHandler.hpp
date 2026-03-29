#pragma once

/**
 * @file SequencerStepHandler.hpp
 * @brief Standalone sequencer step editing bindings
 */

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

/**
 * v0 bindings (sequencer view scope):
 * - MACRO_1..MACRO_8 release: toggle step in current page
 * - NAV turn: page switch (wrap)
 * - NAV release: toggle focused step
 *
 * Bottom action buttons are handled separately by SequencerRangeActionHandler.
 */
class SequencerStepHandler {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
    };

    SequencerStepHandler(StateRefs state,
                        oc::api::EncoderAPI& encoders,
                        oc::api::ButtonAPI& buttons,
                        oc::type::ScopeID scopeId);

    ~SequencerStepHandler() = default;

    SequencerStepHandler(const SequencerStepHandler&) = delete;
    SequencerStepHandler& operator=(const SequencerStepHandler&) = delete;
    SequencerStepHandler(SequencerStepHandler&&) = delete;
    SequencerStepHandler& operator=(SequencerStepHandler&&) = delete;

private:
    void setupBindings();

    void toggleStep(uint8_t indexInPage);
    void toggleFocusedStep();
    void movePage(float delta);
    void prevPage();
    void nextPage();

    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
};

}  // namespace core::handler
