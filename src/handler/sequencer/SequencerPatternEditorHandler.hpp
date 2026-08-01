#pragma once

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerPatternRandomizeSession.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

/** Allocation-free input owner for the retained Pattern Editor session. */
class SequencerPatternEditorHandler {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        core::state::sequencer::SequencerPatternRandomizeSession& randomize;
        SequencerHistoryDomainServices history;
    };

    SequencerPatternEditorHandler(StateRefs state,
                                  oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                  oc::api::EncoderAPI& encoders, oc::api::ButtonAPI& buttons,
                                  oc::type::ScopeID sequencerViewScope,
                                  oc::type::ScopeID overlayScope);

    SequencerPatternEditorHandler(const SequencerPatternEditorHandler&) = delete;
    SequencerPatternEditorHandler& operator=(const SequencerPatternEditorHandler&) = delete;

    /** Opens only the root Pattern and derives the first window from Page. */
    bool openFromCurrentPage();
    void close();
    /** Closes a stale session as soon as another Track owns the editor. */
    void update(uint32_t nowMs);
private:
    void setupBindings();
    bool ownsActiveTrack() const;
    void navigate(float delta);
    void setFocusedValue(float normalized);
    void beginWindowSelection();
    void endWindowSelection();
    void beginLayerSelection();
    void endLayerSelection();
    void configureOptForFocusedField();
    void openRandomize();
    void cancelRandomize();
    void rerollRandomize();
    void applyRandomize();
    void addPage();
    bool beginPendingEdit(core::state::sequencer::SequencerPatternEditorField field,
                          int32_t beforeValue, int32_t afterValue,
                          core::state::sequencer::SequencerHistoryActionKind actionKind =
                              core::state::sequencer::SequencerHistoryActionKind::PatternSettings);
    bool sealPendingEdit(bool changed);
    bool commitPendingEdit();
    void resetPendingEditMetadata();

    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    core::state::sequencer::SequencerPatternRandomizeSession& randomize_;
    SequencerHistoryDomainServices history_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID sequencer_view_scope_ = 0;
    oc::type::ScopeID overlay_scope_ = 0;
    core::state::sequencer::SequencerPatternEditorField edit_field_ =
        core::state::sequencer::SequencerPatternEditorField::LENGTH;
    core::state::sequencer::SequencerHistoryActionKind edit_action_ =
        core::state::sequencer::SequencerHistoryActionKind::PatternSettings;
    int32_t edit_before_value_ = 0;
    int32_t edit_after_value_ = 0;
    bool edit_pending_ = false;
};

}  // namespace core::handler
