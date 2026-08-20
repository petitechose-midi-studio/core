#pragma once

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/sequencer/DrumLaneAuditionServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

class SequencerStepEditHandler;

/** Input and transaction owner for the retained Drum Lane Editor. */
class DrumLaneEditorHandler {
public:
    DrumLaneEditorHandler(
        core::state::sequencer::SequencerState& sequencer,
        SequencerHistoryDomainServices history,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID overlayScope,
        DrumLaneAuditionServices audition = {}
    );

    ~DrumLaneEditorHandler();

    bool open(bool create);
    void close();
    void update(uint32_t nowMs);
    void attachPresetLibraryHandler(SequencerStepEditHandler& handler) {
        preset_library_handler_ = &handler;
    }

private:
    void setupBindings();
    void moveField(float delta);
    void moveLane(float delta);
    void editValue(float normalized);
    void activateField();
    void apply();
    void remove();
    void configureOpt();
    void requestNoteAudition(uint8_t note, uint32_t nowMs);
    void cancelNoteAudition(uint32_t nowMs);
    void updateNoteAudition(uint32_t nowMs);
    bool stopActiveNoteAudition(uint32_t nowMs);
    bool beginHistory(core::state::sequencer::SequencerHistoryDescriptor descriptor);
    bool sealHistory(
        bool changed,
        core::state::sequencer::SequencerHistoryDescriptor descriptor
    );

    core::state::sequencer::SequencerState& sequencer_;
    SequencerHistoryDomainServices history_;
    DrumLaneAuditionServices audition_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID overlay_scope_ = 0;
    bool lane_switch_modifier_ = false;
    bool audition_active_ = false;
    bool audition_pending_ = false;
    uint8_t audition_channel_ = 0U;
    uint8_t audition_note_ = 0U;
    uint8_t audition_pending_note_ = 0U;
    uint8_t audition_off_retry_count_ = 0U;
    uint32_t audition_off_at_ms_ = 0U;
    uint32_t audition_retry_at_ms_ = 0U;
    uint32_t audition_next_on_at_ms_ = 0U;
    SequencerStepEditHandler* preset_library_handler_ = nullptr;
};

}  // namespace core::handler
