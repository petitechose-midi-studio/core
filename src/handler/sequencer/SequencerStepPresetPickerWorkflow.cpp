#include "handler/sequencer/SequencerStepPresetPickerWorkflow.hpp"

#include <cstring>

#include <config/App.hpp>
#include <oc/type/Result.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {

FLASHMEM SequencerStepPresetPickerWorkflow::SequencerStepPresetPickerWorkflow(
    core::state::sequencer::SequencerState& sequencer,
    SequencerStepPresetDomainServices& stepPresets,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
)
    : sequencer_(sequencer)
    , step_presets_(stepPresets)
    , overlays_(overlays) {}

FLASHMEM void SequencerStepPresetPickerWorkflow::refreshList() {
    auto& picker = sequencer_.stepPresetPicker;
    using Picker = core::state::sequencer::SequencerStepPresetPickerState;
    using Feedback = core::state::sequencer::SequencerStepPresetFeedback;

    SequencerStepPresetDomainServices::Entry entries[Picker::ENTRY_CAPACITY]{};
    const auto listed = step_presets_.listPresets(entries, Picker::ENTRY_CAPACITY);
    if (!listed.ok()) {
        picker.entryCount.set(0);
        picker.truncated.set(false);
        picker.setFeedback(Feedback::FAILED);
        return;
    }

    for (uint8_t i = 0; i < Picker::ENTRY_CAPACITY; ++i) {
        picker.setEntry(i, i < listed.count ? entries[i].id : nullptr);
    }
    picker.entryCount.set(listed.count);
    picker.truncated.set(listed.truncated);
    picker.clampSelection();
    picker.revision.set(picker.revision.get() + 1U);
}

FLASHMEM void SequencerStepPresetPickerWorkflow::open() {
    if (!sequencer_.stepEdit.visible.get()) return;

    sequencer_.stepEdit.contextHold.clear();
    sequencer_.stepEdit.localVariationEditActive.set(false);
    sequencer_.stepPresetPicker.open(
        core::state::sequencer::SequencerStepPresetPickerMode::LOAD
    );
    refreshList();
    overlays_.show(core::ui::OverlayType::SEQ_STEP_PRESET, true);
}

FLASHMEM void SequencerStepPresetPickerWorkflow::close() {
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::SEQ_STEP_PRESET);
    sequencer_.stepPresetPicker.reset();
}

FLASHMEM void SequencerStepPresetPickerWorkflow::move(float delta) {
    auto& picker = sequencer_.stepPresetPicker;
    if (!picker.visible.get() || !nav::hasTurnDelta(delta)) return;

    const int count = static_cast<int>(picker.itemCount());
    if (count <= 0) return;

    const int current = static_cast<int>(picker.selectedIndex.get());
    const int next = nav::nextWrappedIndex(delta, current, count);
    picker.selectedIndex.set(static_cast<uint8_t>(next));
    picker.feedback.set(core::state::sequencer::SequencerStepPresetFeedback::NONE);
}

FLASHMEM void SequencerStepPresetPickerWorkflow::toggleMode() {
    auto& picker = sequencer_.stepPresetPicker;
    using Mode = core::state::sequencer::SequencerStepPresetPickerMode;
    const auto next = picker.mode.get() == Mode::LOAD ? Mode::SAVE : Mode::LOAD;
    picker.mode.set(next);
    picker.selectedIndex.set(0);
    picker.feedback.set(core::state::sequencer::SequencerStepPresetFeedback::NONE);
    refreshList();
    picker.revision.set(picker.revision.get() + 1U);
}

FLASHMEM bool SequencerStepPresetPickerWorkflow::shouldCommitBeforeLoad() const {
    const auto& picker = sequencer_.stepPresetPicker;
    return picker.visible.get() &&
           picker.mode.get() == core::state::sequencer::SequencerStepPresetPickerMode::LOAD &&
           picker.entryCount.get() > 0;
}

FLASHMEM const char* SequencerStepPresetPickerWorkflow::selectedPresetId() const {
    const auto& picker = sequencer_.stepPresetPicker;
    const uint8_t entryIndex = picker.existingEntryIndexForSelectedItem();
    if (entryIndex >= picker.entryCount.get()) return "";
    return picker.entryId(entryIndex);
}

FLASHMEM SequencerStepPresetPickerOutcome SequencerStepPresetPickerWorkflow::execute() {
    auto& picker = sequencer_.stepPresetPicker;
    if (!picker.visible.get()) return SequencerStepPresetPickerOutcome::NONE;

    using Mode = core::state::sequencer::SequencerStepPresetPickerMode;
    using Feedback = core::state::sequencer::SequencerStepPresetFeedback;

    if (picker.mode.get() == Mode::SAVE) {
        char presetId[core::state::sequencer::SequencerStepPresetPickerState::ID_SIZE] = {};
        if (picker.selectedIndex.get() == 0) {
            const auto next = step_presets_.nextPresetId(presetId, sizeof(presetId));
            if (!next.ok()) {
                OC_LOG_WARN("[StepPreset] next id failed status={} file={}",
                            sequencerStepPresetStatusLabel(next.status),
                            oc::type::errorCodeToString(next.fileError));
                setFeedback(next);
                return SequencerStepPresetPickerOutcome::NONE;
            }
        } else {
            std::strncpy(presetId, selectedPresetId(), sizeof(presetId) - 1U);
            presetId[sizeof(presetId) - 1U] = '\0';
        }

        const auto result = step_presets_.savePreset(presetId);
        if (!result.ok()) {
            OC_LOG_WARN("[StepPreset] save id={} failed status={} asset={} file={} bytes={}",
                        result.presetId,
                        sequencerStepPresetStatusLabel(result.status),
                        core::state::sequencer::sequencerGraphAssetStatusLabel(result.assetStatus),
                        oc::type::errorCodeToString(result.fileError),
                        result.bytes);
            setFeedback(result);
            return SequencerStepPresetPickerOutcome::NONE;
        }
        picker.mode.set(Mode::LOAD);
        picker.selectedIndex.set(0);
        refreshList();
        const uint8_t count = picker.entryCount.get();
        for (uint8_t i = 0; i < count; ++i) {
            if (std::strcmp(picker.entryId(i), result.presetId) == 0) {
                picker.selectedIndex.set(i);
                break;
            }
        }
        picker.setFeedback(Feedback::SAVED);
        return SequencerStepPresetPickerOutcome::SAVED;
    }

    if (picker.entryCount.get() == 0) {
        picker.setFeedback(Feedback::EMPTY);
        return SequencerStepPresetPickerOutcome::LOAD_EMPTY;
    }

    const auto result = step_presets_.loadPreset(selectedPresetId());
    if (!result.ok()) {
        OC_LOG_WARN("[StepPreset] load id={} failed status={} asset={} file={} bytes={}",
                    result.presetId,
                    sequencerStepPresetStatusLabel(result.status),
                    core::state::sequencer::sequencerGraphAssetStatusLabel(result.assetStatus),
                    oc::type::errorCodeToString(result.fileError),
                    result.bytes);
        setFeedback(result);
        return SequencerStepPresetPickerOutcome::LOAD_FAILED;
    }

    return SequencerStepPresetPickerOutcome::LOADED;
}

FLASHMEM void SequencerStepPresetPickerWorkflow::setFeedback(
    const SequencerStepPresetActionResult& result
) {
    using Feedback = core::state::sequencer::SequencerStepPresetFeedback;

    if (result.status == SequencerStepPresetStatus::EMPTY) {
        sequencer_.stepPresetPicker.setFeedback(Feedback::EMPTY);
        return;
    }
    if (result.status == SequencerStepPresetStatus::INCOMPATIBLE ||
        result.assetStatus ==
            core::state::sequencer::SequencerGraphAssetStatus::INCOMPATIBLE_TARGET) {
        sequencer_.stepPresetPicker.setFeedback(Feedback::INCOMPATIBLE);
        return;
    }
    if (!result.ok()) {
        sequencer_.stepPresetPicker.setFeedback(Feedback::FAILED);
        return;
    }
    sequencer_.stepPresetPicker.setFeedback(Feedback::NONE);
}

}  // namespace core::handler
