#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/DrumPatternState.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

struct SequencerState;

struct SequencerClipAddress {
    uint8_t track = 0U;
    uint8_t slot = 0U;
};

enum class SequencerLauncherSlotKind : uint8_t {
    EMPTY = 0,
    CLIP,
    STOP,
};

enum class SequencerLauncherFollowQuantization : uint8_t {
    GLOBAL = 0,
    BEAT,
    BAR,
};

/** One-byte authored Follow choice; explicit slot values keep their wire IDs. */
enum class SequencerLauncherFollowChoice : uint8_t {
    TARGET_1 = 0U,
    TARGET_2,
    TARGET_3,
    TARGET_4,
    TARGET_5,
    TARGET_6,
    TARGET_7,
    TARGET_8,
    RANDOM_ANY = 0xFBU,
    RANDOM_OTHER = 0xFCU,
    FIRST = 0xFDU,
    NEXT = 0xFEU,
    NONE = 0xFFU,
};

constexpr bool sequencerLauncherFollowChoiceIsTarget(
    SequencerLauncherFollowChoice choice
) noexcept {
    return static_cast<uint8_t>(choice) < 8U;
}

constexpr SequencerLauncherFollowChoice sequencerLauncherFollowTarget(
    uint8_t slot
) noexcept {
    return slot < 8U
        ? static_cast<SequencerLauncherFollowChoice>(slot)
        : SequencerLauncherFollowChoice::NONE;
}

constexpr uint8_t sequencerLauncherFollowTargetSlot(
    SequencerLauncherFollowChoice choice
) noexcept {
    return sequencerLauncherFollowChoiceIsTarget(choice)
        ? static_cast<uint8_t>(choice)
        : 0xFFU;
}

[[nodiscard]] SequencerLauncherFollowChoice stepSequencerLauncherFollowChoice(
    SequencerLauncherFollowChoice choice,
    int direction
) noexcept;

/** Compact authored follow action shared by Clips and Scenes. */
struct SequencerLauncherBehavior {
    static constexpr uint8_t MAX_LENGTH = 16U;

    uint8_t length = 0U;
    SequencerLauncherFollowChoice follow =
        SequencerLauncherFollowChoice::NONE;
    SequencerLauncherFollowQuantization quantization =
        SequencerLauncherFollowQuantization::GLOBAL;

    [[nodiscard]] bool enabled() const noexcept {
        return length > 0U && follow != SequencerLauncherFollowChoice::NONE;
    }
};

constexpr bool operator==(
    const SequencerLauncherBehavior& lhs,
    const SequencerLauncherBehavior& rhs
) noexcept {
    return lhs.length == rhs.length && lhs.follow == rhs.follow &&
        lhs.quantization == rhs.quantization;
}

enum class SequencerClipStructureAction : uint8_t {
    CREATE = 0,
    DELETE,
    MOVE,
    DUPLICATE_CLIP,
};

constexpr bool operator==(
    SequencerClipAddress lhs,
    SequencerClipAddress rhs
) noexcept {
    return lhs.track == rhs.track && lhs.slot == rhs.slot;
}

/** Plain, non-reactive Pattern document retained only for an inactive Clip. */
struct SequencerClipDocument {
    SequencerPatternSnapshot pattern{};
    SequencerClipSnapshot clip{};
    uint32_t ccLaneRevision = 0U;
    SequencerTrackKind trackKind = SequencerTrackKind::INSTRUMENT;
    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph;
    SequencerCcLaneBankPtr ccLanes;
    core::app::ExtmemUniquePtr<DrumTrackState> drum;

    SequencerClipDocument();
    ~SequencerClipDocument();
    SequencerClipDocument(const SequencerClipDocument&) = delete;
    SequencerClipDocument& operator=(const SequencerClipDocument&) = delete;
    SequencerClipDocument(SequencerClipDocument&&) noexcept;
    SequencerClipDocument& operator=(SequencerClipDocument&&) noexcept;
};

using SequencerClipDocumentPtr =
    core::app::ExtmemUniquePtr<SequencerClipDocument>;

/** Strong-guarantee clone of one complete Clip document into PSRAM. */
[[nodiscard]] bool captureSequencerClipDocument(
    const SequencerPatternState& pattern,
    const SequencerClipState& clip,
    SequencerTrackKind trackKind,
    const DrumTrackState* drum,
    SequencerClipDocumentPtr& out
);

/** Creates a blank Clip while preserving the destination Track's Drum kit. */
[[nodiscard]] bool createEmptySequencerClipDocument(
    SequencerTrackKind trackKind,
    const DrumTrackState* drumTemplate,
    SequencerClipDocumentPtr& out
);

[[nodiscard]] bool cloneSequencerClipDocument(
    const SequencerClipDocument& source,
    SequencerClipDocumentPtr& out
);

[[nodiscard]] bool sameSequencerClipDocument(
    const SequencerClipDocument& lhs,
    const SequencerClipDocument& rhs
) noexcept;

[[nodiscard]] bool validSequencerClipDocument(
    const SequencerClipDocument& document,
    SequencerTrackKind expectedKind
) noexcept;

struct SequencerClipGridSnapshot {
    static constexpr uint8_t INVALID_SLOT = 0xFFU;
    static constexpr uint8_t TRACK_COUNT = SequencerTrackBankState::TRACK_COUNT;
    static constexpr uint8_t SLOT_COUNT = 8U;
    static constexpr uint16_t CELL_COUNT = TRACK_COUNT * SLOT_COUNT;

    std::array<uint8_t, TRACK_COUNT> residentSlots{};
    std::array<uint32_t, CELL_COUNT> generations{};
    std::array<uint16_t, SLOT_COUNT> stopMasks{};
    std::array<SequencerLauncherBehavior, CELL_COUNT> clipBehaviors{};
    std::array<SequencerLauncherBehavior, SLOT_COUNT> sceneBehaviors{};
    std::array<SequencerClipDocumentPtr, CELL_COUNT> documents{};

    SequencerClipGridSnapshot();
    ~SequencerClipGridSnapshot();
    SequencerClipGridSnapshot(const SequencerClipGridSnapshot&) = delete;
    SequencerClipGridSnapshot& operator=(const SequencerClipGridSnapshot&) = delete;
    SequencerClipGridSnapshot(SequencerClipGridSnapshot&&) noexcept;
    SequencerClipGridSnapshot& operator=(SequencerClipGridSnapshot&&) noexcept;
    void reset();
};

/**
 * Sparse launcher ownership.
 *
 * One Clip per enabled Track is resident in the existing reactive Track bank.
 * Only the other occupied slots own a plain PSRAM document here. Eight rows
 * are addressable while at most sixteen inactive documents may coexist; this
 * keeps the 4 x 2 viewport independent from retained-memory capacity.
 */
class SequencerClipGridState {
public:
    static constexpr uint8_t INVALID_SLOT = SequencerClipGridSnapshot::INVALID_SLOT;
    static constexpr uint8_t TRACK_COUNT = SequencerClipGridSnapshot::TRACK_COUNT;
    static constexpr uint8_t SLOT_COUNT = SequencerClipGridSnapshot::SLOT_COUNT;
    static constexpr uint16_t CELL_COUNT = SequencerClipGridSnapshot::CELL_COUNT;
    static constexpr uint8_t MAX_INACTIVE_DOCUMENTS = 16U;
    static constexpr uint32_t MAX_INACTIVE_RETAINED_BYTES = 256U * 1024U;

    SequencerClipGridState();
    ~SequencerClipGridState();
    SequencerClipGridState(const SequencerClipGridState&) = delete;
    SequencerClipGridState& operator=(const SequencerClipGridState&) = delete;

    [[nodiscard]] static constexpr bool validAddress(
        SequencerClipAddress address
    ) noexcept {
        return address.track < TRACK_COUNT && address.slot < SLOT_COUNT;
    }

    [[nodiscard]] static constexpr uint16_t cellIndex(
        SequencerClipAddress address
    ) noexcept {
        return static_cast<uint16_t>(address.track) * SLOT_COUNT + address.slot;
    }

    void reset(uint16_t enabledTrackMask = 0x0001U);
    void synchronizeEnabledTracks(uint16_t enabledTrackMask);

    [[nodiscard]] uint8_t residentSlot(uint8_t track) const noexcept;
    [[nodiscard]] bool isOccupied(SequencerClipAddress address) const noexcept;
    [[nodiscard]] bool isResident(SequencerClipAddress address) const noexcept;
    [[nodiscard]] SequencerLauncherSlotKind slotKind(
        SequencerClipAddress address
    ) const noexcept;
    [[nodiscard]] bool isStop(SequencerClipAddress address) const noexcept;
    [[nodiscard]] bool setStop(SequencerClipAddress address) noexcept;
    [[nodiscard]] bool clearStop(SequencerClipAddress address) noexcept;
    [[nodiscard]] SequencerLauncherBehavior clipBehavior(
        SequencerClipAddress address
    ) const noexcept;
    [[nodiscard]] SequencerLauncherBehavior sceneBehavior(
        uint8_t slot
    ) const noexcept;
    [[nodiscard]] bool setClipBehavior(
        SequencerClipAddress address,
        SequencerLauncherBehavior behavior
    ) noexcept;
    [[nodiscard]] bool setSceneBehavior(
        uint8_t slot,
        SequencerLauncherBehavior behavior
    ) noexcept;
    [[nodiscard]] bool sceneUsed(uint8_t slot) const noexcept;
    [[nodiscard]] uint8_t lastNavigableScene() const noexcept;
    [[nodiscard]] uint32_t generation(SequencerClipAddress address) const noexcept;
    [[nodiscard]] uint8_t inactiveDocumentCount() const noexcept {
        return inactive_document_count_;
    }
    [[nodiscard]] uint32_t inactiveRetainedBytes() const noexcept {
        return inactive_retained_bytes_;
    }
    [[nodiscard]] uint8_t occupiedCount() const noexcept;

    [[nodiscard]] const SequencerClipDocument* inactiveDocument(
        SequencerClipAddress address
    ) const noexcept;
    [[nodiscard]] SequencerClipDocument* inactiveDocument(
        SequencerClipAddress address
    ) noexcept;

    /** Invalidates prepared runtime content after an in-place document edit. */
    [[nodiscard]] bool markInactiveDocumentMutated(
        SequencerClipAddress address
    ) noexcept;

    /** Installs a newly occupied, non-resident slot. Does not consume on failure. */
    [[nodiscard]] bool installInactiveDocument(
        SequencerClipAddress address,
        SequencerClipDocumentPtr&& document
    );

    /** Removes one non-resident Clip and returns its ownership. */
    [[nodiscard]] SequencerClipDocumentPtr removeInactiveDocument(
        SequencerClipAddress address
    );

    /** Moves one Clip to an empty slot on the same Track without allocation. */
    [[nodiscard]] bool moveClip(
        SequencerClipAddress source,
        SequencerClipAddress destination
    ) noexcept;

    /**
     * Exchanges storage roles after a fully prepared resident switch.
     *
     * The returned target document is consumed by the reactive Track/editor;
     * outgoing remains at the old slot. No allocation can occur here.
     */
    [[nodiscard]] SequencerClipDocumentPtr exchangeResidentDocument(
        uint8_t track,
        uint8_t targetSlot,
        SequencerClipDocumentPtr outgoing
    ) noexcept;

    [[nodiscard]] uint32_t revision() const noexcept {
        return revision_.get();
    }
    oc::state::Signal<uint32_t, 8>& revisionSignal() noexcept {
        return revision_;
    }
    const oc::state::Signal<uint32_t, 8>& revisionSignal() const noexcept {
        return revision_;
    }

private:
    struct Cell {
        uint32_t generation = 1U;
        SequencerClipDocumentPtr document;
    };

    static uint32_t nextGeneration(uint32_t current) noexcept;
    void publishMutation() noexcept;
    void clearTrack(uint8_t track) noexcept;

    std::array<uint8_t, TRACK_COUNT> resident_slots_{};
    std::array<Cell, CELL_COUNT> cells_{};
    std::array<uint16_t, SLOT_COUNT> stop_masks_{};
    std::array<SequencerLauncherBehavior, CELL_COUNT> clip_behaviors_{};
    std::array<SequencerLauncherBehavior, SLOT_COUNT> scene_behaviors_{};
    uint32_t inactive_retained_bytes_ = 0U;
    uint8_t inactive_document_count_ = 0U;
    oc::state::Signal<uint32_t, 8> revision_{1U};

    friend bool captureSequencerClipGridSnapshot(
        const SequencerClipGridState&,
        SequencerClipGridSnapshot&
    );
    friend bool applySequencerClipGridSnapshot(
        SequencerClipGridState&,
        const SequencerClipGridSnapshot&
    );
    friend bool restoreSequencerClipGridSnapshot(
        SequencerClipGridState&,
        SequencerClipGridSnapshot&&
    ) noexcept;
    friend void extractSequencerClipGridSnapshot(
        SequencerClipGridState&,
        SequencerClipGridSnapshot&
    ) noexcept;
};

/**
 * Shared capability contract for Clip move/duplicate placement.
 *
 * Cross-Track transfer is intentionally kind-preserving. A resident Clip may
 * move inside its Track, but cannot leave it while every enabled Track must
 * retain one live editor/runtime owner.
 */
[[nodiscard]] bool canTransferSequencerClip(
    const SequencerClipGridState& grid,
    const SequencerTrackBankState& bank,
    SequencerClipAddress source,
    SequencerClipAddress destination,
    SequencerClipStructureAction action
) noexcept;

[[nodiscard]] uint16_t compatibleSequencerClipTrackMask(
    const SequencerClipGridState& grid,
    const SequencerTrackBankState& bank,
    SequencerClipAddress source
) noexcept;

[[nodiscard]] bool firstSequencerClipTransferDestination(
    const SequencerClipGridState& grid,
    const SequencerTrackBankState& bank,
    SequencerClipAddress source,
    SequencerClipStructureAction action,
    SequencerClipAddress& out
) noexcept;

struct SequencerClipStructureChange {
    SequencerClipStructureAction action = SequencerClipStructureAction::CREATE;
    SequencerClipAddress source{};
    SequencerClipAddress destination{};
    uint32_t retainedBytes = 0U;
    uint16_t retainedSpans = 0U;
    bool afterApplied = false;
    SequencerLauncherBehavior behavior{};
    SequencerClipDocumentPtr document;
};

using SequencerClipStructureChangePtr =
    core::app::ExtmemUniquePtr<SequencerClipStructureChange>;

[[nodiscard]] SequencerClipStructureChangePtr
prepareSequencerClipInstallChange(
    SequencerClipStructureAction action,
    SequencerClipAddress destination,
    SequencerClipDocumentPtr document,
    SequencerLauncherBehavior behavior = {}
);

[[nodiscard]] SequencerClipStructureChangePtr
prepareSequencerClipDeleteChange(
    const SequencerClipGridState& grid,
    SequencerClipAddress source
);

[[nodiscard]] SequencerClipStructureChangePtr
prepareSequencerClipMoveChange(
    const SequencerClipGridState& grid,
    SequencerClipAddress source,
    SequencerClipAddress destination
);

[[nodiscard]] uint32_t sequencerClipDocumentRetainedBytes(
    const SequencerClipDocument& document
) noexcept;
[[nodiscard]] uint16_t sequencerClipDocumentRetainedSpans(
    const SequencerClipDocument& document
) noexcept;

/** Allocation-free structural history replay. */
[[nodiscard]] bool applySequencerClipStructureChange(
    SequencerClipGridState& grid,
    SequencerClipStructureChange& change,
    bool after
) noexcept;

[[nodiscard]] bool captureSequencerClipGridSnapshot(
    const SequencerClipGridState& source,
    SequencerClipGridSnapshot& out
);

/** Moves a scratch grid into snapshot ownership without cloning payloads. */
void extractSequencerClipGridSnapshot(
    SequencerClipGridState& source,
    SequencerClipGridSnapshot& out
) noexcept;

[[nodiscard]] bool cloneSequencerClipGridSnapshot(
    const SequencerClipGridSnapshot& source,
    SequencerClipGridSnapshot& out
);

[[nodiscard]] bool validSequencerClipGridSnapshot(
    const SequencerClipGridSnapshot& snapshot,
    uint16_t enabledTrackMask,
    uint16_t drumTrackMask
) noexcept;

/** Validates, then transfers a prepared snapshot without allocating. */
[[nodiscard]] bool restoreSequencerClipGridSnapshot(
    SequencerClipGridState& target,
    SequencerClipGridSnapshot&& snapshot
) noexcept;

/** Clones first, then atomically replaces the complete sparse grid. */
[[nodiscard]] bool applySequencerClipGridSnapshot(
    SequencerClipGridState& target,
    const SequencerClipGridSnapshot& snapshot
);

/**
 * Cold authoring transition into another occupied Clip on one Track.
 * All allocations finish before the first live mutation.
 */
[[nodiscard]] bool switchResidentSequencerClip(
    SequencerClipGridState& grid,
    SequencerTrackBankState& bank,
    SequencerState& active,
    SequencerClipAddress target
);

}  // namespace core::state::sequencer
