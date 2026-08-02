#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerPatternState.hpp"

namespace core::state::sequencer {

template <typename T>
struct SequencerStepContentDraftValue {
    T value{};

    constexpr SequencerStepContentDraftValue() = default;
    constexpr explicit SequencerStepContentDraftValue(T initial) : value(initial) {}
    [[nodiscard]] constexpr T get() const { return value; }
    constexpr void set(T next) { value = next; }
};

enum class SequencerStepContentDraftKind : uint8_t {
    NONE = 0,
    CHORD,
    MICRO_SEQUENCE,
    CYCLE_STATES,
};

enum class SequencerStepContentDraftExitChoice : uint8_t {
    CONTINUE = 0,
    DISCARD,
    SAVE,
    COUNT,
};

enum class SequencerStepContentDraftFailure : uint8_t {
    NONE = 0,
    OUT_OF_MEMORY,
    HISTORY_UNAVAILABLE,
    PUBLISH_FAILED,
    TRANSITION_BLOCKED,
    UNPUBLISHABLE_MUTATION,
};

enum class SequencerStepContentDraftBlockedTransition : uint8_t {
    NONE = 0,
    TRACK,
    VIEW,
    PROJECT_LOAD,
    RESET,
    STRUCTURE_EDIT,
    HISTORY,
};

/** Hot, bounded authoring payload for a new Chord. */
struct SequencerStepChordDraftState {
    static constexpr uint16_t INVALID_NODE = 0xFFFFU;

    uint16_t ownerNodeId = INVALID_NODE;
    bool modePresent = false;
    bool localPresent = false;
    oc::note::sequencer::StepSequencerChordMode mode =
        oc::note::sequencer::StepSequencerChordMode::Single;
    oc::note::sequencer::StepSequencerChordSpec spec{};

    bool pristineModePresent = false;
    bool pristineLocalPresent = false;
    oc::note::sequencer::StepSequencerChordMode pristineMode =
        oc::note::sequencer::StepSequencerChordMode::Single;
    oc::note::sequencer::StepSequencerChordSpec pristineSpec{};

    void reset();
    void markPristine();
    [[nodiscard]] bool modified() const;
};

static_assert(
    sizeof(SequencerStepChordDraftState) <= 28,
    "Eight-voice Chord authoring must remain a small local POD"
);

/** Data owned by a Step-content authoring session in hot RAM. */
struct SequencerStepContentDraftOwnedState {
    SequencerStepContentDraftValue<bool> active{false};
    SequencerStepContentDraftValue<SequencerStepContentDraftKind> kind{
        SequencerStepContentDraftKind::NONE
    };
    SequencerStepContentDraftValue<bool> exitPromptVisible{false};
    SequencerStepContentDraftValue<SequencerStepContentDraftExitChoice> exitChoice{
        SequencerStepContentDraftExitChoice::SAVE
    };
    SequencerStepContentDraftValue<uint32_t> revision{0};

    core::app::ExtmemUniquePtr<SequencerPatternState> scratch;
    uint32_t pristineGraphRevision = 0;
    uint32_t pristineGraphFingerprint = 0;
    uint8_t ownerStep = 0;
    SequencerStepChordDraftState chord{};
    SequencerStepContentDraftFailure failure =
        SequencerStepContentDraftFailure::NONE;
    SequencerStepContentDraftBlockedTransition blockedTransition =
        SequencerStepContentDraftBlockedTransition::NONE;
};

static_assert(
    sizeof(SequencerStepContentDraftOwnedState) <= 56,
    "Step draft owned hot state must remain one PSRAM handle, one Chord POD, "
    "and bounded scalar metadata"
);

/**
 * One bounded, reusable PSRAM authoring scratch for new Step content.
 *
 * Published Pattern data remains the runtime/persistence authority until
 * commit. The scratch allocation is retained between sessions so repeated
 * creation gestures do not churn PSRAM. Only one draft may exist at a time.
 *
 * Reactive callback storage is not owned here. Draft invalidation reuses the
 * enclosing SequencerContentViewState revision signal through one non-owning
 * pointer, because every draft consumer already observes that view revision.
 */
struct SequencerStepContentDraftSession
    : SequencerStepContentDraftOwnedState {
    using RevisionSignal = oc::state::Signal<uint32_t, 8>;

    void bindRevisionSignal(RevisionSignal& signal);

    [[nodiscard]] bool begin(
        const SequencerPatternState& published,
        SequencerStepContentDraftKind nextKind,
        uint8_t nextOwnerStep,
        uint16_t nextOwnerNodeId = SequencerStepChordDraftState::INVALID_NODE
    );

    [[nodiscard]] bool modified() const;
    [[nodiscard]] SequencerPatternState* pattern();
    [[nodiscard]] const SequencerPatternState* pattern() const;

    void markPristine();
    void touch();
    void showExitPrompt();
    void hideExitPrompt();
    void clearFailure();
    void noteFailure(SequencerStepContentDraftFailure nextFailure);
    void noteBlockedTransition(
        SequencerStepContentDraftBlockedTransition transition
    );
    [[nodiscard]] bool rejectTransitionIfActive(
        SequencerStepContentDraftBlockedTransition transition
    );
    void resetSession();

private:
    RevisionSignal* revisionSignal_ = nullptr;
};

static_assert(
    sizeof(SequencerStepContentDraftSession) <=
        sizeof(SequencerStepContentDraftOwnedState) + sizeof(void*),
    "Step draft session may add only one non-owning revision-signal pointer"
);

}  // namespace core::state::sequencer
