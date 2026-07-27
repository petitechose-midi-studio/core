#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "app/ExtmemAllocator.hpp"
#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/StructureClipboardPastePlan.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

enum class SequencerTrackTransferStatus : uint8_t {
    DISABLED = 0,
    READY,
    APPLIED,
    NO_CHANGE,
    INCONSISTENT_STATE,
    STALE,
    ALLOCATION_UNAVAILABLE,
    HISTORY_UNAVAILABLE,
    PUBLICATION_UNAVAILABLE,
};

struct SequencerTrackTransferResult {
    SequencerTrackTransferStatus status = SequencerTrackTransferStatus::DISABLED;
    core::state::ClipboardTransferPlan plan{};
    uint32_t activationGeneration = 0;
    uint32_t operationId = 0;

    bool applied() const { return status == SequencerTrackTransferStatus::APPLIED; }
};

/**
 * Fully materialized Track transfer. Every graph and history allocation is
 * owned here before the first live state write. Commit consumes the object and
 * only performs non-failing ownership transfers and signal publication.
 */
struct PreparedSequencerTrackTransfer {
    using GraphPtr =
        core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph>;
    using CcLanePtr = core::state::sequencer::SequencerCcLaneBankPtr;

    SequencerTrackTransferStatus status = SequencerTrackTransferStatus::DISABLED;
    core::state::ClipboardTransferPlan plan{};
    uint16_t pendingTrackMask = 0;
    uint16_t initialEnabledMask = 0;
    uint16_t initialProjectMutedMask = 0;
    uint16_t initialAudibleMask = 0;
    uint16_t nextAudibleMask = 0;
    uint16_t nextEnabledMask = 0;
    uint16_t historyMask = 0;
    uint8_t previousActiveTrack = 0;
    core::state::sequencer::SequencerTrackActivationQueue* activationQueue = nullptr;
    core::state::sequencer::SequencerTrackActivationBatch activationBatch{};
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr history;
    GraphPtr bankGraph;
    CcLanePtr bankCcLanes;
    std::array<
        GraphPtr,
        core::state::ClipboardTransferPlan::MAX_ENTRIES - 1U
    > additionalBankGraphs{};
    std::array<
        CcLanePtr,
        core::state::ClipboardTransferPlan::MAX_ENTRIES - 1U
    > additionalBankCcLanes{};
    GraphPtr editorGraph;
    CcLanePtr editorCcLanes;
    GraphPtr outgoingActiveGraph;
    CcLanePtr outgoingActiveCcLanes;

    PreparedSequencerTrackTransfer() = default;
    ~PreparedSequencerTrackTransfer();
    PreparedSequencerTrackTransfer(const PreparedSequencerTrackTransfer&) = delete;
    PreparedSequencerTrackTransfer& operator=(const PreparedSequencerTrackTransfer&) = delete;
    PreparedSequencerTrackTransfer(PreparedSequencerTrackTransfer&&) noexcept = default;
    PreparedSequencerTrackTransfer& operator=(PreparedSequencerTrackTransfer&&) noexcept = default;

    bool ready() const {
        return status == SequencerTrackTransferStatus::READY && history != nullptr;
    }

    GraphPtr& bankGraphAt(uint8_t index) {
        return index == 0U
            ? bankGraph
            : additionalBankGraphs[index - 1U];
    }

    CcLanePtr& bankCcLanesAt(uint8_t index) {
        return index == 0U
            ? bankCcLanes
            : additionalBankCcLanes[index - 1U];
    }
};

PreparedSequencerTrackTransfer prepareSequencerTrackTransfer(
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::project::ProjectTrackState& projectTracks,
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    const SharedTrackDomainServices& sharedTracks,
    const SequencerHistoryDomainServices& history,
    uint8_t targetTrack,
    uint16_t pendingTrackMask = 0,
    core::state::sequencer::SequencerTrackActivationQueue* activationQueue = nullptr,
    bool transportPlaying = false,
    core::state::macro::MacroPagesState* macroPages = nullptr
);

SequencerTrackTransferResult commitPreparedSequencerTrackTransfer(
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::project::ProjectTrackState& projectTracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    const SharedTrackDomainServices& sharedTracks,
    const SequencerHistoryDomainServices& history,
    PreparedSequencerTrackTransfer prepared,
    core::state::macro::MacroPagesState* macroPages = nullptr
);

SequencerTrackTransferResult executeSequencerTrackTransfer(
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::project::ProjectTrackState& projectTracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& clipboard,
    const SharedTrackDomainServices& sharedTracks,
    const SequencerHistoryDomainServices& history,
    uint8_t targetTrack,
    uint16_t pendingTrackMask = 0,
    core::state::sequencer::SequencerTrackActivationQueue* activationQueue = nullptr,
    bool transportPlaying = false,
    core::state::macro::MacroPagesState* macroPages = nullptr
);

}  // namespace core::handler
