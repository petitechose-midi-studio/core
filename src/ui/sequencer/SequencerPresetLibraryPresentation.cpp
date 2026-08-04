#include "ui/sequencer/SequencerPresetLibraryPresentation.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerPresetLibraryActionSpec.hpp"
#include "ui/sequencer/SequencerChordPresetPresentation.hpp"
#include "ui/sequencer/SequencerPresetLibraryPresentationCommon.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/strip/ContextActionVisualProjection.hpp"

namespace core::ui::sequencer {
namespace contextual = core::state::contextual;
using Picker =
    core::state::sequencer::SequencerPresetLibrarySessionState;
using Presentation = SequencerPresetLibraryPresentation;

namespace preset_library_presentation_common {

FLASHMEM uint32_t mixRevision(uint32_t seed, uint32_t value) {
    return (seed ^ value) * 16777619U;
}

FLASHMEM const char* feedbackLabel(
    core::state::sequencer::SequencerPresetLibraryFeedback feedback,
    const ListConfig& config
) {
    using Feedback =
        core::state::sequencer::SequencerPresetLibraryFeedback;
    switch (feedback) {
        case Feedback::SAVED: return "Saved";
        case Feedback::LOADED: return config.loadedFeedback;
        case Feedback::QUEUED: return config.queuedFeedback;
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
    const contextual::OperationFeedbackState& feedback,
    const ListConfig& config
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
        case Status::APPLIED:
            return feedback.action == contextual::ContextActionId::SAVE
                ? "Saved"
                : config.loadedFeedback;
        case Status::CANCELLED: return "Cancelled";
        case Status::BLOCKED: return "Blocked";
        case Status::WARNING: return config.warningFeedback;
        case Status::CONFLICT: return "Target changed";
        case Status::FAILED: return "Failed";
        case Status::PREVIEW: return "Preview";
        case Status::NONE:
        default: return "";
    }
}

FLASHMEM const char* shortOperationLabel(
    const contextual::OperationFeedbackState& feedback
) {
    using Status = contextual::OperationFeedbackStatus;
    switch (feedback.status) {
        case Status::PRESSED: return "Hold";
        case Status::ARMED: return "Armed";
        case Status::QUEUED: return "Queued";
        case Status::APPLIED:
            return feedback.action == contextual::ContextActionId::SAVE
                ? "Saved"
                : "Loaded";
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

FLASHMEM void formatList(
    Presentation& data,
    const Picker& picker,
    bool saveMode,
    const ListConfig& config
) {
    std::snprintf(
        data.title.data(),
        data.title.size(),
        "%s %s Preset",
        saveMode ? "Save" : "Load",
        config.kindLabel
    );

    int itemIndex = 0;
    if (picker.newAssetItemOffset() > 0U) {
        std::snprintf(
            data.itemBuffers[itemIndex].data(),
            data.itemBuffers[itemIndex].size(),
            "+  New %s Preset",
            config.kindLabel
        );
        data.items[itemIndex] = data.itemBuffers[itemIndex].data();
        ++itemIndex;
    }
    for (uint8_t index = 0;
         index < picker.entryCount.get() &&
         itemIndex < static_cast<int>(data.items.size());
         ++index) {
        const bool readable = picker.entryHasReadableMetadata(index) &&
            picker.entryName(index)[0] != '\0';
        if (readable && duplicateName(picker, index)) {
            std::snprintf(
                data.itemBuffers[itemIndex].data(),
                data.itemBuffers[itemIndex].size(),
                "%s  [%s]",
                picker.entryName(index),
                picker.entryId(index)
            );
        } else {
            std::snprintf(
                data.itemBuffers[itemIndex].data(),
                data.itemBuffers[itemIndex].size(),
                "%s",
                readable ? picker.entryName(index) : picker.entryId(index)
            );
        }
        data.items[itemIndex] = data.itemBuffers[itemIndex].data();
        ++itemIndex;
    }
    if (itemIndex == 0) {
        std::snprintf(
            data.itemBuffers[0].data(),
            data.itemBuffers[0].size(),
            "No %s Presets",
            config.kindLabel
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
    const char* operationText = operation.active
        ? operationLabel(operation, config)
        : "";
    const char* currentFeedback = feedbackLabel(
        picker.feedback.get(),
        config
    );
    if (operationText[0] != '\0') {
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "%s",
            operationText
        );
    } else if (currentFeedback[0] != '\0') {
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "%s",
            currentFeedback
        );
    } else if (picker.inspecting.get()) {
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "Inspecting preset..."
        );
    } else if (!saveMode && config.compatibility[0] != '\0') {
        const char* pagination = picker.hasPreviousPage.get()
            ? (picker.hasNextPage.get() ? "<  %s  >" : "<  %s")
            : (picker.hasNextPage.get() ? "%s  >" : "%s");
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            pagination,
            config.compatibility
        );
    } else {
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "%s",
            config.idleMeta
        );
    }
}

FLASHMEM uint32_t baseRevision(const Picker& picker) {
    uint32_t revision = picker.revision.get();
    revision = mixRevision(revision, picker.visible.get() ? 1U : 0U);
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(picker.libraryKind.get())
    );
    revision = mixRevision(revision, static_cast<uint32_t>(picker.mode.get()));
    revision = mixRevision(revision, picker.selectedIndex.get());
    revision = mixRevision(revision, picker.entryCount.get());
    revision = mixRevision(revision, picker.truncated.get() ? 1U : 0U);
    revision = mixRevision(revision, picker.hasPreviousPage.get() ? 1U : 0U);
    revision = mixRevision(revision, picker.hasNextPage.get() ? 1U : 0U);
    revision = mixRevision(revision, picker.totalEntryCount.get());
    revision = mixRevision(revision, picker.inspecting.get() ? 1U : 0U);
    revision = mixRevision(revision, picker.detailVisible.get() ? 1U : 0U);
    revision = mixRevision(revision, picker.detailFocus.get());
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(picker.feedback.get())
    );
    revision = mixRevision(
        revision,
        picker.operationFeedback.get().active ? 1U : 0U
    );
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(picker.operationFeedback.get().action)
    );
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(picker.operationFeedback.get().status)
    );
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(picker.operationFeedback.get().reason)
    );
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(picker.actionGuard.get().phase)
    );
    return revision;
}

}  // namespace preset_library_presentation_common

namespace {

FLASHMEM void formatDetail(Presentation& data, const Picker& picker) {
    const auto& step = picker.step();
    const auto& descriptor = step.descriptor;
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
        step.target.contextLabel
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
    if (descriptor.scalePolicy == core::state::sequencer::
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

}  // namespace

FLASHMEM SequencerPresetLibraryPresentation
buildSequencerPresetLibraryPresentation(
    const core::state::sequencer::SequencerState& sequencer
) {
    if (sequencer.presetLibrary.libraryKind.get() ==
        core::state::sequencer::SequencerPresetLibraryKind::CHORD) {
        return buildSequencerChordPresetPresentation(sequencer);
    }
    Presentation data{};
    const auto& picker = sequencer.presetLibrary;
    if (!picker.visible.get()) return data;
    const auto& step = picker.step();

    using Mode =
        core::state::sequencer::SequencerPresetLibraryMode;
    const bool saveMode = picker.mode.get() == Mode::SAVE;
    data.visible = true;
    if (picker.detailVisible.get() && step.descriptor.valid) {
        formatDetail(data, picker);
    } else {
        char idleMeta[48]{};
        std::snprintf(
            idleMeta,
            sizeof(idleMeta),
            "%s \xC2\xB7 %s",
            picker.truncated.get() ? "More" : "Target",
            step.target.contextLabel[0] != '\0'
                ? step.target.contextLabel
                : "Unavailable"
        );
        preset_library_presentation_common::formatList(
            data,
            picker,
            saveMode,
            {
                .kindLabel = "Step",
                .loadedFeedback = "Loaded into editor",
                .queuedFeedback = "Queued for loop boundary",
                .warningFeedback = "Check impact",
                .compatibility = step.descriptor.valid
                    ? core::state::sequencer::
                          sequencerStepPresetCompatibilityLabel(
                              step.descriptor.compatibility
                          )
                    : "",
                .idleMeta = idleMeta,
            }
        );
    }

    uint32_t revision =
        preset_library_presentation_common::baseRevision(picker);
    revision = preset_library_presentation_common::mixRevision(
        revision,
        static_cast<uint32_t>(step.descriptor.compatibility)
    );
    revision = preset_library_presentation_common::mixRevision(
        revision,
        step.descriptor.previewStateIndex
    );
    data.dataRevision = revision;
    return data;
}

FLASHMEM SequencerPresetLibraryActionPresentation
buildSequencerPresetLibraryActionPresentation(const Picker& picker) {
    SequencerPresetLibraryActionPresentation data{};
    const bool saveMode =
        picker.mode.get() ==
        core::state::sequencer::SequencerPresetLibraryMode::SAVE;
    const auto action = core::state::sequencer::
        buildSequencerPresetLibraryActionSpec(picker);
    const auto guard = picker.actionGuard.get();
    const auto feedback = picker.operationFeedback.get();
    namespace action_visual =
        core::ui::context_action_visual_projection;
    const auto& variant = action_visual::visibleVariant(
        action,
        guard,
        feedback
    );
    const auto visualPolicy = action_visual::projectedVisualPolicy(
        variant,
        feedback
    );
    data.saveIcon = saveMode;
    data.overwriteIcon = contextual::hasHoldAction(action);
    data.visual = action_visual::stripVisual(
        variant,
        guard,
        feedback
    );
    data.tone = action_visual::stripTone(visualPolicy.tone);

    using Guard = contextual::GuardedActionPhase;
    const bool holdFeedbackMatches =
        contextual::hasHoldAction(action) &&
        (!feedback.active || feedback.action == action.hold.action);
    data.holdActive = holdFeedbackMatches &&
        (guard.phase == Guard::PRESSED ||
         guard.phase == Guard::ARMED);
    data.holdStartedAtMs = guard.pressedAtMs;
    data.holdDurationMs = action.guard.durationMs;

    if (!feedback.active ||
        feedback.status ==
            contextual::OperationFeedbackStatus::NONE) {
        return data;
    }
    data.statusIcon = action_visual::feedbackIconGlyph(
        feedback,
        visualPolicy.icon
    );
    data.showLabel = true;
    std::snprintf(
        data.label.data(),
        data.label.size(),
        "%s",
        preset_library_presentation_common::shortOperationLabel(feedback)
    );
    return data;
}

}  // namespace core::ui::sequencer
