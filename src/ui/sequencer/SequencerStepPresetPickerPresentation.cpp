#include "ui/sequencer/SequencerStepPresetPickerPresentation.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "ui/font/StandaloneIcons.hpp"

namespace core::ui::sequencer {
namespace {

namespace contextual = core::state::contextual;
using Picker = core::state::sequencer::SequencerStepPresetPickerState;
using Presentation = SequencerStepPresetPickerPresentation;

FLASHMEM uint32_t mixRevision(uint32_t seed, uint32_t value) {
    return (seed ^ value) * 16777619U;
}

FLASHMEM const char* feedbackLabel(
    core::state::sequencer::SequencerStepPresetFeedback feedback
) {
    using Feedback = core::state::sequencer::SequencerStepPresetFeedback;
    switch (feedback) {
        case Feedback::SAVED: return "Saved";
        case Feedback::APPLIED: return "Applied";
        case Feedback::QUEUED: return "Queued for loop boundary";
        case Feedback::CANCELLED: return "Cancelled";
        case Feedback::EMPTY: return "No preset";
        case Feedback::INCOMPATIBLE: return "Incompatible";
        case Feedback::FAILED: return "Failed";
        case Feedback::NONE:
        default: return "";
    }
}

FLASHMEM const char* operationReasonLabel(
    contextual::ContextActionReason reason
) {
    using Reason = contextual::ContextActionReason;
    switch (reason) {
        case Reason::CAPACITY: return "Capacity full";
        case Reason::STORAGE_UNAVAILABLE: return "Storage unavailable";
        case Reason::ALLOCATION_UNAVAILABLE: return "Memory unavailable";
        case Reason::CORRUPT_ASSET: return "Corrupt preset";
        case Reason::UNSUPPORTED_VERSION: return "Unsupported preset version";
        case Reason::STALE_TARGET: return "Target changed";
        case Reason::CONFLICT: return "Conflict - retry";
        case Reason::INCOMPATIBLE: return "Incompatible target";
        case Reason::HISTORY_UNAVAILABLE: return "Undo unavailable";
        case Reason::READ_ONLY: return "Read only";
        case Reason::NO_ROUTE: return "No MIDI route";
        case Reason::FAILED: return "Operation failed";
        case Reason::PENDING: return "Waiting for loop boundary";
        case Reason::NONE:
        default: return "";
    }
}

FLASHMEM const char* operationLabel(
    const contextual::OperationFeedbackState& feedback
) {
    const char* reason = operationReasonLabel(feedback.reason);
    using Status = contextual::OperationFeedbackStatus;
    if ((feedback.status == Status::BLOCKED ||
         feedback.status == Status::CONFLICT ||
         feedback.status == Status::FAILED) && reason[0] != '\0') {
        return reason;
    }
    switch (feedback.status) {
        case Status::PRESSED: return "Hold to confirm";
        case Status::ARMED: return "Armed - keep holding";
        case Status::QUEUED: return "Queued for loop boundary";
        case Status::APPLIED: return "Applied";
        case Status::CANCELLED: return "Cancelled";
        case Status::BLOCKED: return "Blocked";
        case Status::WARNING: return "Check impact";
        case Status::CONFLICT: return "Target changed";
        case Status::FAILED: return "Failed";
        case Status::PREVIEW: return "Preview";
        case Status::NONE:
        default: return "";
    }
}

FLASHMEM const char* shortOperationLabel(
    contextual::OperationFeedbackStatus status
) {
    using Status = contextual::OperationFeedbackStatus;
    switch (status) {
        case Status::PRESSED: return "Hold";
        case Status::ARMED: return "Armed";
        case Status::QUEUED: return "Queued";
        case Status::APPLIED: return "Applied";
        case Status::CANCELLED: return "Cancelled";
        case Status::BLOCKED: return "Blocked";
        case Status::WARNING: return "Warning";
        case Status::CONFLICT: return "Conflict";
        case Status::FAILED: return "Failed";
        case Status::PREVIEW: return "Preview";
        case Status::NONE:
        default: return "";
    }
}

FLASHMEM bool duplicateName(const Picker& picker, uint8_t candidate) {
    if (!picker.entryHasReadableMetadata(candidate)) return false;
    const char* name = picker.entryName(candidate);
    if (name == nullptr || name[0] == '\0') return false;
    for (uint8_t i = 0; i < picker.entryCount.get(); ++i) {
        if (i != candidate && picker.entryHasReadableMetadata(i) &&
            std::strcmp(name, picker.entryName(i)) == 0) {
            return true;
        }
    }
    return false;
}

FLASHMEM void formatDetail(Presentation& data, const Picker& picker) {
    const auto& descriptor = picker.descriptor;
    const char* destinationScale = std::strstr(
        descriptor.adaptationSummary,
        "->"
    );
    if (destinationScale != nullptr) {
        destinationScale += 2;
        while (*destinationScale == ' ') ++destinationScale;
    } else {
        destinationScale = descriptor.adaptationSummary;
    }
    std::snprintf(
        data.title.data(),
        data.title.size(),
        "%s",
        descriptor.semanticName[0] != '\0'
            ? descriptor.semanticName
            : "Preset details"
    );
    std::snprintf(
        data.itemBuffers[0].data(),
        data.itemBuffers[0].size(),
        "Target   %s",
        picker.frozenTarget.contextLabel
    );
    std::snprintf(
        data.itemBuffers[1].data(),
        data.itemBuffers[1].size(),
        "Content  %s",
        descriptor.contentSummary
    );
    std::snprintf(
        data.itemBuffers[2].data(),
        data.itemBuffers[2].size(),
        "%s",
        descriptor.footprint == core::state::sequencer::
                SequencerStepPresetFootprint::REPLACE
            ? "Impact   Replace step; keeps route/scale"
            : "Impact   Add content; keeps route/scale"
    );
    if (descriptor.mixedPitchPolicy) {
        std::snprintf(
            data.itemBuffers[3].data(),
            data.itemBuffers[3].size(),
            "Pitch    Mixed; scale -> %.35s",
            destinationScale
        );
    } else if (descriptor.scalePolicy == core::state::sequencer::
                   SequencerStepPresetScalePolicy::SCALE_RELATIVE) {
        std::snprintf(
            data.itemBuffers[3].data(),
            data.itemBuffers[3].size(),
            "Pitch    Scale-relative -> %.35s",
            destinationScale
        );
    } else {
        std::snprintf(
            data.itemBuffers[3].data(),
            data.itemBuffers[3].size(),
            "Pitch    Chromatic (unchanged)"
        );
    }
    std::snprintf(
        data.itemBuffers[4].data(),
        data.itemBuffers[4].size(),
        "%s",
        descriptor.previewSummary
    );
    for (uint8_t i = 0; i < 5; ++i) {
        data.items[i] = data.itemBuffers[i].data();
    }
    data.itemCount = 5;
    data.selectedIndex = std::clamp<int>(picker.detailFocus.get(), 0, 4);
    std::snprintf(
        data.meta.data(),
        data.meta.size(),
        "%s",
        core::state::sequencer::sequencerStepPresetCompatibilityLabel(
            descriptor.compatibility
        )
    );
}

FLASHMEM SequencerStepPresetActionTone actionToneFor(
    contextual::ContextTone tone
) {
    switch (tone) {
        case contextual::ContextTone::GREEN:
            return SequencerStepPresetActionTone::POSITIVE;
        case contextual::ContextTone::AMBER:
            return SequencerStepPresetActionTone::WARNING;
        case contextual::ContextTone::RED:
            return SequencerStepPresetActionTone::DESTRUCTIVE;
        case contextual::ContextTone::BLUE:
            return SequencerStepPresetActionTone::CONSTRUCTIVE;
        case contextual::ContextTone::NEUTRAL:
        case contextual::ContextTone::DEFAULT:
        default:
            return SequencerStepPresetActionTone::NEUTRAL;
    }
}

}  // namespace

FLASHMEM SequencerStepPresetPickerPresentation
buildSequencerStepPresetPickerPresentation(
    const core::state::sequencer::SequencerState& sequencer
) {
    Presentation data{};
    const auto& picker = sequencer.stepPresetPicker;
    if (!picker.visible.get()) return data;

    using Mode = core::state::sequencer::SequencerStepPresetPickerMode;
    const bool saveMode = picker.mode.get() == Mode::SAVE;
    data.visible = true;
    if (picker.detailVisible.get() && picker.descriptor.valid) {
        formatDetail(data, picker);
    } else {
        std::snprintf(
            data.title.data(),
            data.title.size(),
            "%s",
            saveMode ? "Save Step Preset" : "Load Step Preset"
        );
        int itemIndex = 0;
        if (picker.newAssetItemOffset() > 0) {
            std::snprintf(
                data.itemBuffers[itemIndex].data(),
                data.itemBuffers[itemIndex].size(),
                "+  New Step Preset"
            );
            data.items[itemIndex] = data.itemBuffers[itemIndex].data();
            ++itemIndex;
        }
        for (uint8_t i = 0;
             i < picker.entryCount.get() &&
             itemIndex < static_cast<int>(data.items.size());
             ++i) {
            const bool readable = picker.entryHasReadableMetadata(i) &&
                picker.entryName(i)[0] != '\0';
            if (readable && duplicateName(picker, i)) {
                std::snprintf(
                    data.itemBuffers[itemIndex].data(),
                    data.itemBuffers[itemIndex].size(),
                    "%s  [%s]",
                    picker.entryName(i),
                    picker.entryId(i)
                );
            } else {
                std::snprintf(
                    data.itemBuffers[itemIndex].data(),
                    data.itemBuffers[itemIndex].size(),
                    "%s",
                    readable ? picker.entryName(i) : picker.entryId(i)
                );
            }
            data.items[itemIndex] = data.itemBuffers[itemIndex].data();
            ++itemIndex;
        }
        if (itemIndex == 0) {
            std::snprintf(
                data.itemBuffers[0].data(),
                data.itemBuffers[0].size(),
                "No Step Presets"
            );
            data.items[0] = data.itemBuffers[0].data();
            itemIndex = 1;
        }
        data.itemCount = itemIndex;
        data.selectedIndex = std::clamp<int>(
            picker.selectedIndex.get(),
            0,
            itemIndex - 1
        );

        const auto operation = picker.operationFeedback.get();
        const char* currentOperation = operation.active
            ? operationLabel(operation)
            : "";
        const char* currentFeedback = feedbackLabel(picker.feedback.get());
        if (currentOperation[0] != '\0') {
            std::snprintf(
                data.meta.data(),
                data.meta.size(),
                "%s",
                currentOperation
            );
        } else if (currentFeedback[0] != '\0') {
            std::snprintf(
                data.meta.data(),
                data.meta.size(),
                "%s",
                currentFeedback
            );
        } else if (!saveMode && picker.descriptor.valid) {
            const char* compatibility = core::state::sequencer::
                sequencerStepPresetCompatibilityLabel(
                    picker.descriptor.compatibility
                );
            const char* pagination = picker.hasPreviousPage.get()
                ? (picker.hasNextPage.get() ? "<  %s  >" : "<  %s")
                : (picker.hasNextPage.get() ? "%s  >" : "%s");
            std::snprintf(
                data.meta.data(),
                data.meta.size(),
                pagination,
                compatibility
            );
        } else {
            std::snprintf(
                data.meta.data(),
                data.meta.size(),
                "%s - Step %02u",
                picker.truncated.get() ? "More" : "Target",
                static_cast<unsigned>(sequencer.stepEdit.stepIndex.get() + 1U)
            );
        }
    }

    uint32_t revision = picker.revision.get();
    revision = mixRevision(revision, picker.visible.get() ? 1U : 0U);
    revision = mixRevision(revision, static_cast<uint32_t>(picker.mode.get()));
    revision = mixRevision(revision, picker.selectedIndex.get());
    revision = mixRevision(revision, picker.entryCount.get());
    revision = mixRevision(revision, picker.truncated.get() ? 1U : 0U);
    revision = mixRevision(revision, picker.hasPreviousPage.get() ? 1U : 0U);
    revision = mixRevision(revision, picker.hasNextPage.get() ? 1U : 0U);
    revision = mixRevision(revision, picker.totalEntryCount.get());
    revision = mixRevision(revision, picker.detailVisible.get() ? 1U : 0U);
    revision = mixRevision(revision, picker.detailFocus.get());
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(picker.descriptor.compatibility)
    );
    revision = mixRevision(revision, picker.descriptor.previewStateIndex);
    revision = mixRevision(revision, picker.descriptor.mixedPitchPolicy ? 1U : 0U);
    revision = mixRevision(revision, static_cast<uint32_t>(picker.feedback.get()));
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(picker.operationFeedback.get().status)
    );
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(picker.actionGuard.get().phase)
    );
    data.dataRevision = revision;
    return data;
}

FLASHMEM SequencerStepPresetActionPresentation
buildSequencerStepPresetActionPresentation(const Picker& picker) {
    SequencerStepPresetActionPresentation data{};
    using Mode = core::state::sequencer::SequencerStepPresetPickerMode;
    const bool saveMode = picker.mode.get() == Mode::SAVE;
    const bool focusedAsset = picker.entryCount.get() > 0 &&
        !picker.selectedItemIsNewAsset() &&
        picker.existingEntryIndexForSelectedItem() < picker.entryCount.get();
    const auto action = core::state::sequencer::
        buildSequencerStepPresetActionSpec(
            saveMode,
            picker.selectedItemIsNewAsset(),
            focusedAsset,
            picker.frozenTarget,
            picker.descriptor
        );
    const auto variant = contextual::hasHoldAction(action)
        ? action.hold
        : action.tap;
    data.saveIcon = saveMode;
    data.overwriteIcon = contextual::hasHoldAction(action);
    data.visual = variant.availability ==
            contextual::ContextActionAvailability::DISABLED
        ? SequencerStepPresetActionVisual::DISABLED
        : SequencerStepPresetActionVisual::ACTIVE;
    data.tone = actionToneFor(variant.visual.tone);

    const auto guard = picker.actionGuard.get();
    using Guard = contextual::GuardedActionPhase;
    if (guard.phase == Guard::PRESSED) {
        data.visual = SequencerStepPresetActionVisual::PRESSED;
        data.tone = SequencerStepPresetActionTone::WARNING;
    } else if (guard.phase == Guard::ARMED || guard.phase == Guard::COMMITTED) {
        data.visual = SequencerStepPresetActionVisual::ARMED;
        data.tone = SequencerStepPresetActionTone::WARNING;
    } else if (guard.phase == Guard::CANCELLED) {
        data.visual = SequencerStepPresetActionVisual::CANCELLED;
    }
    data.holdActive = guard.phase == Guard::PRESSED ||
        guard.phase == Guard::ARMED;
    data.holdStartedAtMs = guard.pressedAtMs;
    data.holdDurationMs = action.guard.durationMs;

    const auto feedback = picker.operationFeedback.get();
    if (!feedback.active) return data;
    using Status = contextual::OperationFeedbackStatus;
    switch (feedback.status) {
        case Status::PRESSED:
            data.visual = SequencerStepPresetActionVisual::PRESSED;
            data.tone = SequencerStepPresetActionTone::WARNING;
            data.statusIcon = standalone::icons::ACTION_OVERWRITE;
            break;
        case Status::ARMED:
            data.statusIcon = standalone::icons::ACTION_OVERWRITE;
            data.visual = SequencerStepPresetActionVisual::ARMED;
            data.tone = SequencerStepPresetActionTone::WARNING;
            break;
        case Status::QUEUED:
            data.statusIcon = standalone::icons::STATUS_QUEUED;
            data.visual = SequencerStepPresetActionVisual::ARMED;
            data.tone = SequencerStepPresetActionTone::WARNING;
            break;
        case Status::APPLIED:
            data.statusIcon = standalone::icons::ACTION_VALIDATE;
            data.visual = SequencerStepPresetActionVisual::APPLIED;
            data.tone = SequencerStepPresetActionTone::POSITIVE;
            break;
        case Status::CANCELLED:
            data.statusIcon = standalone::icons::ACTION_CANCEL;
            data.visual = SequencerStepPresetActionVisual::CANCELLED;
            data.tone = SequencerStepPresetActionTone::NEUTRAL;
            break;
        case Status::BLOCKED:
            data.statusIcon = standalone::icons::STATUS_ERROR;
            data.visual = SequencerStepPresetActionVisual::CANCELLED;
            data.tone = SequencerStepPresetActionTone::DESTRUCTIVE;
            break;
        case Status::CONFLICT:
            data.statusIcon = standalone::icons::STATUS_CONFLICT;
            data.visual = SequencerStepPresetActionVisual::CANCELLED;
            data.tone = SequencerStepPresetActionTone::DESTRUCTIVE;
            break;
        case Status::FAILED:
            data.statusIcon = standalone::icons::STATUS_ERROR;
            data.visual = SequencerStepPresetActionVisual::CANCELLED;
            data.tone = SequencerStepPresetActionTone::DESTRUCTIVE;
            break;
        case Status::WARNING:
            data.statusIcon = standalone::icons::STATUS_WARNING;
            data.visual = SequencerStepPresetActionVisual::ACTIVE;
            data.tone = SequencerStepPresetActionTone::WARNING;
            break;
        case Status::PREVIEW:
            data.statusIcon = standalone::icons::STATUS_PREVIEW;
            data.visual = SequencerStepPresetActionVisual::ACTIVE;
            data.tone = SequencerStepPresetActionTone::CONSTRUCTIVE;
            break;
        case Status::NONE:
        default:
            return data;
    }
    data.showLabel = true;
    std::snprintf(
        data.label.data(),
        data.label.size(),
        "%s",
        shortOperationLabel(feedback.status)
    );
    return data;
}

}  // namespace core::ui::sequencer
