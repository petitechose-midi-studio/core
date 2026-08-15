#include "handler/sequencer/SequencerChordPresetLibraryAdapter.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::handler {
namespace {

namespace contextual = core::state::contextual;
namespace sequencer = core::state::sequencer;

FLASHMEM contextual::ContextActionReason reasonForResult(
    const SequencerChordPresetActionResult& result
) {
    switch (result.status) {
        case SequencerChordPresetStatus::CORRUPT:
            return contextual::ContextActionReason::CORRUPT_ASSET;
        case SequencerChordPresetStatus::STALE_TARGET:
            return contextual::ContextActionReason::STALE_TARGET;
        case SequencerChordPresetStatus::COLLISION:
            return contextual::ContextActionReason::CONFLICT;
        case SequencerChordPresetStatus::STORAGE_UNAVAILABLE:
            return contextual::ContextActionReason::STORAGE_UNAVAILABLE;
        case SequencerChordPresetStatus::INCOMPATIBLE:
            return contextual::ContextActionReason::INCOMPATIBLE;
        case SequencerChordPresetStatus::EMPTY:
            return contextual::ContextActionReason::EMPTY_SELECTION;
        case SequencerChordPresetStatus::QUEUED:
            return contextual::ContextActionReason::PENDING;
        default:
            return contextual::ContextActionReason::FAILED;
    }
}

FLASHMEM sequencer::SequencerPresetLibraryFeedback feedbackForResult(
    const SequencerChordPresetActionResult& result
) {
    using Feedback = sequencer::SequencerPresetLibraryFeedback;
    if (result.status == SequencerChordPresetStatus::EMPTY) {
        return Feedback::EMPTY;
    }
    if (result.status == SequencerChordPresetStatus::QUEUED) {
        return Feedback::QUEUED;
    }
    if (result.status == SequencerChordPresetStatus::INCOMPATIBLE ||
        result.status == SequencerChordPresetStatus::CORRUPT ||
        result.status == SequencerChordPresetStatus::STALE_TARGET) {
        return Feedback::INCOMPATIBLE;
    }
    return result.ok() ? Feedback::NONE : Feedback::FAILED;
}

FLASHMEM void copyAssetId(
    SequencerPresetLibraryResult& result,
    const char* assetId
) {
    std::strncpy(
        result.assetId,
        assetId != nullptr ? assetId : "",
        sizeof(result.assetId) - 1U
    );
}

}  // namespace

FLASHMEM SequencerChordPresetLibraryAdapter::
SequencerChordPresetLibraryAdapter(
    sequencer::SequencerState& sequencer,
    SequencerChordPresetDomainServices& chordPresets
) : sequencer_(sequencer),
    chord_presets_(chordPresets) {}

FLASHMEM SequencerPresetLibraryAdapter
SequencerChordPresetLibraryAdapter::operations() {
    return SequencerPresetLibraryAdapterBinding<
        SequencerChordPresetLibraryAdapter>::bind(
            *this,
            sequencer::SequencerPresetLibraryKind::CHORD
        );
}

FLASHMEM bool
SequencerChordPresetLibraryAdapter::beginSession() {
    const auto target = chord_presets_.captureTarget();
    if (!target.valid) return false;
    auto& picker = sequencer_.presetLibrary;
    auto& chord = picker.chord();
    chord.target = target;
    chord.descriptor = {};
    return true;
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerChordPresetLibraryAdapter::loadPage(
    SequencerPresetLibraryPager::Entry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    SequencerPresetLibraryPager::PageDirection direction,
    core::persistence::ProductAssetFileListResult& out
) {
    const auto listed = chord_presets_.listPresetsPage(
        entries,
        capacity,
        anchorExclusive,
        direction
    );
    if (listed.status == SequencerChordPresetStatus::QUEUED) {
        return SequencerPresetLibraryPager::PageLoadStatus::PENDING;
    }
    if (!listed.ok()) {
        return SequencerPresetLibraryPager::PageLoadStatus::FAILED;
    }
    out = {
        .count = listed.count,
        .truncated = listed.truncated,
        .hasPrevious = listed.hasPrevious,
        .hasNext = listed.hasNext,
        .totalCount = listed.totalCount,
    };
    return SequencerPresetLibraryPager::PageLoadStatus::READY;
}

FLASHMEM void
SequencerChordPresetLibraryAdapter::clearInspection() {
    sequencer_.presetLibrary.chord().descriptor = {};
}

FLASHMEM sequencer::SequencerPresetLibraryFeedback
SequencerChordPresetLibraryAdapter::inspect(
    const char* assetId,
    bool force
) {
    auto& picker = sequencer_.presetLibrary;
    auto& chord = picker.chord();
    if (assetId == nullptr || assetId[0] == '\0') {
        clearInspection();
        picker.inspecting.set(false);
        picker.bump();
        return sequencer::SequencerPresetLibraryFeedback::EMPTY;
    }

    const uint32_t wantedTargetHash =
        sequencer::sequencerChordPresetTargetHash(
            chord.target
        );
    if (!force && chord.descriptor.valid &&
        std::strcmp(
            chord.descriptor.technicalId,
            assetId
        ) == 0 &&
        chord.descriptor.previewKey.targetHash ==
            wantedTargetHash) {
        return picker.feedback.get();
    }

    uint32_t generation =
        picker.previewGeneration.get() + 1U;
    if (generation == 0U) generation = 1U;
    picker.previewGeneration.set(generation);
    picker.inspecting.set(true);
    picker.bump();

    const auto inspected = chord_presets_.inspectPreset(
        assetId,
        chord.target,
        generation
    );
    if (picker.previewGeneration.get() != generation ||
        inspected.descriptor.generation != generation ||
        inspected.descriptor.previewKey.targetHash !=
            wantedTargetHash) {
        return picker.feedback.get();
    }

    chord.descriptor = inspected.descriptor;
    picker.inspecting.set(false);
    picker.bump();
    if (sequencer::sequencerChordPresetCanApply(
            inspected.descriptor.compatibility
        )) {
        return sequencer::SequencerPresetLibraryFeedback::NONE;
    }
    if (inspected.descriptor.compatibility ==
        sequencer::SequencerChordPresetCompatibility::
            STORAGE_UNAVAILABLE) {
        return sequencer::SequencerPresetLibraryFeedback::FAILED;
    }
    return sequencer::SequencerPresetLibraryFeedback::INCOMPATIBLE;
}

FLASHMEM uint8_t
SequencerChordPresetLibraryAdapter::detailRowCount() const {
    // Formula, Transform and Output are inspectable semantic groups. They are
    // intentionally read-only, but exposing their focus keeps Detail
    // navigation visually honest and consistent with the common grammar.
    return 3U;
}

FLASHMEM void
SequencerChordPresetLibraryAdapter::adjustFocusedDetail(
    const char*,
    float
) {}

FLASHMEM contextual::ContextActionSpec
SequencerChordPresetLibraryAdapter::actionSpec(
    bool saveMode,
    bool selectedNewAsset,
    bool hasFocusedAsset
) const {
    const auto& picker = sequencer_.presetLibrary;
    const auto& chord = picker.chord();
    return sequencer::buildSequencerChordPresetActionSpec(
        saveMode,
        selectedNewAsset,
        hasFocusedAsset,
        chord.target,
        chord.descriptor
    );
}

FLASHMEM bool
SequencerChordPresetLibraryAdapter::shouldCommitBeforeLoad(bool) const {
    return false;
}

FLASHMEM SequencerPresetLibraryResult
SequencerChordPresetLibraryAdapter::execute(
    Mode mode,
    const char* assetId,
    bool createNew,
    bool overwriteAuthorized
) {
    auto& picker = sequencer_.presetLibrary;
    auto& chord = picker.chord();
    if (mode == Mode::SAVE) {
        char resolvedId[PickerState::ID_SIZE]{};
        SequencerChordPresetActionResult action{};
        if (createNew) {
            action = chord_presets_.nextPresetId(
                resolvedId,
                sizeof(resolvedId)
            );
            if (action.status == SequencerChordPresetStatus::QUEUED) {
                return {
                    .outcome = SequencerPresetLibraryOutcome::RETRY_PENDING,
                    .feedback = feedbackForResult(action),
                    .reason = reasonForResult(action),
                };
            }
            if (!action.ok()) {
                return {
                    .outcome =
                        SequencerPresetLibraryOutcome::BLOCKED,
                    .feedback = feedbackForResult(action),
                    .reason = reasonForResult(action),
                };
            }
        } else {
            std::strncpy(
                resolvedId,
                assetId != nullptr ? assetId : "",
                sizeof(resolvedId) - 1U
            );
        }

        action = chord_presets_.savePreset(
            resolvedId,
            chord.target,
            overwriteAuthorized
        );
        if (action.status == SequencerChordPresetStatus::QUEUED) {
            return {
                .outcome = SequencerPresetLibraryOutcome::RETRY_PENDING,
                .feedback = feedbackForResult(action),
                .reason = reasonForResult(action),
            };
        }
        if (!action.ok()) {
            return {
                .outcome =
                    SequencerPresetLibraryOutcome::BLOCKED,
                .feedback = feedbackForResult(action),
                .reason = reasonForResult(action),
            };
        }
        SequencerPresetLibraryResult result{};
        result.outcome =
            SequencerPresetLibraryOutcome::SAVED;
        result.feedback =
            sequencer::SequencerPresetLibraryFeedback::SAVED;
        copyAssetId(result, action.presetId);
        return result;
    }

    const auto action = chord_presets_.applyPreset(
        assetId,
        chord.target,
        chord.descriptor.previewKey
    );
    if (action.status == SequencerChordPresetStatus::QUEUED) {
        return {
            .outcome = SequencerPresetLibraryOutcome::RETRY_PENDING,
            .feedback = feedbackForResult(action),
            .reason = reasonForResult(action),
        };
    }
    if (!action.ok()) {
        return {
            .outcome =
                SequencerPresetLibraryOutcome::LOAD_FAILED,
            .feedback = feedbackForResult(action),
            .reason = reasonForResult(action),
        };
    }
    chord.target = chord_presets_.captureTarget();
    return {
        .outcome = SequencerPresetLibraryOutcome::LOADED,
        .feedback =
            sequencer::SequencerPresetLibraryFeedback::LOADED,
        .reason = contextual::ContextActionReason::NONE,
    };
}

FLASHMEM SequencerPresetLibraryResult
SequencerChordPresetLibraryAdapter::update(uint32_t) {
    return {};
}

}  // namespace core::handler
