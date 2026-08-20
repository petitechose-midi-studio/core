#include "handler/sequencer/SequencerPatternPresetLibraryAdapter.hpp"

#include <cstdio>
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

FLASHMEM void copyDisplayName(
    SequencerPresetLibraryResult& result,
    const char* displayName
) {
    std::strncpy(
        result.displayName,
        displayName != nullptr ? displayName : "",
        sizeof(result.displayName) - 1U
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
    auto adapter = SequencerPresetLibraryAdapterBinding<
        SequencerPatternPresetLibraryAdapter>::bind(
            *this,
            sequencer::SequencerPresetLibraryKind::PATTERN
        );
    adapter.enterFolder = enterFolder_;
    adapter.leaveFolder = leaveFolder_;
    adapter.createFolder = createFolder_;
    adapter.beginManagement = beginManagement_;
    adapter.renameManaged = renameManaged_;
    adapter.moveManaged = moveManaged_;
    adapter.deleteManaged = deleteManaged_;
    return adapter;
}

FLASHMEM bool SequencerPatternPresetLibraryAdapter::beginSession() {
    const bool editorActive = sequencer_.patternEditor.active.get() ||
        sequencer_.drumSequencer.laneEditor.active;
    if (!editorActive || preview_session_.active()) {
        return false;
    }
    auto& pattern = sequencer_.presetLibrary.pattern();
    pattern.target = pattern_presets_.captureTarget();
    pattern.descriptor = {};
    pattern.sourceFilter =
        sequencer::SequencerPatternPresetSourceFilter::ALL;
    pattern.location.reset();
    pattern.copySourceLocation.reset();
    pattern.copySourceId.fill('\0');
    pattern.copySourceName.fill('\0');
    pattern.factoryCopyPending = false;
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
    auto& pattern = sequencer_.presetLibrary.pattern();
    auto listed = pattern.panel ==
            sequencer::SequencerPatternPresetLibraryPanel::MOVE_DESTINATION
        ? pattern_presets_.listFoldersPage(
              entries,
              capacity,
              anchorExclusive,
              direction,
              pattern.location
          )
        : pattern_presets_.listPresetsPage(
              entries,
              capacity,
              anchorExclusive,
              direction,
              pattern.sourceFilter,
              pattern.target.trackKind,
              pattern.location
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
            sequencer_.presetLibrary.pattern().target.trackKind,
            sequencer_.presetLibrary.pattern().location
        );
    }
    if (listed.status == SequencerPatternPresetDomainStatus::QUEUED) {
        return SequencerPresetLibraryPager::PageLoadStatus::PENDING;
    }
    if (!listed.ok()) {
        return SequencerPresetLibraryPager::PageLoadStatus::FAILED;
    }
    if (pattern.panel ==
            sequencer::SequencerPatternPresetLibraryPanel::MOVE_DESTINATION &&
        pattern.managedEntryKind ==
            sequencer::SequencerPresetLibraryEntryKind::FOLDER &&
        std::strcmp(
            pattern.location.relativeDirectory.data(),
            pattern.managedLocation.relativeDirectory.data()
        ) == 0) {
        char sourceEntryId[PickerState::ID_SIZE]{};
        std::snprintf(
            sourceEntryId,
            sizeof(sourceEntryId),
            "@%s",
            pattern.managedEntryId.data()
        );
        for (uint8_t index = 0U; index < listed.count; ++index) {
            if (std::strcmp(entries[index].id, sourceEntryId) != 0) continue;
            for (uint8_t next = static_cast<uint8_t>(index + 1U);
                 next < listed.count;
                 ++next) {
                entries[next - 1U] = entries[next];
            }
            --listed.count;
            if (listed.totalCount > 0U) --listed.totalCount;
            break;
        }
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
        pattern.target,
        pattern.location
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
    // Pattern detail is a read-only musical thumbnail. NAV is deliberately a
    // consumed no-op here, matching the common library grammar instead of
    // focusing invisible pseudo-rows.
    return 0U;
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
        if (pattern.factoryCopyPending) {
            if (!createNew) {
                return {
                    .outcome = SequencerPresetLibraryOutcome::BLOCKED,
                    .feedback = sequencer::SequencerPresetLibraryFeedback::FAILED,
                    .reason = contextual::ContextActionReason::NO_ACTION,
                };
            }

            const auto action = pattern_presets_.copyFactoryPreset(
                pattern.copySourceId.data(),
                pattern.location
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

            pattern.factoryCopyPending = false;
            pattern.copySourceLocation.reset();
            pattern.copySourceId.fill('\0');
            pattern.copySourceName.fill('\0');
            SequencerPresetLibraryResult result{};
            result.outcome = SequencerPresetLibraryOutcome::SAVED;
            result.feedback = sequencer::SequencerPresetLibraryFeedback::SAVED;
            copyAssetId(result, action.presetId);
            return result;
        }

        char resolvedId[PickerState::ID_SIZE]{};
        SequencerPatternPresetActionResult action{};
        if (createNew) {
            action = pattern_presets_.nextPresetId(
                resolvedId,
                sizeof(resolvedId),
                pattern.location
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
            overwriteAuthorized,
            pattern.location
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
        result.returnToParent = true;
        copyAssetId(result, action.presetId);
        copyDisplayName(result, action.semanticName);
        return result;
    }

    const auto action = pattern_presets_.previewPreset(
        assetId,
        pattern.target,
        pattern.descriptor.previewKey,
        preview_session_,
        pattern.location
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
    if (preview_session_.active()) {
        sequencer_.patternPresetPreview.begin(
            pattern.target,
            pattern.descriptor.metadata.semanticName,
            action.activation == SequencerPatternPresetActivation::QUEUED
        );
    }
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
SequencerPatternPresetLibraryAdapter::confirmPreview() {
    const auto action = pattern_presets_.confirmPresetPreview(preview_session_);
    if (!action.ok()) {
        return {
            .outcome = SequencerPresetLibraryOutcome::BLOCKED,
            .feedback = feedbackForResult(action),
            .reason = reasonForResult(action),
        };
    }
    sequencer_.patternPresetPreview.reset();
    return {
        .outcome = SequencerPresetLibraryOutcome::LOADED,
        .feedback = sequencer::SequencerPresetLibraryFeedback::LOADED,
        .reason = contextual::ContextActionReason::NONE,
        .refreshPublishedState = true,
    };
}

FLASHMEM SequencerPresetLibraryResult
SequencerPatternPresetLibraryAdapter::cancelPreview() {
    const auto action = pattern_presets_.cancelPresetPreview(preview_session_);
    if (!action.ok()) {
        return {
            .outcome = SequencerPresetLibraryOutcome::BLOCKED,
            .feedback = feedbackForResult(action),
            .reason = reasonForResult(action),
        };
    }
    sequencer_.patternPresetPreview.reset();
    return {
        .outcome = SequencerPresetLibraryOutcome::CANCELLED,
        .feedback = sequencer::SequencerPresetLibraryFeedback::CANCELLED,
        .reason = action.activation == SequencerPatternPresetActivation::QUEUED
            ? contextual::ContextActionReason::PENDING
            : contextual::ContextActionReason::NONE,
        .refreshPublishedState = true,
    };
}

FLASHMEM void SequencerPatternPresetLibraryAdapter::updatePreview() {
    if (!preview_session_.active()) return;
    if (!pattern_presets_.targetMatches(preview_session_.target)) {
        preview_session_.reset();
        sequencer_.patternPresetPreview.reset();
        return;
    }
    const auto status = pattern_presets_.activationStatus(
        preview_session_.target.trackIndex,
        preview_session_.activationGeneration
    );
    if (status == sequencer::SequencerTrackActivationStatus::APPLIED) {
        sequencer_.patternPresetPreview.setQueued(false);
    }
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

FLASHMEM bool SequencerPatternPresetLibraryAdapter::enterFolder(
    const char* entryId
) {
    char folder[
        sequencer::SequencerPatternPresetLocation::MAX_FOLDER_NAME_SIZE + 1U
    ]{};
    if (!core::persistence::PatternPresetFileStore::folderNameFromEntryId(
            entryId,
            folder,
            sizeof(folder)
        )) {
        return false;
    }
    auto& pattern = sequencer_.presetLibrary.pattern();
    if (!pattern.location.enter(folder)) return false;
    // The aggregate source exposes User folders; Factory has its own
    // immutable category hierarchy.
    if (pattern.sourceFilter !=
        sequencer::SequencerPatternPresetSourceFilter::FACTORY) {
        pattern.sourceFilter =
            sequencer::SequencerPatternPresetSourceFilter::USER;
    }
    pattern.descriptor = {};
    return true;
}

FLASHMEM bool SequencerPatternPresetLibraryAdapter::leaveFolder() {
    auto& pattern = sequencer_.presetLibrary.pattern();
    if (!pattern.location.leave()) return false;
    pattern.descriptor = {};
    return true;
}

FLASHMEM SequencerPresetLibraryResult
SequencerPatternPresetLibraryAdapter::createFolder(
    const char* folderName
) {
    const auto action = pattern_presets_.createFolder(
        sequencer_.presetLibrary.pattern().location,
        folderName
    );
    if (!action.ok()) {
        return {
            // Folder creation has its own UI draft and is not the ordinary
            // preset-save retry path. Keep the draft visible so the user can
            // retry instead of accidentally saving a Pattern.
            .outcome = SequencerPresetLibraryOutcome::BLOCKED,
            .feedback = feedbackForResult(action),
            .reason = reasonForResult(action),
        };
    }
    SequencerPresetLibraryResult result{};
    result.outcome = SequencerPresetLibraryOutcome::SAVED;
    result.feedback = sequencer::SequencerPresetLibraryFeedback::SAVED;
    return result;
}

FLASHMEM bool SequencerPatternPresetLibraryAdapter::beginManagement(
    sequencer::SequencerPresetLibraryEntryKind kind,
    const char* entryId,
    const char* entryName
) {
    if (entryId == nullptr || entryName == nullptr || entryId[0] == '\0' ||
        entryName[0] == '\0') {
        return false;
    }
    auto& pattern = sequencer_.presetLibrary.pattern();
    if (pattern.sourceFilter ==
        sequencer::SequencerPatternPresetSourceFilter::FACTORY) {
        return false;
    }
    char resolvedId[PickerState::ID_SIZE]{};
    if (kind == sequencer::SequencerPresetLibraryEntryKind::FOLDER) {
        if (!core::persistence::PatternPresetFileStore::folderNameFromEntryId(
                entryId,
                resolvedId,
                sizeof(resolvedId)
            )) {
            return false;
        }
    } else {
        if (!pattern.descriptor.valid ||
            pattern.descriptor.source !=
                sequencer::SequencerPatternPresetSource::USER ||
            std::strcmp(
                pattern.descriptor.metadata.technicalId,
                entryId
            ) != 0) {
            return false;
        }
        std::strncpy(resolvedId, entryId, sizeof(resolvedId) - 1U);
    }

    pattern.managedLocation = pattern.location;
    pattern.managedEntryKind = kind;
    std::strncpy(
        pattern.managedEntryId.data(),
        resolvedId,
        pattern.managedEntryId.size() - 1U
    );
    std::strncpy(
        pattern.managedEntryName.data(),
        entryName,
        pattern.managedEntryName.size() - 1U
    );
    return true;
}

FLASHMEM SequencerPresetLibraryResult
SequencerPatternPresetLibraryAdapter::renameManaged(const char* newName) {
    const auto& pattern = sequencer_.presetLibrary.pattern();
    const auto action = pattern.managedEntryKind ==
            sequencer::SequencerPresetLibraryEntryKind::FOLDER
        ? pattern_presets_.renameFolder(
              pattern.managedLocation,
              pattern.managedEntryId.data(),
              newName
          )
        : pattern_presets_.renamePreset(
              pattern.managedEntryId.data(),
              pattern.managedEntryName.data(),
              newName,
              pattern.managedLocation
          );
    return {
        .outcome = action.ok()
            ? SequencerPresetLibraryOutcome::SAVED
            : SequencerPresetLibraryOutcome::BLOCKED,
        .feedback = action.ok()
            ? sequencer::SequencerPresetLibraryFeedback::SAVED
            : feedbackForResult(action),
        .reason = action.ok()
            ? contextual::ContextActionReason::NONE
            : reasonForResult(action),
    };
}

FLASHMEM SequencerPresetLibraryResult
SequencerPatternPresetLibraryAdapter::moveManaged() {
    const auto& pattern = sequencer_.presetLibrary.pattern();
    const auto action = pattern.managedEntryKind ==
            sequencer::SequencerPresetLibraryEntryKind::FOLDER
        ? pattern_presets_.moveFolder(
              pattern.managedLocation,
              pattern.managedEntryId.data(),
              pattern.location
          )
        : pattern_presets_.movePreset(
              pattern.managedLocation,
              pattern.managedEntryId.data(),
              pattern.location
          );
    return {
        .outcome = action.ok()
            ? SequencerPresetLibraryOutcome::SAVED
            : SequencerPresetLibraryOutcome::BLOCKED,
        .feedback = action.ok()
            ? sequencer::SequencerPresetLibraryFeedback::SAVED
            : feedbackForResult(action),
        .reason = action.ok()
            ? contextual::ContextActionReason::NONE
            : reasonForResult(action),
    };
}

FLASHMEM SequencerPresetLibraryResult
SequencerPatternPresetLibraryAdapter::deleteManaged() {
    const auto& pattern = sequencer_.presetLibrary.pattern();
    const auto action = pattern.managedEntryKind ==
            sequencer::SequencerPresetLibraryEntryKind::FOLDER
        ? pattern_presets_.deleteFolder(
              pattern.managedLocation,
              pattern.managedEntryId.data()
          )
        : pattern_presets_.deletePreset(
              pattern.managedEntryId.data(),
              pattern.managedEntryName.data(),
              pattern.managedLocation
          );
    return {
        .outcome = action.ok()
            ? SequencerPresetLibraryOutcome::SAVED
            : SequencerPresetLibraryOutcome::BLOCKED,
        .feedback = action.ok()
            ? sequencer::SequencerPresetLibraryFeedback::SAVED
            : feedbackForResult(action),
        .reason = action.ok()
            ? contextual::ContextActionReason::NONE
            : reasonForResult(action),
    };
}

FLASHMEM bool SequencerPatternPresetLibraryAdapter::enterFolder_(
    void* context,
    const char* entryId
) {
    return static_cast<SequencerPatternPresetLibraryAdapter*>(context)
        ->enterFolder(entryId);
}

FLASHMEM bool SequencerPatternPresetLibraryAdapter::leaveFolder_(
    void* context
) {
    return static_cast<SequencerPatternPresetLibraryAdapter*>(context)
        ->leaveFolder();
}

FLASHMEM SequencerPresetLibraryResult
SequencerPatternPresetLibraryAdapter::createFolder_(
    void* context,
    const char* folderName
) {
    return static_cast<SequencerPatternPresetLibraryAdapter*>(context)
        ->createFolder(folderName);
}

FLASHMEM bool SequencerPatternPresetLibraryAdapter::beginManagement_(
    void* context,
    sequencer::SequencerPresetLibraryEntryKind kind,
    const char* entryId,
    const char* entryName
) {
    return static_cast<SequencerPatternPresetLibraryAdapter*>(context)
        ->beginManagement(kind, entryId, entryName);
}

FLASHMEM SequencerPresetLibraryResult
SequencerPatternPresetLibraryAdapter::renameManaged_(
    void* context,
    const char* newName
) {
    return static_cast<SequencerPatternPresetLibraryAdapter*>(context)
        ->renameManaged(newName);
}

FLASHMEM SequencerPresetLibraryResult
SequencerPatternPresetLibraryAdapter::moveManaged_(void* context) {
    return static_cast<SequencerPatternPresetLibraryAdapter*>(context)
        ->moveManaged();
}

FLASHMEM SequencerPresetLibraryResult
SequencerPatternPresetLibraryAdapter::deleteManaged_(void* context) {
    return static_cast<SequencerPatternPresetLibraryAdapter*>(context)
        ->deleteManaged();
}

}  // namespace core::handler
