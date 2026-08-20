#pragma once

#include <cstring>

#include "state/sequencer/SequencerUiState.hpp"

namespace core::state::sequencer {

inline contextual::ContextActionSpec buildSequencerPresetLibraryActionSpec(
    const SequencerPresetLibrarySessionState& picker
) {
    const bool saveMode =
        picker.mode.get() == SequencerPresetLibraryMode::SAVE;
    const bool selectedNewAsset = picker.selectedItemIsNewAsset();
    const bool focusedAsset = picker.selectedItemIsExistingAsset() &&
        picker.entryKind(picker.existingEntryIndexForSelectedItem()) ==
            SequencerPresetLibraryEntryKind::ASSET;
    const bool focusedFolder = picker.selectedItemIsExistingAsset() &&
        picker.entryKind(picker.existingEntryIndexForSelectedItem()) ==
            SequencerPresetLibraryEntryKind::FOLDER;

    if (picker.libraryKind.get() == SequencerPresetLibraryKind::CHORD) {
        return buildSequencerChordPresetActionSpec(
            saveMode,
            selectedNewAsset,
            focusedAsset,
            picker.chord().target,
            picker.chord().descriptor
        );
    }
    if (picker.libraryKind.get() == SequencerPresetLibraryKind::PATTERN) {
        const auto& pattern = picker.pattern();
        contextual::ContextActionSpec spec{};
        spec.scope = contextual::ContextScope::PATTERN;
        spec.target.kind = contextual::ContextEntityKind::ASSET;

        if (pattern.textEdit != SequencerPatternPresetTextEdit::NONE ||
            picker.selectedItemIsNewFolder()) {
            const bool editing = pattern.textEdit !=
                SequencerPatternPresetTextEdit::NONE;
            const bool renamingAsset = pattern.textEdit ==
                    SequencerPatternPresetTextEdit::RENAME &&
                pattern.managedEntryKind ==
                    SequencerPresetLibraryEntryKind::ASSET;
            const bool valid = editing && (renamingAsset
                ? validSequencerPresetSemanticName(pattern.textDraft.data())
                : sequencerPatternPresetFolderNameIsValid(
                      pattern.textDraft.data()
                  ));
            const bool unchanged = pattern.textEdit ==
                    SequencerPatternPresetTextEdit::RENAME &&
                std::strcmp(
                    pattern.textDraft.data(),
                    pattern.managedEntryName.data()
                ) == 0;
            spec.tap = {
                .action = pattern.textEdit ==
                        SequencerPatternPresetTextEdit::RENAME
                    ? contextual::ContextActionId::RENAME
                    : contextual::ContextActionId::CREATE,
                .impact = contextual::ContextActionImpact::CONSTRUCTIVE,
                .availability = (!editing || valid) && !unchanged
                    ? contextual::ContextActionAvailability::AVAILABLE
                    : contextual::ContextActionAvailability::DISABLED,
                .reason = unchanged
                    ? contextual::ContextActionReason::SAME_SOURCE_TARGET
                    : (valid
                        ? contextual::ContextActionReason::NONE
                        : contextual::ContextActionReason::INVALID_PAYLOAD),
                .visual = {
                    .icon = pattern.textEdit ==
                            SequencerPatternPresetTextEdit::RENAME
                        ? contextual::ContextIconId::RENAME
                        : contextual::ContextIconId::CREATE,
                    .tone = contextual::ContextTone::AMBER,
                },
            };
            return spec;
        }

        if (pattern.panel == SequencerPatternPresetLibraryPanel::MANAGE) {
            switch (pattern.managementAction) {
                case SequencerPatternPresetManagementAction::RENAME:
                    spec.tap = {
                        .action = contextual::ContextActionId::RENAME,
                        .impact = contextual::ContextActionImpact::VALUE_EDIT,
                        .availability = contextual::
                            ContextActionAvailability::AVAILABLE,
                        .reason = contextual::ContextActionReason::NONE,
                        .visual = {
                            .icon = contextual::ContextIconId::RENAME,
                            .tone = contextual::ContextTone::AMBER,
                        },
                    };
                    break;
                case SequencerPatternPresetManagementAction::MOVE:
                    spec.tap = {
                        .action = contextual::ContextActionId::MOVE,
                        .impact = contextual::ContextActionImpact::VALUE_EDIT,
                        .availability = contextual::
                            ContextActionAvailability::AVAILABLE,
                        .reason = contextual::ContextActionReason::NONE,
                        .visual = {
                            .icon = contextual::ContextIconId::MOVE,
                            .tone = contextual::ContextTone::BLUE,
                        },
                    };
                    break;
                case SequencerPatternPresetManagementAction::DELETE:
                    spec.hold = {
                        .action = contextual::ContextActionId::DELETE_ASSET,
                        .impact = contextual::ContextActionImpact::DESTRUCTIVE,
                        .availability = contextual::
                            ContextActionAvailability::WARNING,
                        .reason = contextual::ContextActionReason::NONE,
                        .visual = {
                            .icon = contextual::ContextIconId::REMOVE,
                            .tone = contextual::ContextTone::RED,
                        },
                    };
                    spec.guard = {
                        .kind = contextual::ContextGuardKind::HOLD,
                        .durationMs =
                            SequencerPatternPresetLibraryState::
                                DELETE_GUARD_MS,
                    };
                    break;
                case SequencerPatternPresetManagementAction::COUNT:
                default:
                    break;
            }
            return spec;
        }

        if (pattern.panel ==
            SequencerPatternPresetLibraryPanel::MOVE_DESTINATION) {
            const bool moveHere = picker.selectedIndex.get() == 0U;
            const bool unchanged = std::strcmp(
                pattern.location.relativeDirectory.data(),
                pattern.managedLocation.relativeDirectory.data()
            ) == 0;
            spec.tap = {
                .action = contextual::ContextActionId::MOVE,
                .impact = contextual::ContextActionImpact::VALUE_EDIT,
                .availability = moveHere && !unchanged
                    ? contextual::ContextActionAvailability::AVAILABLE
                    : contextual::ContextActionAvailability::DISABLED,
                .reason = unchanged
                    ? contextual::ContextActionReason::SAME_SOURCE_TARGET
                    : (moveHere
                        ? contextual::ContextActionReason::NONE
                        : contextual::ContextActionReason::NO_ACTION),
                .visual = {
                    .icon = contextual::ContextIconId::MOVE,
                    .tone = contextual::ContextTone::BLUE,
                },
            };
            return spec;
        }
        if (focusedFolder) {
            spec.tap = {
                .action = contextual::ContextActionId::ENTER,
                .impact = contextual::ContextActionImpact::NON_MUTATING,
                .availability =
                    contextual::ContextActionAvailability::AVAILABLE,
                .reason = contextual::ContextActionReason::NONE,
                .visual = {
                    .icon = contextual::ContextIconId::ENTER,
                    .tone = contextual::ContextTone::AMBER,
                },
            };
            return spec;
        }
        if (pattern.factoryCopyPending) {
            spec.tap = {
                .action = contextual::ContextActionId::SAVE,
                .impact = contextual::ContextActionImpact::CONSTRUCTIVE,
                .availability = selectedNewAsset
                    ? contextual::ContextActionAvailability::AVAILABLE
                    : contextual::ContextActionAvailability::DISABLED,
                .reason = selectedNewAsset
                    ? contextual::ContextActionReason::NONE
                    : contextual::ContextActionReason::NO_ACTION,
                .visual = {
                    .icon = contextual::ContextIconId::SAVE,
                    .tone = contextual::ContextTone::GREEN,
                },
            };
            return spec;
        }
        return buildSequencerPatternPresetActionSpec(
            saveMode,
            selectedNewAsset,
            focusedAsset,
            picker.pattern().target,
            picker.pattern().descriptor
        );
    }
    return buildSequencerStepPresetActionSpec(
        saveMode,
        selectedNewAsset,
        focusedAsset,
        picker.step().target,
        picker.step().descriptor
    );
}

}  // namespace core::state::sequencer
