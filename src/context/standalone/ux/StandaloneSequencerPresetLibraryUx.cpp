#include "context/standalone/ux/StandaloneSequencerPresetLibraryUx.hpp"
#include "context/standalone/ux/StandaloneUxSurfaces.hpp"

#if defined(MS_UX_RECORDER)

#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "config/InputIDs.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"
#include "state/sequencer/SequencerChordPresetModel.hpp"
#include "state/sequencer/SequencerPresetLibraryActionSpec.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerStepPresetModel.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerUiState.hpp"
#include "validation/ux/SemanticUxContext.hpp"

namespace core::context::standalone::ux {
namespace {

FLASHMEM bool isButton(
    const oc::core::input::InputBindingTraceEvent& event,
    Config::ButtonID button,
    oc::core::input::ButtonBindingType type
) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonId == static_cast<oc::type::ButtonID>(button) &&
           event.buttonType == type;
}

FLASHMEM bool isEncoder(
    const oc::core::input::InputBindingTraceEvent& event,
    Config::EncoderID encoder
) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Encoder &&
           event.encoderId == static_cast<oc::type::EncoderID>(encoder);
}

FLASHMEM void copyValueLabel(char (&out)[16], const char* value) {
    if (value == nullptr) return;
    size_t length = 0;
    while (length + 1U < sizeof(out) && value[length] != '\0') {
        out[length] = value[length];
        ++length;
    }
    out[length] = '\0';
}

FLASHMEM const char* chordPresetActionName(
    core::state::contextual::ContextActionId action
) {
    using Action = core::state::contextual::ContextActionId;
    switch (action) {
        case Action::SAVE: return "save_chord_preset";
        case Action::LOAD: return "load_chord_preset_into_editor";
        default: return nullptr;
    }
}

FLASHMEM const char* stepPresetActionName(
    core::state::contextual::ContextActionId action
) {
    using Action = core::state::contextual::ContextActionId;
    switch (action) {
        case Action::SAVE: return "save_step_preset";
        case Action::LOAD: return "load_step_preset_into_editor";
        default: return nullptr;
    }
}

FLASHMEM const char* stepPresetCompatibilityReasonName(
    core::state::sequencer::SequencerStepPresetCompatibility compatibility
) {
    using Compatibility =
        core::state::sequencer::SequencerStepPresetCompatibility;
    switch (compatibility) {
        case Compatibility::UNKNOWN: return "compatibility_unknown";
        case Compatibility::READY: return nullptr;
        case Compatibility::WARNING_ADAPTED: return "adapted";
        case Compatibility::BLOCKED_CONTEXT: return "wrong_context";
        case Compatibility::BLOCKED_PITCH_CONTEXT: return "pitch_context";
        case Compatibility::BLOCKED_CAPACITY: return "capacity";
        case Compatibility::CORRUPT: return "corrupt_asset";
        case Compatibility::UNSUPPORTED_VERSION:
            return "unsupported_version";
        case Compatibility::STORAGE_UNAVAILABLE:
            return "storage_unavailable";
        case Compatibility::STALE_TARGET: return "stale_target";
    }
    return "compatibility_unknown";
}

}  // namespace

namespace {

FLASHMEM bool captureSequencerChordPresetSemanticUxContext(
    const core::state::sequencer::SequencerPresetLibrarySessionState& picker,
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) {
    namespace seq = core::state::sequencer;
    namespace contextual = core::state::contextual;
    using ButtonType = oc::core::input::ButtonBindingType;
    const auto& chordLibrary = picker.chord();

    const auto feedback = picker.operationFeedback.get();
    const bool focusedAsset = picker.selectedItemIsExistingAsset();
    const bool feedbackIsSave = feedback.active &&
        feedback.action == contextual::ContextActionId::SAVE;
    const bool saveMode = feedbackIsSave ||
        (!feedback.active &&
         picker.mode.get() == seq::SequencerPresetLibraryMode::SAVE);
    out.mode = picker.detailVisible.get()
        ? (saveMode
               ? "sequencer.chord_preset.save.detail"
               : "sequencer.chord_preset.load.detail")
        : (saveMode
               ? "sequencer.chord_preset.save"
               : "sequencer.chord_preset.load");

    const auto spec = seq::buildSequencerPresetLibraryActionSpec(
        picker
    );
    const auto variant = contextual::hasHoldAction(spec)
        ? spec.hold
        : spec.tap;
    const auto action = feedback.active ? feedback.action : variant.action;

    out.target = "chord";
    out.targetIndex = static_cast<int16_t>(picker.selectedIndex.get());
    if (chordLibrary.target.valid) {
        out.targetStep =
            static_cast<int16_t>(chordLibrary.target.stepIndex);
    }
    out.routePolicy = "preserve_destination";
    out.projection = "draft_preview";
    if (chordLibrary.descriptor.valid &&
        chordLibrary.descriptor.technicalId[0] != '\0') {
        out.source = chordLibrary.descriptor.technicalId;
    } else if (focusedAsset) {
        out.source = picker.entryId(
            picker.existingEntryIndexForSelectedItem()
        );
    } else if (saveMode && picker.selectedItemIsNewAsset()) {
        out.source = "new_chord_preset";
    } else {
        out.source = "no_chord_preset";
    }

    if (picker.detailVisible.get() &&
        chordLibrary.descriptor.valid) {
        if (!seq::sequencerChordPresetCanApply(
                chordLibrary.descriptor.compatibility
            )) {
            switch (picker.detailFocus.get()) {
                case 0U:
                    out.property = "asset_id";
                    copyValueLabel(out.valueLabel, out.source);
                    break;
                case 1U:
                    out.property = "compatibility";
                    copyValueLabel(
                        out.valueLabel,
                        seq::sequencerChordPresetCompatibilityLabel(
                            chordLibrary.descriptor.compatibility
                        )
                    );
                    break;
                case 2U:
                default:
                    out.property = "load_availability";
                    copyValueLabel(out.valueLabel, "unavailable");
                    break;
            }
        } else {
            switch (picker.detailFocus.get()) {
                case 0U:
                    out.property = "projected_formula";
                    std::snprintf(
                        out.valueLabel,
                        sizeof(out.valueLabel),
                        "%u%s",
                        static_cast<unsigned>(
                            chordLibrary.descriptor.resolution.count
                        ),
                        chordLibrary.descriptor.targetBasis ==
                                oc::note::sequencer::
                                    StepSequencerChordIntervalBasis::
                                        ScaleDegrees
                            ? " DEG"
                            : " ST"
                    );
                    break;
                case 1U:
                    out.property = "transform";
                    std::snprintf(
                        out.valueLabel,
                        sizeof(out.valueLabel),
                        "inv %u",
                        static_cast<unsigned>(
                            chordLibrary.descriptor.projectedFormula.
                                inversion()
                        )
                    );
                    break;
                case 2U:
                default:
                    out.property = "resolved_output";
                    std::snprintf(
                        out.valueLabel,
                        sizeof(out.valueLabel),
                        "%u voices",
                        static_cast<unsigned>(
                            chordLibrary.descriptor.resolution.count
                        )
                    );
                    break;
            }
        }
    } else if (chordLibrary.descriptor.valid) {
        out.property = "asset_id";
        copyValueLabel(out.valueLabel, out.source);
    } else if (picker.selectedItemIsNewAsset()) {
        out.property = "new_asset";
        copyValueLabel(out.valueLabel, "unsaved");
    } else {
        out.property = "asset_id";
        copyValueLabel(out.valueLabel, out.source);
    }

    if (picker.inspecting.get()) {
        out.outcome = "pending";
        out.reason = "preset_inspection_pending";
    } else if (feedback.active) {
        out.outcome = sequencerUxOperationOutcomeName(feedback.status);
        out.reason = sequencerUxContextActionReasonName(feedback.reason);
    } else {
        out.outcome = sequencerUxGuardedActionOutcomeName(
            picker.actionGuard.get().phase
        );
        out.reason = sequencerUxContextActionReasonName(variant.reason);
        if (out.outcome == nullptr) {
            switch (variant.availability) {
                case contextual::ContextActionAvailability::AVAILABLE:
                    out.outcome = "ready";
                    break;
                case contextual::ContextActionAvailability::WARNING:
                    out.outcome = "warning";
                    break;
                case contextual::ContextActionAvailability::DISABLED:
                    out.outcome = "blocked";
                    break;
            }
        }
    }
    if (out.reason == nullptr &&
        chordLibrary.descriptor.valid) {
        out.reason = sequencerUxContextActionReasonName(
            seq::sequencerChordPresetCompatibilityReason(
                chordLibrary.descriptor.compatibility
            )
        );
    }
    out.hasConflict = true;
    out.conflict = feedback.active &&
        (feedback.status ==
             contextual::OperationFeedbackStatus::CONFLICT ||
         feedback.reason == contextual::ContextActionReason::CONFLICT);

    const bool actionGesture =
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, ButtonType::PRESS) ||
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, ButtonType::RELEASE) ||
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, ButtonType::LONG_PRESS);
    if (isEncoder(event, Config::EncoderID::NAV)) {
        if (picker.detailVisible.get()) {
            out.effect = "focus_preset_library_detail";
        } else {
            out.effect = "select_preset_library_asset";
        }
    } else if (isEncoder(event, Config::EncoderID::OPT)) {
        out.effect = "noop";
        out.outcome = "noop";
        out.reason = "preset_library_detail_has_no_adjustable_row";
    } else if (isButton(
                   event,
                   Config::ButtonID::NAV,
                   ButtonType::RELEASE
               )) {
        if (picker.detailVisible.get()) {
            out.effect = "noop";
            out.outcome = "noop";
            out.reason = "preset_library_detail_already_open";
        } else {
            out.effect = "enter_preset_library_detail";
        }
    } else if (isButton(
                   event,
                   Config::ButtonID::LEFT_TOP,
                   ButtonType::RELEASE
               )) {
        out.effect = picker.detailVisible.get()
            ? "back_to_preset_library_list"
            : "close_preset_library";
    } else if (isButton(
                   event,
                   Config::ButtonID::LEFT_CENTER,
                   ButtonType::RELEASE
               )) {
        out.effect = "noop";
        out.outcome = "noop";
        out.reason = "unbound_in_preset_library";
    } else if (isButton(
                   event,
                   Config::ButtonID::BOTTOM_LEFT,
                   ButtonType::RELEASE
               )) {
        out.effect = saveMode
            ? "show_preset_library_save_mode"
            : "show_preset_library_load_mode";
    } else if (actionGesture) {
        out.effect = chordPresetActionName(action);
    }
    return true;
}

}  // namespace

FLASHMEM
SequencerPresetLibraryUxSurface::SequencerPresetLibraryUxSurface(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackActivationQueue*
        trackActivations
) : sequencer_(sequencer),
    track_activations_(trackActivations) {}

FLASHMEM bool
SequencerPresetLibraryUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    namespace seq = core::state::sequencer;
    namespace contextual = core::state::contextual;

    const auto& picker = sequencer_.presetLibrary;
    if (!picker.visible.get()) return false;
    if (picker.libraryKind.get() ==
        seq::SequencerPresetLibraryKind::CHORD) {
        return captureSequencerChordPresetSemanticUxContext(
            picker,
            event,
            out
        );
    }
    const auto& stepLibrary = picker.step();

    const auto feedback = picker.operationFeedback.get();
    const bool focusedAsset = picker.selectedItemIsExistingAsset();
    const bool feedbackIsSave = feedback.active &&
        feedback.action == contextual::ContextActionId::SAVE;
    const bool saveMode = feedbackIsSave ||
        (!feedback.active &&
         picker.mode.get() == seq::SequencerPresetLibraryMode::SAVE);
    out.mode = picker.detailVisible.get()
        ? (saveMode
               ? "sequencer.step_preset.save.detail"
               : "sequencer.step_preset.load.detail")
        : (saveMode
               ? "sequencer.step_preset.save"
               : "sequencer.step_preset.load");

    const auto spec = seq::buildSequencerPresetLibraryActionSpec(
        picker
    );
    const auto variant = contextual::hasHoldAction(spec)
        ? spec.hold
        : spec.tap;
    const auto action = feedback.active
        ? feedback.action
        : variant.action;

    out.target = "step";
    out.targetIndex =
        static_cast<int16_t>(picker.selectedIndex.get());
    if (stepLibrary.target.valid) {
        out.targetStep =
            static_cast<int16_t>(stepLibrary.target.stepIndex);
    }
    // Presets carry musical content only; destination route ownership remains
    // with the frozen Step target.
    out.routePolicy = "preserve_destination";
    out.projection = picker.inspecting.get()
        ? "inspection_pending"
        : "preview";
    if (stepLibrary.descriptor.valid &&
        stepLibrary.descriptor.technicalId[0] != '\0') {
        out.source = stepLibrary.descriptor.technicalId;
    } else if (focusedAsset) {
        out.source = picker.entryId(
            picker.existingEntryIndexForSelectedItem()
        );
    } else if (saveMode && picker.selectedItemIsNewAsset()) {
        out.source = "new_step_preset";
    } else {
        out.source = "no_step_preset";
    }

    if (picker.detailVisible.get() &&
        stepLibrary.descriptor.valid) {
        switch (picker.detailFocus.get()) {
            case 0U:
                out.property = "target_context";
                copyValueLabel(
                    out.valueLabel,
                    stepLibrary.target.contextLabel
                );
                break;
            case 1U:
                out.property = "content";
                copyValueLabel(
                    out.valueLabel,
                    stepLibrary.descriptor.contentSummary
                );
                break;
            case 2U:
                out.property = "impact";
                copyValueLabel(
                    out.valueLabel,
                    stepLibrary.descriptor.footprint ==
                            seq::SequencerStepPresetFootprint::REPLACE
                        ? "replace"
                        : "add"
                );
                break;
            case 3U:
                out.property = "pitch_policy";
                copyValueLabel(
                    out.valueLabel,
                    stepLibrary.descriptor.scalePolicy ==
                            seq::SequencerStepPresetScalePolicy::
                                SCALE_RELATIVE
                        ? "scale_relative"
                        : "chromatic"
                );
                break;
            case 4U:
            default:
                out.property = "preview_state";
                std::snprintf(
                    out.valueLabel,
                    sizeof(out.valueLabel),
                    "%u/%u",
                    static_cast<unsigned>(
                        picker.previewStateIndex.get() + 1U
                    ),
                    static_cast<unsigned>(
                        stepLibrary.descriptor.previewStateCount > 0U
                            ? stepLibrary.descriptor.previewStateCount
                            : 1U
                    )
                );
                break;
        }
    } else if (stepLibrary.descriptor.valid) {
        out.property = "asset_id";
        copyValueLabel(out.valueLabel, out.source);
    } else if (picker.selectedItemIsNewAsset()) {
        out.property = "new_asset";
        copyValueLabel(out.valueLabel, "unsaved");
    } else {
        out.property = "asset_id";
        copyValueLabel(out.valueLabel, out.source);
    }

    if (picker.inspecting.get()) {
        out.outcome = "pending";
        out.reason = "preset_inspection_pending";
    } else if (feedback.active) {
        out.outcome = sequencerUxOperationOutcomeName(
            feedback.status
        );
        out.reason = sequencerUxContextActionReasonName(
            feedback.reason
        );
    } else {
        out.outcome = sequencerUxGuardedActionOutcomeName(
            picker.actionGuard.get().phase
        );
        out.reason = sequencerUxContextActionReasonName(
            variant.reason
        );
        if (out.outcome == nullptr) {
            switch (variant.availability) {
                case contextual::ContextActionAvailability::AVAILABLE:
                    out.outcome = "ready";
                    break;
                case contextual::ContextActionAvailability::WARNING:
                    out.outcome = "warning";
                    break;
                case contextual::ContextActionAvailability::DISABLED:
                    out.outcome = "blocked";
                    break;
            }
        }
    }
    if (feedback.active &&
        stepLibrary.activationGeneration != 0U &&
        stepLibrary.target.valid &&
        track_activations_ != nullptr) {
        using FeedbackStatus =
            contextual::OperationFeedbackStatus;
        using ActivationStatus =
            seq::SequencerTrackActivationStatus;
        const auto telemetry = track_activations_->telemetry(
            stepLibrary.target.trackIndex
        );
        const bool terminalMatches =
            (feedback.status == FeedbackStatus::QUEUED &&
             telemetry.status == ActivationStatus::QUEUED) ||
            (feedback.status == FeedbackStatus::APPLIED &&
             telemetry.status == ActivationStatus::APPLIED) ||
            (feedback.status == FeedbackStatus::CANCELLED &&
             telemetry.status == ActivationStatus::CANCELLED);
        if (terminalMatches &&
            telemetry.generation ==
                stepLibrary.activationGeneration &&
            telemetry.origin ==
                seq::SequencerTrackActivationOrigin::STEP_PRESET) {
            out.activationOrigin = "step_preset";
            out.hasActivationGeneration = true;
            out.activationGeneration = telemetry.generation;
        }
    }
    if (out.reason == nullptr &&
        stepLibrary.descriptor.valid) {
        out.reason = stepPresetCompatibilityReasonName(
            stepLibrary.descriptor.compatibility
        );
    }
    out.hasConflict = true;
    out.conflict = feedback.active &&
        (feedback.status ==
             contextual::OperationFeedbackStatus::CONFLICT ||
         feedback.reason ==
             contextual::ContextActionReason::CONFLICT);

    using ButtonType = oc::core::input::ButtonBindingType;
    const bool actionGesture =
        isButton(
            event,
            Config::ButtonID::BOTTOM_RIGHT,
            ButtonType::PRESS
        ) ||
        isButton(
            event,
            Config::ButtonID::BOTTOM_RIGHT,
            ButtonType::RELEASE
        ) ||
        isButton(
            event,
            Config::ButtonID::BOTTOM_RIGHT,
            ButtonType::LONG_PRESS
        );
    if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = picker.detailVisible.get()
            ? "focus_preset_library_detail"
            : "select_preset_library_asset";
    } else if (isEncoder(event, Config::EncoderID::OPT)) {
        if (picker.detailVisible.get() &&
            picker.detailFocus.get() == 4U) {
            out.effect = "select_preset_library_preview_state";
        } else {
            out.effect = "noop";
            out.outcome = "noop";
            out.reason = "preview_state_not_focused";
        }
    } else if (isButton(
                   event,
                   Config::ButtonID::NAV,
                   ButtonType::RELEASE
               )) {
        if (picker.detailVisible.get()) {
            out.effect = "noop";
            out.outcome = "noop";
            out.reason = "preset_library_detail_already_open";
        } else {
            out.effect = "enter_preset_library_detail";
        }
    } else if (isButton(
                   event,
                   Config::ButtonID::LEFT_TOP,
                   ButtonType::RELEASE
               )) {
        out.effect = picker.detailVisible.get()
            ? "back_to_preset_library_list"
            : "close_preset_library";
    } else if (isButton(
                   event,
                   Config::ButtonID::LEFT_CENTER,
                   ButtonType::RELEASE
               )) {
        out.effect = "noop";
        out.outcome = "noop";
        out.reason = "unbound_in_preset_library";
    } else if (isButton(
                   event,
                   Config::ButtonID::BOTTOM_LEFT,
                   ButtonType::RELEASE
               )) {
        out.effect = saveMode
            ? "show_preset_library_save_mode"
            : "show_preset_library_load_mode";
    } else if (actionGesture) {
        out.effect = stepPresetActionName(action);
    }
    return true;
}

}  // namespace core::context::standalone::ux

#endif
