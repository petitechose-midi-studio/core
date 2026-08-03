#pragma once

#include <array>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerPatternState.hpp"
#include "state/sequencer/SequencerUiState.hpp"

namespace core::state::sequencer {

struct SequencerPreparedGraphContentPath;
struct SequencerState;

enum class SequencerQuickControlsNestedPublishOutcome : uint8_t {
    Failed,
    NoChange,
    Published,
};

/** Immutable UI location captured before a detached Quick Controls session. */
struct SequencerQuickControlsOpeningView {
    bool valid = false;
    bool previewEnabled = false;
    uint8_t stackDepth = 0;
    uint8_t page = 0;
    uint8_t focusedStep = 0;
    std::array<
        SequencerContentViewFrame,
        SequencerContentViewState::MAX_CHILD_DEPTH
    > frames{};
};

static_assert(
    sizeof(SequencerQuickControlsOpeningView) == 54U,
    "Quick Controls opening view must remain one bounded 54-byte sidecar"
);

/** One complete mutable Pattern plus the opening UI location, owned in PSRAM. */
struct SequencerQuickControlsDraft {
    SequencerPatternState pattern;
    SequencerQuickControlsOpeningView openingView{};
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#if OC_ENABLE_STATS
static_assert(
    sizeof(SequencerQuickControlsDraft) == 2048U,
    "diagnostic ARM detached Quick Controls root changed"
);
#else
static_assert(
    sizeof(SequencerQuickControlsDraft) == 2000U,
    "LOCK-P: ARM detached Quick Controls root changed"
);
#endif
#endif

/** Lazy owner for the sole detached Quick Controls Pattern. */
class SequencerQuickControlsDraftSession {
public:
    [[nodiscard]] bool begin(
        const SequencerPatternState& published,
        const SequencerPreparedGraphContentPath& openingPath,
        uint8_t openingPage,
        uint8_t openingFocusedStep
    );

    [[nodiscard]] bool active() const;
    [[nodiscard]] SequencerPatternState* pattern();
    [[nodiscard]] const SequencerPatternState* pattern() const;
    [[nodiscard]] SequencerPatternState* previewPattern();
    [[nodiscard]] const SequencerPatternState* previewPattern() const;

    void suspendPreview();
    void resumePreview();
    // Allocation-free nested publication into an existing detached Step draft.
    // The obsolete parent Graph/CC owners are retained by this session until reset.
    [[nodiscard]] SequencerQuickControlsNestedPublishOutcome
    publishToDetachedParent(SequencerPatternState& parent);
    [[nodiscard]] bool restoreOpeningView(SequencerState& sequencer) const;
    void reset();

private:
    core::app::ExtmemUniquePtr<SequencerQuickControlsDraft> draft_;
};

static_assert(
    sizeof(SequencerQuickControlsDraftSession) == sizeof(void*),
    "Quick Controls idle state must remain one lazy PSRAM handle"
);

}  // namespace core::state::sequencer
