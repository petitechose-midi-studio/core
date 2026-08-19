#include "handler/sequencer/SequencerPatternPresetLibraryAdapter.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/PatternPresetFactoryLibrary.hpp"

namespace core::handler {
namespace {

namespace contextual = core::state::contextual;
namespace sequencer = core::state::sequencer;

FLASHMEM contextual::ContextActionReason reasonForResult(
    const SequencerPatternPresetActionResult& result
) {
    switch (result.status) {
        case SequencerPatternPresetDomainStatus::CORRUPT:
            return contextual::ContextActionReason::CORRUPT_ASSET;
        case SequencerPatternPresetDomainStatus::UNSUPPORTED_VERSION:
            return contextual::ContextActionReason::UNSUPPORTED_VERSION;
        case SequencerPatternPresetDomainStatus::STALE_TARGET:
            return contextual::ContextActionReason::STALE_TARGET;
        case SequencerPatternPresetDomainStatus::COLLISION:
            return contextual::ContextActionReason::CONFLICT;
        case SequencerPatternPresetDomainStatus::READ_ONLY:
            return contextual::ContextActionReason::READ_ONLY;
        case SequencerPatternPresetDomainStatus::STORAGE_UNAVAILABLE:
            return contextual::ContextActionReason::STORAGE_UNAVAILABLE;
        case SequencerPatternPresetDomainStatus::ALLOCATION_UNAVAILABLE:
            return contextual::ContextActionReason::ALLOCATION_UNAVAILABLE;
        case SequencerPatternPresetDomainStatus::HISTORY_UNAVAILABLE:
            return contextual::ContextActionReason::HISTORY_UNAVAILABLE;
        case SequencerPatternPresetDomainStatus::INCOMPATIBLE:
            return contextual::ContextActionReason::INCOMPATIBLE;
        case SequencerPatternPresetDomainStatus::EMPTY:
            return contextual::ContextActionReason::EMPTY_SELECTION;
        case SequencerPatternPresetDomainStatus::QUEUED:
            return contextual::ContextActionReason::PENDING;
        default:
            return contextual::ContextActionReason::FAILED;
    }
}

FLASHMEM sequencer::SequencerPresetLibraryFeedback feedbackForResult(
    const SequencerPatternPresetActionResult& result
) {
    using Feedback = sequencer::SequencerPresetLibraryFeedback;
    if (result.status == SequencerPatternPresetDomainStatus::EMPTY) {
        return Feedback::EMPTY;
    }
    if (result.status == SequencerPatternPresetDomainStatus::QUEUED) {
        return Feedback::QUEUED;
    }
    if (result.status == SequencerPatternPresetDomainStatus::INCOMPATIBLE ||
        result.status == SequencerPatternPresetDomainStatus::CORRUPT ||
        result.status ==
            SequencerPatternPresetDomainStatus::UNSUPPORTED_VERSION ||
        result.status == SequencerPatternPresetDomainStatus::STALE_TARGET) {
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

FLASHMEM SequencerPatternPresetLibraryAdapter::
SequencerPatternPresetLibraryAdapter(
    sequencer::SequencerState& sequencer,
    SequencerPatternPresetDomainServices& patternPresets
) : sequencer_(sequencer),
    pattern_presets_(patternPresets) {}

FLASHMEM SequencerPresetLibraryAdapter
SequencerPatternPresetLibraryAdapter::operations() {
    return SequencerPresetLibraryAdapterBinding<
        SequencerPatternPresetLibraryAdapter>::bind(
            *this,
            sequencer::SequencerPresetLibraryKind::PATTERN
        );
}

FLASHMEM bool SequencerPatternPresetLibraryAdapter::beginSession() {
    if (!sequencer_.patternEditor.active.get()) return false;
    auto& pattern = sequencer_.presetLibrary.pattern();
    pattern.target = pattern_presets_.captureTarget();
    pattern.descriptor = {};
    pattern.sourceFilter =
        sequencer::SequencerPatternPresetSourceFilter::ALL;
    pattern.activationGeneration = 0U;
    return pattern.target.valid;
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPatternPresetLibraryAdapter::loadPage(
    SequencerPresetLibraryPager::Entry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    SequencerPresetLibraryPager::PageDirection direction,
    core::persistence::ProductAssetFileListResult& out
) {
    auto listed = pattern_presets_.listPresetsPage(
        entries,
        capacity,
        anchorExclusive,
        direction,
        sequencer_.presetLibrary.pattern().sourceFilter,
        sequencer_.presetLibrary.pattern().target.trackKind
    );
    if (listed.status == SequencerPatternPresetDomainStatus::QUEUED &&
        sequencer_.presetLibrary.pattern().sourceFilter ==
            sequencer::SequencerPatternPresetSourceFilter::ALL &&
        pattern_presets_.playbackActive()) {
        sequencer_.presetLibrary.pattern().sourceFilter =
            sequencer::SequencerPatternPresetSourceFilter::FACTORY;
        listed = pattern_presets_.listPresetsPage(
            entries,
            capacity,
            anchorExclusive,
            direction,
            sequencer::SequencerPatternPresetSourceFilter::FACTORY,
            sequencer_.presetLibrary.pattern().target.trackKind
        );
    }
    if (listed.status == SequencerPatternPresetDomainStatus::QUEUED) {
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
SequencerPatternPresetLibraryAdapter::clearInspection() {
    sequencer_.presetLibrary.pattern().descriptor = {};
}

FLASHMEM sequencer::SequencerPresetLibraryFeedback
SequencerPatternPresetLibraryAdapter::inspect(
    const char* assetId,
    bool force
) {
    auto& picker = sequencer_.presetLibrary;
    auto& pattern = picker.pattern();
    if (assetId == nullptr || assetId[0] == '\0') {
        clearInspection();
        picker.inspecting.set(false);
        picker.bump();
        return sequencer::SequencerPresetLibraryFeedback::EMPTY;
    }

    pattern.target.projectRevision = pattern_presets_.projectRevision();
    const sequencer::SequencerPatternPresetPreviewKey wanted{
        .targetHash = sequencer::sequencerPatternPresetTargetHash(
            pattern.target
        ),
        .projectRevision = pattern.target.projectRevision,
    };
    if (!force && pattern.descriptor.valid &&
        std::strcmp(
            pattern.descriptor.metadata.technicalId,
            assetId
        ) == 0 &&
        pattern.descriptor.previewKey.targetHash == wanted.targetHash &&
        pattern.descriptor.previewKey.projectRevision ==
            wanted.projectRevision) {
        return picker.feedback.get();
    }

    uint32_t generation = picker.previewGeneration.get() + 1U;
    if (generation == 0U) generation = 1U;
    picker.previewGeneration.set(generation);
    picker.inspecting.set(true);
    picker.bump();

    const auto inspected = pattern_presets_.inspectPreset(
        assetId,
        pattern.target
    );
    if (picker.previewGeneration.get() != generation ||
        inspected.descriptor.previewKey.targetHash != wanted.targetHash ||
        inspected.descriptor.previewKey.projectRevision !=
            wanted.projectRevision) {
        return picker.feedback.get();
    }

    pattern.descriptor = inspected.descriptor;
    picker.inspecting.set(false);
    picker.bump();
    if (sequencer::sequencerPatternPresetCanApply(
            inspected.descriptor.compatibility
        )) {
        return sequencer::SequencerPresetLibraryFeedback::NONE;
    }
    if (inspected.descriptor.compatibility ==
        sequencer::SequencerPatternPresetCompatibility::
            STORAGE_UNAVAILABLE) {
        return sequencer::SequencerPresetLibraryFeedback::FAILED;
    }
    return sequencer::SequencerPresetLibraryFeedback::INCOMPATIBLE;
}

FLASHMEM uint8_t
SequencerPatternPresetLibraryAdapter::detailRowCount() const {
    return 3U;
}

FLASHMEM void
SequencerPatternPresetLibraryAdapter::adjustFocusedDetail(
    const char*,
    float
) {}

FLASHMEM contextual::ContextActionSpec
SequencerPatternPresetLibraryAdapter::actionSpec(
    bool saveMode,
    bool selectedNewAsset,
    bool hasFocusedAsset
) const {
    const auto& pattern = sequencer_.presetLibrary.pattern();
    return sequencer::buildSequencerPatternPresetActionSpec(
        saveMode,
        selectedNewAsset,
        hasFocusedAsset,
        pattern.target,
        pattern.descriptor
    );
}

FLASHMEM bool
SequencerPatternPresetLibraryAdapter::shouldCommitBeforeLoad(bool) const {
    return false;
}

FLASHMEM SequencerPresetLibraryResult
SequencerPatternPresetLibraryAdapter::execute(
    Mode mode,
    const char* assetId,
    bool createNew,
    bool overwriteAuthorized
) {
    auto& pattern = sequencer_.presetLibrary.pattern();
    pattern.activationGeneration = 0U;

    if (mode == Mode::SAVE) {
        char resolvedId[PickerState::ID_SIZE]{};
        SequencerPatternPresetActionResult action{};
        if (createNew) {
            action = pattern_presets_.nextPresetId(
                resolvedId,
                sizeof(resolvedId)
            );
            if (action.status == SequencerPatternPresetDomainStatus::QUEUED) {
                return {
                    .outcome = SequencerPresetLibraryOutcome::RETRY_PENDING,
                    .feedback = feedbackForResult(action),
                    .reason = reasonForResult(action),
                };
            }
            if (!action.ok()) {
                return {
                    .outcome = SequencerPresetLibraryOutcome::BLOCKED,
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

        action = pattern_presets_.savePreset(
            resolvedId,
            pattern.target,
            overwriteAuthorized
        );
        if (action.status == SequencerPatternPresetDomainStatus::QUEUED) {
            return {
                .outcome = SequencerPresetLibraryOutcome::RETRY_PENDING,
                .feedback = feedbackForResult(action),
                .reason = reasonForResult(action),
            };
        }
        if (!action.ok()) {
            return {
                .outcome = SequencerPresetLibraryOutcome::BLOCKED,
                .feedback = feedbackForResult(action),
                .reason = reasonForResult(action),
            };
        }
        SequencerPresetLibraryResult result{};
        result.outcome = SequencerPresetLibraryOutcome::SAVED;
        result.feedback =
            sequencer::SequencerPresetLibraryFeedback::SAVED;
        copyAssetId(result, action.presetId);
        return result;
    }

    const auto action = pattern_presets_.applyPreset(
        assetId,
        pattern.target,
        pattern.descriptor.previewKey
    );
    if (action.status == SequencerPatternPresetDomainStatus::QUEUED &&
        action.activation != SequencerPatternPresetActivation::QUEUED) {
        return {
            .outcome = SequencerPresetLibraryOutcome::RETRY_PENDING,
            .feedback = feedbackForResult(action),
            .reason = reasonForResult(action),
        };
    }
    if (!action.ok()) {
        return {
            .outcome = SequencerPresetLibraryOutcome::LOAD_FAILED,
            .feedback = feedbackForResult(action),
            .reason = reasonForResult(action),
        };
    }

    pattern.activationGeneration = action.activationGeneration;
    SequencerPresetLibraryResult result{};
    result.refreshPublishedState = true;
    if (action.activation == SequencerPatternPresetActivation::QUEUED) {
        result.outcome = SequencerPresetLibraryOutcome::QUEUED;
        result.feedback =
            sequencer::SequencerPresetLibraryFeedback::QUEUED;
        result.reason = contextual::ContextActionReason::PENDING;
    } else {
        result.outcome = SequencerPresetLibraryOutcome::LOADED;
        result.feedback =
            sequencer::SequencerPresetLibraryFeedback::LOADED;
    }
    return result;
}

FLASHMEM SequencerPresetLibraryResult
SequencerPatternPresetLibraryAdapter::update(uint32_t) {
    auto& picker = sequencer_.presetLibrary;
    auto& pattern = picker.pattern();
    if (picker.feedback.get() !=
            sequencer::SequencerPresetLibraryFeedback::QUEUED ||
        pattern.activationGeneration == 0U) {
        return {};
    }

    using ActivationStatus =
        sequencer::SequencerTrackActivationStatus;
    const auto activation = pattern_presets_.activationStatus(
        pattern.target.trackIndex,
        pattern.activationGeneration
    );
    if (activation == ActivationStatus::APPLIED) {
        return {
            .outcome = SequencerPresetLibraryOutcome::LOADED,
            .feedback =
                sequencer::SequencerPresetLibraryFeedback::LOADED,
            .reason = contextual::ContextActionReason::NONE,
            .refreshPublishedState = true,
        };
    }
    if (activation == ActivationStatus::CANCELLED ||
        activation == ActivationStatus::IDLE) {
        return {
            .outcome = SequencerPresetLibraryOutcome::CANCELLED,
            .feedback =
                sequencer::SequencerPresetLibraryFeedback::CANCELLED,
            .reason = contextual::ContextActionReason::STALE_TARGET,
            .refreshPublishedState = true,
        };
    }
    return {};
}

}  // namespace core::handler
