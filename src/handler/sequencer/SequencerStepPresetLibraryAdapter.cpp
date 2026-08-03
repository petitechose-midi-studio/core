#include "handler/sequencer/SequencerStepPresetLibraryAdapter.hpp"

#include <cstring>
#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"

namespace core::handler {
namespace {

namespace contextual = core::state::contextual;
namespace sequencer = core::state::sequencer;

constexpr uint8_t DETAIL_ROW_COUNT = 5U;
constexpr uint8_t PREVIEW_DETAIL_ROW = 4U;

FLASHMEM contextual::ContextActionReason reasonForResult(
    const SequencerStepPresetActionResult& result
) {
    switch (result.status) {
        case SequencerStepPresetStatus::CAPACITY:
            return contextual::ContextActionReason::CAPACITY;
        case SequencerStepPresetStatus::CORRUPT:
            return contextual::ContextActionReason::CORRUPT_ASSET;
        case SequencerStepPresetStatus::UNSUPPORTED_VERSION:
            return contextual::ContextActionReason::UNSUPPORTED_VERSION;
        case SequencerStepPresetStatus::STALE_TARGET:
            return contextual::ContextActionReason::STALE_TARGET;
        case SequencerStepPresetStatus::COLLISION:
            return contextual::ContextActionReason::CONFLICT;
        case SequencerStepPresetStatus::STORAGE_UNAVAILABLE:
            return contextual::ContextActionReason::STORAGE_UNAVAILABLE;
        case SequencerStepPresetStatus::ALLOCATION_UNAVAILABLE:
            return contextual::ContextActionReason::ALLOCATION_UNAVAILABLE;
        case SequencerStepPresetStatus::HISTORY_UNAVAILABLE:
            return contextual::ContextActionReason::HISTORY_UNAVAILABLE;
        case SequencerStepPresetStatus::INCOMPATIBLE:
            return contextual::ContextActionReason::INCOMPATIBLE;
        case SequencerStepPresetStatus::EMPTY:
            return contextual::ContextActionReason::EMPTY_SELECTION;
        case SequencerStepPresetStatus::QUEUED:
            return contextual::ContextActionReason::PENDING;
        default:
            return contextual::ContextActionReason::FAILED;
    }
}

FLASHMEM sequencer::SequencerPresetLibraryFeedback feedbackForResult(
    const SequencerStepPresetActionResult& result
) {
    using Feedback = sequencer::SequencerPresetLibraryFeedback;
    if (result.status == SequencerStepPresetStatus::EMPTY) {
        return Feedback::EMPTY;
    }
    if (result.status == SequencerStepPresetStatus::QUEUED) {
        return Feedback::QUEUED;
    }
    if (result.status == SequencerStepPresetStatus::INCOMPATIBLE ||
        result.status == SequencerStepPresetStatus::CAPACITY ||
        result.status == SequencerStepPresetStatus::CORRUPT ||
        result.status ==
            SequencerStepPresetStatus::UNSUPPORTED_VERSION ||
        result.status == SequencerStepPresetStatus::STALE_TARGET) {
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

FLASHMEM SequencerStepPresetLibraryAdapter::
SequencerStepPresetLibraryAdapter(
    sequencer::SequencerState& sequencer,
    SequencerStepPresetDomainServices& stepPresets
) : sequencer_(sequencer),
    step_presets_(stepPresets) {}

FLASHMEM SequencerPresetLibraryAdapter
SequencerStepPresetLibraryAdapter::operations() {
    SequencerPresetLibraryAdapter ops{};
    ops.context = this;
    ops.kind = sequencer::SequencerPresetLibraryKind::STEP;
    ops.beginSession = beginSession_;
    ops.loadPage = loadPage_;
    ops.clearInspection = clearInspection_;
    ops.inspect = inspect_;
    ops.detailRowCount = detailRowCount_;
    ops.adjustFocusedDetail = adjustFocusedDetail_;
    ops.actionSpec = actionSpec_;
    ops.shouldCommitBeforeLoad = shouldCommitBeforeLoad_;
    ops.execute = execute_;
    ops.update = update_;
    return ops;
}

FLASHMEM bool SequencerStepPresetLibraryAdapter::beginSession_(
    void* context
) {
    auto* self =
        static_cast<SequencerStepPresetLibraryAdapter*>(context);
    return self != nullptr && self->beginSession();
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerStepPresetLibraryAdapter::loadPage_(
    void* context,
    SequencerPresetLibraryPager::Entry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    SequencerPresetLibraryPager::PageDirection direction,
    core::persistence::ProductAssetFileListResult& out
) {
    auto* self =
        static_cast<SequencerStepPresetLibraryAdapter*>(context);
    return self != nullptr
        ? self->loadPage(
              entries,
              capacity,
              anchorExclusive,
              direction,
              out
          )
        : SequencerPresetLibraryPager::PageLoadStatus::FAILED;
}

FLASHMEM void SequencerStepPresetLibraryAdapter::clearInspection_(
    void* context
) {
    auto* self =
        static_cast<SequencerStepPresetLibraryAdapter*>(context);
    if (self != nullptr) self->clearInspection();
}

FLASHMEM sequencer::SequencerPresetLibraryFeedback
SequencerStepPresetLibraryAdapter::inspect_(
    void* context,
    const char* assetId,
    bool force
) {
    auto* self =
        static_cast<SequencerStepPresetLibraryAdapter*>(context);
    return self != nullptr
        ? self->inspect(assetId, force)
        : sequencer::SequencerPresetLibraryFeedback::FAILED;
}

FLASHMEM uint8_t
SequencerStepPresetLibraryAdapter::detailRowCount_(const void*) {
    return DETAIL_ROW_COUNT;
}

FLASHMEM void
SequencerStepPresetLibraryAdapter::adjustFocusedDetail_(
    void* context,
    const char* assetId,
    float delta
) {
    auto* self =
        static_cast<SequencerStepPresetLibraryAdapter*>(context);
    if (self != nullptr) {
        self->adjustFocusedDetail(assetId, delta);
    }
}

FLASHMEM contextual::ContextActionSpec
SequencerStepPresetLibraryAdapter::actionSpec_(
    const void* context,
    bool saveMode,
    bool selectedNewAsset,
    bool hasFocusedAsset
) {
    const auto* self =
        static_cast<const SequencerStepPresetLibraryAdapter*>(context);
    return self != nullptr
        ? self->actionSpec(
              saveMode,
              selectedNewAsset,
              hasFocusedAsset
          )
        : contextual::ContextActionSpec{};
}

FLASHMEM bool
SequencerStepPresetLibraryAdapter::shouldCommitBeforeLoad_(
    const void* context,
    bool hasFocusedAsset
) {
    return context != nullptr && hasFocusedAsset;
}

FLASHMEM SequencerPresetLibraryResult
SequencerStepPresetLibraryAdapter::execute_(
    void* context,
    Mode mode,
    const char* assetId,
    bool createNew,
    bool overwriteAuthorized
) {
    auto* self =
        static_cast<SequencerStepPresetLibraryAdapter*>(context);
    return self != nullptr
        ? self->execute(
              mode,
              assetId,
              createNew,
              overwriteAuthorized
          )
        : SequencerPresetLibraryResult{
              .outcome = SequencerPresetLibraryOutcome::BLOCKED,
              .feedback =
                  sequencer::SequencerPresetLibraryFeedback::FAILED,
              .reason = contextual::ContextActionReason::FAILED,
          };
}

FLASHMEM SequencerPresetLibraryResult
SequencerStepPresetLibraryAdapter::update_(
    void* context,
    uint32_t nowMs
) {
    auto* self =
        static_cast<SequencerStepPresetLibraryAdapter*>(context);
    return self != nullptr
        ? self->update(nowMs)
        : SequencerPresetLibraryResult{};
}

FLASHMEM bool SequencerStepPresetLibraryAdapter::beginSession() {
    if (!sequencer_.stepEdit.visible.get()) return false;
    sequencer_.stepEdit.contextHold.clear();
    sequencer_.stepEdit.localVariationEditActive.set(false);
    auto& picker = sequencer_.presetLibrary;
    auto& step = picker.step();
    step.target = step_presets_.captureTarget();
    step.descriptor = {};
    step.activationGeneration = 0U;
    return true;
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerStepPresetLibraryAdapter::loadPage(
    SequencerPresetLibraryPager::Entry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    SequencerPresetLibraryPager::PageDirection direction,
    core::persistence::ProductAssetFileListResult& out
) {
    const auto listed = step_presets_.listPresetsPage(
        entries,
        capacity,
        anchorExclusive,
        direction
    );
    if (listed.status == SequencerStepPresetStatus::QUEUED) {
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
SequencerStepPresetLibraryAdapter::clearInspection() {
    sequencer_.presetLibrary.step().descriptor = {};
}

FLASHMEM sequencer::SequencerPresetLibraryFeedback
SequencerStepPresetLibraryAdapter::inspect(
    const char* assetId,
    bool force
) {
    auto& picker = sequencer_.presetLibrary;
    auto& step = picker.step();
    if (assetId == nullptr || assetId[0] == '\0') {
        clearInspection();
        picker.inspecting.set(false);
        picker.bump();
        return sequencer::SequencerPresetLibraryFeedback::EMPTY;
    }

    step.target.projectRevision =
        step_presets_.projectRevision();
    const sequencer::SequencerStepPresetPreviewKey wanted{
        .assetHash =
            sequencer::sequencerStepPresetIdHash(assetId),
        .targetHash =
            sequencer::sequencerStepPresetTargetHash(
                step.target
            ),
        .projectRevision =
            step.target.projectRevision,
        .stateIndex = picker.previewStateIndex.get(),
    };
    if (!force && step.descriptor.valid &&
        std::strcmp(
            step.descriptor.technicalId,
            assetId
        ) == 0 &&
        step.descriptor.previewKey.targetHash ==
            wanted.targetHash &&
        step.descriptor.previewKey.projectRevision ==
            wanted.projectRevision &&
        step.descriptor.previewKey.stateIndex ==
            wanted.stateIndex) {
        return picker.feedback.get();
    }

    uint32_t generation =
        picker.previewGeneration.get() + 1U;
    if (generation == 0U) generation = 1U;
    picker.previewGeneration.set(generation);
    picker.inspecting.set(true);
    picker.bump();

    const auto inspected = step_presets_.inspectPreset(
        assetId,
        step.target,
        picker.previewStateIndex.get(),
        generation
    );
    if (picker.previewGeneration.get() != generation ||
        !sequencer::sequencerStepPresetInspectionMatches(
            generation,
            wanted,
            inspected.descriptor
        )) {
        return picker.feedback.get();
    }

    step.descriptor = inspected.descriptor;
    picker.inspecting.set(false);
    picker.bump();
    const auto compatibility =
        inspected.descriptor.compatibility;
    if (sequencer::sequencerStepPresetCanApply(
            compatibility
        )) {
        return sequencer::SequencerPresetLibraryFeedback::NONE;
    }
    if (compatibility ==
        sequencer::SequencerStepPresetCompatibility::
            STORAGE_UNAVAILABLE) {
        return sequencer::SequencerPresetLibraryFeedback::FAILED;
    }
    return sequencer::SequencerPresetLibraryFeedback::INCOMPATIBLE;
}

FLASHMEM void
SequencerStepPresetLibraryAdapter::adjustFocusedDetail(
    const char* assetId,
    float delta
) {
    auto& picker = sequencer_.presetLibrary;
    auto& step = picker.step();
    if (!picker.detailVisible.get() ||
        picker.detailFocus.get() != PREVIEW_DETAIL_ROW ||
        !nav::hasTurnDelta(delta)) {
        return;
    }
    const uint8_t count = std::max<uint8_t>(
        1U,
        step.descriptor.previewStateCount
    );
    picker.previewStateIndex.set(static_cast<uint8_t>(
        nav::nextWrappedIndex(
            delta,
            picker.previewStateIndex.get(),
            count
        )
    ));
    (void)inspect(assetId, true);
}

FLASHMEM contextual::ContextActionSpec
SequencerStepPresetLibraryAdapter::actionSpec(
    bool saveMode,
    bool selectedNewAsset,
    bool hasFocusedAsset
) const {
    const auto& picker = sequencer_.presetLibrary;
    const auto& step = picker.step();
    return sequencer::buildSequencerStepPresetActionSpec(
        saveMode,
        selectedNewAsset,
        hasFocusedAsset,
        step.target,
        step.descriptor
    );
}

FLASHMEM SequencerPresetLibraryResult
SequencerStepPresetLibraryAdapter::execute(
    Mode mode,
    const char* assetId,
    bool createNew,
    bool overwriteAuthorized
) {
    auto& picker = sequencer_.presetLibrary;
    auto& step = picker.step();
    step.activationGeneration = 0U;

    if (mode == Mode::SAVE) {
        char resolvedId[PickerState::ID_SIZE]{};
        SequencerStepPresetActionResult action{};
        if (createNew) {
            action = step_presets_.nextPresetId(
                resolvedId,
                sizeof(resolvedId)
            );
            if (action.status == SequencerStepPresetStatus::QUEUED) {
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

        action = step_presets_.savePreset(
            resolvedId,
            step.target,
            overwriteAuthorized
        );
        if (action.status == SequencerStepPresetStatus::QUEUED) {
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

    const auto action = step_presets_.applyPreset(
        assetId,
        step.target,
        step.descriptor.previewKey
    );
    if (action.status == SequencerStepPresetStatus::QUEUED &&
        action.activation != SequencerStepPresetActivation::QUEUED) {
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

    step.activationGeneration =
        action.activationGeneration;
    SequencerPresetLibraryResult result{};
    result.refreshPublishedState = true;
    if (action.activation ==
        SequencerStepPresetActivation::QUEUED) {
        result.outcome =
            SequencerPresetLibraryOutcome::QUEUED;
        result.feedback =
            sequencer::SequencerPresetLibraryFeedback::QUEUED;
        result.reason = contextual::ContextActionReason::PENDING;
    } else {
        result.outcome =
            SequencerPresetLibraryOutcome::LOADED;
        result.feedback =
            sequencer::SequencerPresetLibraryFeedback::LOADED;
    }
    return result;
}

FLASHMEM SequencerPresetLibraryResult
SequencerStepPresetLibraryAdapter::update(uint32_t) {
    auto& picker = sequencer_.presetLibrary;
    auto& step = picker.step();
    if (picker.feedback.get() !=
            sequencer::SequencerPresetLibraryFeedback::QUEUED ||
        step.activationGeneration == 0U) {
        return {};
    }

    using ActivationStatus =
        sequencer::SequencerTrackActivationStatus;
    const auto activation = step_presets_.activationStatus(
        step.target.trackIndex,
        step.activationGeneration
    );
    if (activation == ActivationStatus::APPLIED) {
        return {
            .outcome =
                SequencerPresetLibraryOutcome::LOADED,
            .feedback =
                sequencer::SequencerPresetLibraryFeedback::LOADED,
            .reason = contextual::ContextActionReason::NONE,
            .refreshPublishedState = true,
        };
    }
    if (activation == ActivationStatus::CANCELLED ||
        activation == ActivationStatus::IDLE) {
        return {
            .outcome =
                SequencerPresetLibraryOutcome::CANCELLED,
            .feedback =
                sequencer::SequencerPresetLibraryFeedback::CANCELLED,
            .reason =
                contextual::ContextActionReason::STALE_TARGET,
            .refreshPublishedState = true,
        };
    }
    return {};
}

}  // namespace core::handler
