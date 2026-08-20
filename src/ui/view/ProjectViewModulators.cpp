#include "ui/view/ProjectView.hpp"

#include <algorithm>
#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "ui/font/StandaloneIcons.hpp"
#include "ui/project/ProjectModulatorUiModel.hpp"
#include "ui/theme/StandaloneListVisuals.hpp"
#include "ui/view/RetainedViewRenderPolicy.hpp"

namespace core::ui {

namespace {

const char MODULATOR_SELECT_TRACK_TITLE[] PROGMEM = "Select track";
const char MODULATOR_SELECT_PAGE_TITLE[] PROGMEM = "Select page";
const char MODULATOR_SELECT_MACRO_TITLE[] PROGMEM = "Select macro";

}  // namespace

void ProjectView::populateModulatorRow(
    void* context,
    int index,
    ms::ui::KeyValueRowBuffer& out
) {
    auto* self = static_cast<ProjectView*>(context);
    if (!self) return;
    const auto node = self->state_refs_.navigation.currentNode.get();
    if (node == core::state::project::ProjectNodeId::MODULATORS_ROOT) {
        core::ui::project::modulators::populateRegistryRow(
            self->state_refs_.pages.control,
            index,
            out
        );
        return;
    }
    if (node ==
        core::state::project::ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER) {
        core::ui::project::modulators::populateSourceKindRow(index, out);
        return;
    }
    if (node ==
        core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        core::ui::project::modulators::populateDestinationPickerRow(
            self->state_refs_.pages,
            self->state_refs_.navigation,
            self->state_refs_.navigation.selectedModulator,
            index,
            out
        );
        return;
    }
    if (node == core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS) {
        core::ui::project::modulators::populateDestinationRow(
            self->state_refs_.pages,
            self->state_refs_.navigation.selectedModulator,
            index,
            out
        );
        return;
    }
    const auto* source = core::state::modulation::findProjectModulator(
        self->state_refs_.pages.control.authored.modulation,
        self->state_refs_.navigation.selectedModulator
    );
    if (source) {
        core::ui::project::modulators::populateSourceDetailRow(
            self->state_refs_.pages.control,
            *source,
            index,
            out
        );
    }
}

void ProjectView::renderModulators() {
    if (!modulator_registry_ || !modulator_workspace_) return;
    using core::state::project::ProjectNodeId;
    const auto node = state_refs_.navigation.currentNode.get();
    const auto& control = state_refs_.pages.control;
    const auto& graph = control.authored.modulation;
    const bool pickerCreating =
        node == ProjectNodeId::MODULATOR_DESTINATION_PICKER &&
        state_refs_.navigation.creatingModulatorSource;
    const bool sourceSelection =
        node == ProjectNodeId::MODULATORS_ROOT ||
        node == ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER ||
        pickerCreating;
    const auto* source = !sourceSelection
        ? core::state::modulation::findProjectModulator(
              graph,
              state_refs_.navigation.selectedModulator
          )
        : core::ui::project::modulators::sourceAtRegistryIndex(
              control,
              state_refs_.navigation.focusedRow.get()
          );

    const bool sourceWorkspace =
        node == ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        node == ProjectNodeId::MODULATOR_SOURCE_OPTIONS ||
        node == ProjectNodeId::MODULATOR_TRIGGER;
    const auto sourceSession = source != nullptr
        ? core::state::modulation::resolveProjectModulatorSourceSession(
              control,
              source->id
          )
        : core::state::modulation::
              ProjectModulatorSourceSessionDescriptor{};
    if (sourceWorkspace) {
        modulator_registry_->render({.visible = false});
        core::ui::project::ProjectModulatorWorkspaceProps workspaceProps{};
        if (source != nullptr && sourceSession.valid()) {
            workspaceProps = {
                .visible = true,
                .control = &control,
                .source = source,
                .auditionBinding = sourceSession.audition()
                    ? core::state::modulation::findProjectModulationBinding(
                          graph,
                          control.audition.bindingId
                      )
                    : nullptr,
                .session = sourceSession,
                .capture = &state_refs_.macroUi.recordedShapeCapture,
                .transientFeedback = sourceSession.existingAudition() &&
                    !state_refs_.navigation.lifecycleFeedback.empty()
                    ? state_refs_.navigation.lifecycleFeedback.get()
                    : nullptr,
                .options = node == ProjectNodeId::MODULATOR_SOURCE_OPTIONS,
                .trigger = node == ProjectNodeId::MODULATOR_TRIGGER,
                .selectedIndex = state_refs_.navigation.focusedRow.get(),
            };
        }
        modulator_workspace_->render(workspaceProps);
        renderModulatorActionStrips(workspaceProps.source);
        return;
    }
    modulator_workspace_->render({.visible = false});

    char meta[48]{};
    const auto guard = state_refs_.navigation.modulatorGuard.get();
    const bool deleting = source != nullptr &&
        state_refs_.navigation.guardedModulator == source->id &&
        guard.phase != core::state::contextual::GuardedActionPhase::IDLE &&
        guard.phase != core::state::contextual::GuardedActionPhase::CANCELLED;
    if (deleting && core::state::modulation::valid(
            state_refs_.navigation.guardedModulationBinding
        )) {
        const auto* binding = core::state::modulation::findProjectModulationBinding(
            graph,
            state_refs_.navigation.guardedModulationBinding
        );
        if (binding) {
            std::snprintf(
                meta,
                sizeof(meta),
                "Remove T%u · P%u · M%u",
                static_cast<unsigned>(binding->destination.track + 1U),
                static_cast<unsigned>(binding->destination.page + 1U),
                static_cast<unsigned>(binding->destination.macro + 1U)
            );
        }
    } else if (deleting) {
        const auto count = core::ui::project::modulators::sourceDestinationCount(
            graph,
            source->id
        );
        std::snprintf(
            meta,
            sizeof(meta),
            "Delete %s · %u destination%s",
            source->name.data(),
            static_cast<unsigned>(count),
            count == 1U ? "" : "s"
        );
    } else if (node == ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        using PickerLevel =
            core::state::project::ModulatorDestinationPickerLevel;
        const auto level = state_refs_.navigation.destinationPickerLevel;
        const char* sourceSuffix = state_refs_.navigation.creatingModulatorSource
            ? (state_refs_.navigation.creatingModulatorKind ==
                       core::state::modulation::ModulatorKind::ADSR
                   ? " · New DAHDSR"
                   : (state_refs_.navigation.creatingModulatorKind ==
                              core::state::modulation::ModulatorKind::RECORDED_SHAPE
                          ? " · New recorded shape"
                          : " · New LFO"))
            : "";
        if (level == PickerLevel::TRACK) {
            std::snprintf(meta, sizeof(meta), "Choose destination%s", sourceSuffix);
        } else if (level == PickerLevel::PAGE) {
            const uint8_t track = state_refs_.navigation.destinationPickerTrack;
            std::snprintf(
                meta,
                sizeof(meta),
                "T%u%s%s",
                static_cast<unsigned>(track + 1U),
                state_refs_.pages.isTrackEnabled(track) ? "" : " · Create",
                sourceSuffix
            );
        } else {
            const uint8_t track = state_refs_.navigation.destinationPickerTrack;
            const uint8_t page = state_refs_.navigation.destinationPickerPage;
            const bool pageExists = state_refs_.pages.isTrackEnabled(track) &&
                state_refs_.pages.tracks[track].isPageEnabled(page);
            std::snprintf(
                meta,
                sizeof(meta),
                "T%u · P%u%s%s",
                static_cast<unsigned>(track + 1U),
                static_cast<unsigned>(page + 1U),
                pageExists ? "" : " · Create",
                sourceSuffix
            );
        }
    } else if (node == ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER) {
        std::snprintf(meta, sizeof(meta), "Choose a modulation source");
    } else if (node == ProjectNodeId::MODULATOR_DESTINATIONS && source) {
        const auto count = core::ui::project::modulators::sourceDestinationCount(
            graph,
            source->id
        );
        std::snprintf(
            meta,
            sizeof(meta),
            count > 1U ? "Used by %u · Copy = Independent"
                       : "Used by %u · Independent",
            static_cast<unsigned>(count)
        );
    } else if (node != ProjectNodeId::MODULATORS_ROOT && source) {
        const auto count = core::ui::project::modulators::sourceDestinationCount(
            graph,
            source->id
        );
        std::snprintf(
            meta,
            sizeof(meta),
            "%s · x%u",
            source->kind == core::state::modulation::ModulatorKind::LFO
                ? "LFO"
                : (source->kind ==
                           core::state::modulation::ModulatorKind::ADSR
                       ? "DAHDSR"
                       : "Motion"),
            static_cast<unsigned>(count)
        );
    } else {
        std::snprintf(
            meta,
            sizeof(meta),
            "%u source%s",
            static_cast<unsigned>(graph.sourceCount),
            graph.sourceCount == 1U ? "" : "s"
        );
    }
    if (!deleting && !state_refs_.navigation.lifecycleFeedback.empty()) {
        std::snprintf(
            meta,
            sizeof(meta),
            "%s",
            state_refs_.navigation.lifecycleFeedback.get()
        );
    }

    int rowCount = 0;
    if (node == ProjectNodeId::MODULATORS_ROOT) {
        rowCount = static_cast<int>(graph.sourceCount) + 1;
    } else if (node == ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER) {
        rowCount = core::state::project::modulators::MODULATOR_SOURCE_KIND_COUNT;
    } else if (node == ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        rowCount = static_cast<int>(
            core::state::project::modulators::destinationPickerRowCount(
                state_refs_.pages,
                state_refs_.navigation
            )
        );
    } else if (node == ProjectNodeId::MODULATOR_DESTINATIONS && source) {
        rowCount = static_cast<int>(
            core::ui::project::modulators::sourceDestinationCount(
                graph,
                source->id
            )
        ) + 1;
    } else if (source) {
        rowCount = control.audition.active() && !sourceSession.valid()
            ? 0
            : static_cast<int>(
                  (sourceSession.audition()
                      ? core::ui::project::modulators::sourceAuditionLayout(
                            source->kind
                        )
                      : core::ui::project::modulators::sourceDetailLayout(
                            source->kind
                        )).count
              );
    }
    const char* title = source ? source->name.data() : "Source";
    if (node == ProjectNodeId::MODULATORS_ROOT) {
        title = "Modulators";
    } else if (node == ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER) {
        title = "Add source";
    } else if (node == ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        using PickerLevel =
            core::state::project::ModulatorDestinationPickerLevel;
        const auto level = state_refs_.navigation.destinationPickerLevel;
        title = level == PickerLevel::TRACK
            ? MODULATOR_SELECT_TRACK_TITLE
            : (level == PickerLevel::PAGE
                   ? MODULATOR_SELECT_PAGE_TITLE
                   : MODULATOR_SELECT_MACRO_TITLE);
    } else if (node == ProjectNodeId::MODULATOR_DESTINATIONS) {
        title = "Destinations";
    }
    modulator_registry_->render({
        .title = title,
        .meta = meta,
        .rowProvider = &ProjectView::populateModulatorRow,
        .rowProviderContext = this,
        .rowCount = rowCount,
        .selectedIndex = std::min<int>(
            state_refs_.navigation.focusedRow.get(),
            rowCount > 0 ? rowCount - 1 : 0
        ),
        .dimUnselected = false,
        .compactFacts = node == ProjectNodeId::MODULATORS_ROOT,
        .visible = true,
        .dataRevision = core::ui::project::modulators::registryRevision(
            control,
            state_refs_.navigation.focusedRow.get()
        ) ^ (static_cast<uint32_t>(node) << 16U) ^
            (static_cast<uint32_t>(
                 state_refs_.navigation.destinationPickerLevel
             ) << 12U) ^
            (static_cast<uint32_t>(
                 state_refs_.navigation.destinationPickerTrack
             ) << 8U) ^
            static_cast<uint32_t>(
                state_refs_.navigation.destinationPickerPage
            ),
        .visualTokens = &standalone::theme::CONTROLLER_LIST_VISUALS,
    });
    renderModulatorActionStrips(source);
}

void ProjectView::renderModulatorCapture() {
    if (!RetainedViewRenderPolicy::visible(container_) ||
        state_refs_.navigation.activeTab.get() !=
            core::state::project::ProjectTab::MODULATORS ||
        state_refs_.navigation.currentNode.get() !=
            core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL) {
        return;
    }
    renderModulators();
}

void ProjectView::renderModulatorActionStrips(
    const core::state::modulation::ModulatorSourceState* source
) {
    ContextActionStripProps left;
    ContextActionStripProps bottom;
    const bool auditioning = state_refs_.pages.control.audition.active();
    const bool detail = state_refs_.navigation.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        state_refs_.navigation.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS ||
        state_refs_.navigation.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS ||
        state_refs_.navigation.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER ||
        state_refs_.navigation.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_TRIGGER ||
        state_refs_.navigation.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER;
    const bool backAvailable = detail ||
        state_refs_.navigation.modulatorReturn.active();
    const char* leftIcon = auditioning
        ? standalone::icons::ACTION_CANCEL
        : backAvailable ? standalone::icons::ACTION_BACKWARD
                        : standalone::icons::ACTION_PLACE_TARGET;
    left.visible = true;
    left.slots[0] = makeStandaloneIconStripSlot(
        leftIcon,
        ContextActionStripVisualState::ACTIVE,
        auditioning
            ? ContextActionStripTone::WARNING
            : ContextActionStripTone::NEUTRAL
    );
    const bool destinations = state_refs_.navigation.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS;
    const bool destinationPicker = state_refs_.navigation.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER;
    const auto requestedSource = state_refs_.pages.control.audition.active()
        ? state_refs_.pages.control.audition.sourceId
        : (source != nullptr ? source->id
                             : core::state::modulation::ModulatorId{});
    const auto session = core::state::modulation::
        resolveProjectModulatorSourceSession(
            state_refs_.pages.control,
            requestedSource
        );
    const bool destinationAudition =
        session.audition() &&
        (destinationPicker ||
         state_refs_.navigation.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
         state_refs_.navigation.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS ||
         state_refs_.navigation.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_TRIGGER);
    const auto* binding = destinations && source
        ? core::state::project::modulators::sourceBindingAtOrdinal(
              state_refs_.pages.control.authored.modulation,
              source->id,
              state_refs_.navigation.focusedRow.get()
          )
        : nullptr;
    if (destinationAudition) {
        bottom.visible = true;
        bottom.slots[2] = makeStandaloneIconStripSlot(
            standalone::icons::STATUS_PREVIEW,
            ContextActionStripVisualState::ACTIVE,
            ContextActionStripTone::POSITIVE
        );
    } else if (!destinationPicker && source != nullptr &&
        (!destinations || binding != nullptr)) {
        bottom.visible = true;
        const bool recordedShapeRecordFocus =
            state_refs_.navigation.currentNode.get() ==
                core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL &&
            source->kind == core::state::modulation::ModulatorKind::RECORDED_SHAPE &&
            core::state::project::modulators::sourceDetailLayout(source->kind)
                    .at(state_refs_.navigation.focusedRow.get()) ==
                core::state::project::modulators::SourceDetailItem::RECORD;
        const bool enabled = destinations
            ? (binding->flags &
               core::state::modulation::PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U
            : (source->flags &
               core::state::modulation::PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
        bottom.slots[0] = recordedShapeRecordFocus
            ? makeStandaloneIconStripSlot(
                  standalone::icons::AUTOMATION,
                  state_refs_.macroUi.recordedShapeCapture.active() &&
                          state_refs_.macroUi.recordedShapeCapture.mode ==
                              core::state::modulation::
                                  ProjectRecordedShapeCaptureMode::
                                      REPLACE_EXISTING &&
                          state_refs_.macroUi.recordedShapeCapture.sourceId ==
                              source->id
                      ? ContextActionStripVisualState::PRESSED
                      : ContextActionStripVisualState::ACTIVE,
                  ContextActionStripTone::POSITIVE
              )
            : makeStandaloneIconStripSlot(
                  enabled ? standalone::icons::STATUS_RESUME
                          : standalone::icons::STATUS_PAUSED,
                  enabled ? ContextActionStripVisualState::ACTIVE
                          : ContextActionStripVisualState::DIM,
                  ContextActionStripTone::NEUTRAL
              );
        if (!destinations) {
            bottom.slots[2] = makeStandaloneIconStripSlot(
                standalone::icons::ACTION_COPY,
                ContextActionStripVisualState::ACTIVE,
                ContextActionStripTone::NEUTRAL
            );
        } else if (core::ui::project::modulators::sourceDestinationCount(
                       state_refs_.pages.control.authored.modulation,
                       source->id
                   ) > 1U) {
            bottom.slots[2] = makeStandaloneIconStripSlot(
                standalone::icons::ACTION_COPY,
                ContextActionStripVisualState::ACTIVE,
                ContextActionStripTone::POSITIVE
            );
        }

        const auto guard = state_refs_.navigation.modulatorGuard.get();
        const bool guardMatches =
            state_refs_.navigation.guardedModulator == source->id &&
            (!destinations ||
             state_refs_.navigation.guardedModulationBinding == binding->id);
        if (guardMatches && !recordedShapeRecordFocus) {
            if (guard.phase == core::state::contextual::GuardedActionPhase::PRESSED) {
                bottom.slots[0].visualState = ContextActionStripVisualState::PRESSED;
            } else if (
                guard.phase == core::state::contextual::GuardedActionPhase::ARMED ||
                guard.phase == core::state::contextual::GuardedActionPhase::COMMITTED
            ) {
                bottom.slots[0] = makeStandaloneIconStripSlot(
                    standalone::icons::ACTION_REMOVE,
                    ContextActionStripVisualState::ARMED,
                    ContextActionStripTone::DESTRUCTIVE
                );
                bottom.slots[0].holdActive = true;
                bottom.slots[0].holdStartedAtMs = guard.armedAtMs;
                bottom.slots[0].holdDurationMs = guard.guardDurationMs;
            }
        }
        const auto clipboardGuard =
            state_refs_.navigation.modulatorClipboardGuard.get();
        const bool clipboardGuardMatches =
            state_refs_.navigation.guardedClipboardModulator == source->id;
        if (clipboardGuardMatches) {
            if (clipboardGuard.phase ==
                core::state::contextual::GuardedActionPhase::PRESSED) {
                bottom.slots[2].visualState =
                    ContextActionStripVisualState::PRESSED;
            } else if (
                (clipboardGuard.phase ==
                     core::state::contextual::GuardedActionPhase::ARMED ||
                 clipboardGuard.phase ==
                     core::state::contextual::GuardedActionPhase::COMMITTED) &&
                state_refs_.navigation.modulatorClipboardPasteAvailable
            ) {
                bottom.slots[2] = makeStandaloneIconStripSlot(
                    standalone::icons::ACTION_PASTE,
                    ContextActionStripVisualState::ARMED,
                    ContextActionStripTone::POSITIVE
                );
                bottom.slots[2].holdActive = true;
                bottom.slots[2].holdStartedAtMs = clipboardGuard.armedAtMs;
                bottom.slots[2].holdDurationMs = clipboardGuard.guardDurationMs;
            }
        }
    }
    if (left_action_strip_) left_action_strip_->render(left);
    if (bottom_action_strip_) bottom_action_strip_->render(bottom);
}

}  // namespace core::ui
