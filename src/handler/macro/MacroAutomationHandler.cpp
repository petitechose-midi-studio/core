#include "handler/macro/MacroAutomationHandler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/ModulatorNavigationWorkflow.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/macro/MacroAutomationEditorModel.hpp"
#include "handler/macro/MacroGuardedActionWorkflow.hpp"
#include "state/contextual/OperationFeedbackState.hpp"
#include "ui/macro/MacroSourceDetailLayout.hpp"
#include "ui/macro/MacroLfoAuditionModel.hpp"
#include "ui/modulation/ModulatorAdsrUiModel.hpp"

namespace core::handler {

namespace {

namespace detail_ui = core::ui::macro;
namespace adsr_ui = core::ui::modulation::adsr;

detail_ui::MacroSourceDetailContext detailContext(
    const MacroEditDomainServices& services,
    uint8_t macroIndex
) {
    const auto* slot = services.automationSlot(macroIndex);
    if (slot == nullptr) return {};
    return {
        .automationStored =
            core::state::macro::macroCurveStored(slot->automation),
        .modulationStored = services.modulationStoredFor(macroIndex),
        .automationPlayback =
            core::state::macro::macroCurvePlaybackActive(slot->automation),
        .modulationPlayback = services.modulationPlaybackActiveFor(macroIndex),
        .manualOverride = services.manualOverrideActiveFor(macroIndex),
    };
}

FLASHMEM const core::state::modulation::ModulatorSourceState* auditionSource(
    const core::state::macro::MacroPagesState& pages
) {
    if (!pages.control.audition.active) return nullptr;
    return core::state::modulation::findProjectModulator(
        pages.control.authored.modulation,
        pages.control.audition.sourceId
    );
}

FLASHMEM const core::state::modulation::ModulationBindingState* auditionBinding(
    const core::state::macro::MacroPagesState& pages
) {
    if (!pages.control.audition.active) return nullptr;
    const auto& graph = pages.control.authored.modulation;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].id == pages.control.audition.bindingId) {
            return &graph.outputBindings[index];
        }
    }
    return nullptr;
}

bool contextActionInProgress(const core::state::MacroEditState& state) {
    const auto phase = state.contextGuard.get().phase;
    return phase == core::state::contextual::GuardedActionPhase::PRESSED ||
           phase == core::state::contextual::GuardedActionPhase::ARMED ||
           phase == core::state::contextual::GuardedActionPhase::COMMITTED;
}

void cancelContextAction(
    core::state::MacroEditState& state,
    uint32_t nowMs,
    bool keepFeedback
) {
    if (!contextActionInProgress(state)) return;
    macro::MacroGuardedActionWorkflow::cancel(state, nowMs);
    if (!keepFeedback) {
        state.contextGuard.set({});
        state.contextFeedback.set({});
        state.contextButton.set(core::state::MacroContextButton::NONE);
    }
}

core::state::contextual::ContextEntityRef slotRef(
    const core::state::macro::MacroPagesState& pages,
    uint8_t macroIndex
) {
    return {
        .kind = core::state::contextual::ContextEntityKind::MACRO_SLOT,
        .track = pages.currentActiveTrack(),
        .page = pages.currentActivePage(),
        .item = macroIndex,
    };
}

core::state::contextual::ContextEntityRef sourceRef(
    const core::state::macro::MacroPagesState& pages,
    uint8_t macroIndex,
    bool modulation
) {
    auto ref = slotRef(pages, macroIndex);
    ref.kind = modulation
        ? core::state::contextual::ContextEntityKind::MODULATION_LANE
        : core::state::contextual::ContextEntityKind::AUTOMATION_LANE;
    return ref;
}

struct ModulationAssignmentRows {
    core::state::modulation::ModulationDestination destination{};
    uint16_t assignmentCount = 0;

    [[nodiscard]] int rowCount() const {
        if (assignmentCount == 0U) return 2;
        return static_cast<int>(assignmentCount) + 2;
    }

    [[nodiscard]] int firstAssignmentRow() const {
        return assignmentCount > 0U ? 1 : 0;
    }

    [[nodiscard]] int addSourceRow() const {
        return assignmentCount == 0U
            ? 1
            : firstAssignmentRow() + static_cast<int>(assignmentCount);
    }

    [[nodiscard]] bool allRow(int row) const {
        return assignmentCount > 0U && row == 0;
    }

    [[nodiscard]] bool addRow(int row) const {
        return row == addSourceRow();
    }

    [[nodiscard]] int assignmentOrdinal(int row) const {
        const int ordinal = row - firstAssignmentRow();
        return ordinal >= 0 && ordinal < static_cast<int>(assignmentCount)
            ? ordinal
            : -1;
    }
};

FLASHMEM ModulationAssignmentRows modulationRows(
    const core::state::macro::MacroPagesState& pages,
    uint8_t macroIndex
) {
    ModulationAssignmentRows rows{};
    rows.destination = core::state::modulation::projectControlDestination({
        .track = pages.currentActiveTrack(),
        .page = pages.currentActivePage(),
        .macro = macroIndex,
    });
    const auto& graph = pages.control.authored.modulation;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].destination == rows.destination) {
            ++rows.assignmentCount;
        }
    }
    return rows;
}

FLASHMEM const core::state::modulation::ModulationBindingState*
bindingForAssignmentOrdinal(
    const core::state::macro::MacroPagesState& pages,
    const ModulationAssignmentRows& rows,
    int ordinal
) {
    if (ordinal < 0) return nullptr;
    const auto& graph = pages.control.authored.modulation;
    int found = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != rows.destination) continue;
        if (found++ == ordinal) return &binding;
    }
    return nullptr;
}

FLASHMEM int rowForBinding(
    const core::state::macro::MacroPagesState& pages,
    const ModulationAssignmentRows& rows,
    core::state::modulation::ModulationBindingId bindingId
) {
    const auto& graph = pages.control.authored.modulation;
    int ordinal = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != rows.destination) continue;
        if (binding.id == bindingId) {
            return rows.firstAssignmentRow() + ordinal;
        }
        ++ordinal;
    }
    return rows.assignmentCount > 0U ? rows.firstAssignmentRow()
                                     : rows.addSourceRow();
}

}  // namespace

FLASHMEM MacroAutomationHandler::MacroAutomationHandler(
    StateRefs state,
    MacroEditDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID automationScope,
    NowProvider nowProvider
)
    : overlays_state_(state.overlays)
    , active_view_(state.activeView)
    , project_navigation_(state.projectNavigation)
    , macro_edit_(state.macroEdit)
    , pages_(state.pages)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , automation_scope_(automationScope)
    , now_provider_(nowProvider)
    , macro_view_was_active_(
          state.activeView.get() == core::ui::ViewType::MACRO
      ) {
    setupBindings();
}

FLASHMEM void MacroAutomationHandler::setupBindings() {
    using ButtonID = Config::ButtonID;
    using EncoderID = Config::EncoderID;

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(automation_scope_)
        .then([this](float delta) {
            if (conversionPreviewActive()) selectConversionPolicy(delta);
            else moveFocus(delta);
        });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(automation_scope_)
        .then([this]() { activateFocusedRow(); });

    encoders_.encoder(EncoderID::OPT)
        .turn()
        .scope(automation_scope_)
        .then([this](float normalized) { editFocusedValue(normalized); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(automation_scope_)
        .then([this]() { backToMacroEdit(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .scope(automation_scope_)
        .then([this]() { setCoarseEditActive(true); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(automation_scope_)
        .then([this]() { setCoarseEditActive(false); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .press()
        .scope(automation_scope_)
        .then([this]() { beginBottomLeftAction(); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .release()
        .scope(automation_scope_)
        .then([this]() { releaseBottomLeftAction(); });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(automation_scope_)
        .then([this]() { beginBottomRightAction(); });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(automation_scope_)
        .then([this]() { releaseBottomRightAction(); });
}

bool MacroAutomationHandler::active() const {
    return macro_edit_.visible.get() &&
           (automationDetailActive() || modulationDetailActive() ||
             conversionPreviewActive() || modulatorCreateActive() ||
             modulatorPickerActive() ||
             modulatorAuditionActive());
}

bool MacroAutomationHandler::automationDetailActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::AUTOMATION;
}

bool MacroAutomationHandler::modulationDetailActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATION;
}

bool MacroAutomationHandler::conversionPreviewActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::CONVERT_PREVIEW;
}

bool MacroAutomationHandler::newModulatorAuditionActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::NEW_MODULATOR_AUDITION;
}

bool MacroAutomationHandler::modulatorCreateActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATOR_CREATE;
}

bool MacroAutomationHandler::modulatorPickerActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::MODULATOR_PICKER;
}

bool MacroAutomationHandler::existingModulatorAuditionActive() const {
    return macro_edit_.flowPhase.get() ==
           core::state::MacroEditFlowPhase::EXISTING_MODULATOR_AUDITION;
}

bool MacroAutomationHandler::modulatorAuditionActive() const {
    return newModulatorAuditionActive() || existingModulatorAuditionActive();
}

uint8_t MacroAutomationHandler::macroIndex() const {
    return macro_edit_.editingIndex.get();
}

FLASHMEM void MacroAutomationHandler::moveFocus(float delta) {
    if (!active() || !nav::hasTurnDelta(delta)) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, true
    );
    services_.endDepthGesture();
    if (modulatorPickerActive()) {
        const int count = static_cast<int>(
            pages_.control.authored.modulation.sourceCount
        );
        if (count <= 0) return;
        const int current = macro_edit_.modulatorPickerIndex.get();
        macro_edit_.modulatorPickerIndex.set(
            nav::nextWrappedIndex(delta, current, count)
        );
        configureOptForFocusedRow();
        return;
    }
    auto& focus = (modulationDetailActive() || modulatorCreateActive() ||
                   modulatorAuditionActive())
        ? macro_edit_.modulationFocusedRow
        : macro_edit_.automationFocusedRow;
    const int current = static_cast<int>(focus.get());
    const auto context = detailContext(services_, macroIndex());
    int count = 0;
    if (modulatorCreateActive()) {
        count = 3;
    } else if (newModulatorAuditionActive()) {
        const auto* source = auditionSource(pages_);
        count = source != nullptr && source->kind ==
                core::state::modulation::ModulatorKind::ADSR
            ? adsr_ui::AUDITION_ITEM_COUNT
            : 3;
    } else if (existingModulatorAuditionActive()) {
        count = 2;
    } else if (modulationDetailActive() && !context.modulationStored) {
        count = 3;
    } else if (modulationDetailActive()) {
        count = modulationRows(pages_, macroIndex()).rowCount();
    } else {
        count = static_cast<int>(
            detail_ui::buildAutomationDetailLayout(context).count
        );
    }
    const int next = nav::nextWrappedIndex(delta, current, count);
    focus.set(static_cast<uint8_t>(next));
    if (modulationDetailActive() && context.modulationStored) {
        const auto rows = modulationRows(pages_, macroIndex());
        const auto* binding = bindingForAssignmentOrdinal(
            pages_,
            rows,
            rows.assignmentOrdinal(next)
        );
        if (binding != nullptr) {
            (void)services_.focusModulationBinding(
                macroIndex(),
                binding->id
            );
        }
    }
    configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::editFocusedValue(float normalized) {
    if (!active()) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, true
    );
    const uint8_t index = macroIndex();
    const float clamped = std::clamp(normalized, 0.0f, 1.0f);
    if (modulatorPickerActive() || modulatorCreateActive()) return;
    if (modulatorAuditionActive()) {
        const auto* source = auditionSource(pages_);
        const auto* binding = auditionBinding(pages_);
        if (source == nullptr || binding == nullptr) return;
        if (existingModulatorAuditionActive()) {
            if (macro_edit_.modulationFocusedRow.get() != 1U) return;
            const int16_t percent = static_cast<int16_t>(
                clamped * 200.0f + 0.5f
            ) - 100;
            (void)services_.setModulatorAuditionDepthQ15(
                index,
                detail_ui::lfo_audition::depthPercentToQ15(percent)
            );
            return;
        }
        if (source->kind == core::state::modulation::ModulatorKind::ADSR) {
            const auto row = static_cast<adsr_ui::AuditionItem>(
                std::min<uint8_t>(
                    macro_edit_.modulationFocusedRow.get(),
                    adsr_ui::AUDITION_ITEM_COUNT - 1U
                )
            );
            auto parameters = source->parameters.adsr;
            const uint8_t durationIndex = static_cast<uint8_t>(
                clamped * static_cast<float>(adsr_ui::DURATION_COUNT - 1U) +
                0.5f
            );
            switch (row) {
                case adsr_ui::AuditionItem::ATTACK:
                    parameters.attack = adsr_ui::durationAt(
                        durationIndex, parameters.timing
                    );
                    break;
                case adsr_ui::AuditionItem::DECAY:
                    parameters.decay = adsr_ui::durationAt(
                        durationIndex, parameters.timing
                    );
                    break;
                case adsr_ui::AuditionItem::SUSTAIN:
                    parameters.sustainQ15 = adsr_ui::sustainPercentToQ15(
                        static_cast<uint8_t>(clamped * 100.0f + 0.5f)
                    );
                    break;
                case adsr_ui::AuditionItem::RELEASE:
                    parameters.release = adsr_ui::durationAt(
                        durationIndex, parameters.timing
                    );
                    break;
                case adsr_ui::AuditionItem::DEPTH: {
                    const int16_t percent = static_cast<int16_t>(
                        clamped * 200.0f + 0.5f
                    ) - 100;
                    (void)services_.setModulatorAuditionDepthQ15(
                        index,
                        detail_ui::lfo_audition::depthPercentToQ15(percent)
                    );
                    return;
                }
            }
            (void)services_.setAdsrAuditionParameters(index, parameters);
            return;
        }
        const uint8_t row = std::min<uint8_t>(
            macro_edit_.modulationFocusedRow.get(),
            2U
        );
        if (row == 0U) {
            const auto shape = static_cast<
                core::state::modulation::ModulatorLfoShape>(
                    static_cast<uint8_t>(
                        clamped * static_cast<float>(
                            detail_ui::lfo_audition::SHAPE_COUNT - 1U
                        ) + 0.5f
                    )
                );
            (void)services_.setLfoAuditionShape(index, shape);
        } else if (row == 1U) {
            const uint8_t rate = static_cast<uint8_t>(
                clamped * static_cast<float>(
                    detail_ui::lfo_audition::RATE_COUNT - 1U
                ) + 0.5f
            );
            (void)services_.setLfoAuditionPeriodTicks(
                index,
                detail_ui::lfo_audition::ratePeriodTicks(rate)
            );
        } else {
            const int16_t percent = static_cast<int16_t>(
                clamped * 200.0f + 0.5f
            ) - 100;
            (void)services_.setModulatorAuditionDepthQ15(
                index,
                detail_ui::lfo_audition::depthPercentToQ15(percent)
            );
        }
        return;
    }
    const auto context = detailContext(services_, index);
    if (modulationDetailActive()) {
        if (!context.modulationStored) return;
        const auto rows = modulationRows(pages_, index);
        if (rows.allRow(macro_edit_.modulationFocusedRow.get())) {
            const auto scaled = static_cast<uint16_t>(std::lround(
                clamped * static_cast<float>(
                    std::numeric_limits<uint16_t>::max()
                )
            ));
            (void)services_.setModulationGlobalDepthQ15(index, scaled);
            return;
        }
        const auto* binding = bindingForAssignmentOrdinal(
            pages_,
            rows,
            rows.assignmentOrdinal(macro_edit_.modulationFocusedRow.get())
        );
        if (binding == nullptr) return;
        (void)services_.focusModulationBinding(index, binding->id);
        (void)services_.setModulationDepth(index, clamped * 2.0f - 1.0f);
        return;
    }
    if (!automationDetailActive()) return;
    const auto layout = detail_ui::buildAutomationDetailLayout(context);
    const auto item = layout.at(macro_edit_.automationFocusedRow.get());
    if (item == detail_ui::AutomationDetailItem::PLAYBACK) {
        (void)services_.setAutomationPlayback(index, clamped >= 0.5f);
        return;
    }
    if (item == detail_ui::AutomationDetailItem::LENGTH) {
        const auto range = macroAutomationLengthEditRange(coarse_edit_active_);
        services_.setAutomationDurationBeats(
            index,
            macroAutomationEncoderPositionToBeat(clamped, range)
        );
        return;
    }
    if (item == detail_ui::AutomationDetailItem::OFFSET) {
        const auto* slot = services_.automationSlot(index);
        const auto range = macroAutomationOffsetEditRange(slot, coarse_edit_active_);
        services_.setAutomationWindowOffsetBeats(
            index,
            macroAutomationEncoderPositionToBeat(clamped, range)
        );
    }
}

FLASHMEM void MacroAutomationHandler::configureOptForFocusedRow() {
    uint8_t steps = 1;
    float position = 0.0f;
    if (modulatorPickerActive() || modulatorCreateActive()) {
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, 1);
        encoders_.setPosition(Config::EncoderID::OPT, 0.0f);
        return;
    }
    if (modulatorAuditionActive()) {
        const auto* source = auditionSource(pages_);
        const auto* binding = auditionBinding(pages_);
        if (source != nullptr && binding != nullptr) {
            if (existingModulatorAuditionActive()) {
                if (macro_edit_.modulationFocusedRow.get() != 1U) {
                    encoders_.setDiscreteSteps(Config::EncoderID::OPT, 1);
                    encoders_.setPosition(Config::EncoderID::OPT, 0.0f);
                    return;
                }
                steps = static_cast<uint8_t>(
                    detail_ui::lfo_audition::DEPTH_STEP_COUNT
                );
                const int16_t percent =
                    detail_ui::lfo_audition::depthQ15ToPercent(
                        binding->amountQ15
                    );
                position = static_cast<float>(percent + 100) / 200.0f;
                encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
                encoders_.setPosition(Config::EncoderID::OPT, position);
                return;
            }
            if (source->kind == core::state::modulation::ModulatorKind::ADSR) {
                const auto row = static_cast<adsr_ui::AuditionItem>(
                    std::min<uint8_t>(
                        macro_edit_.modulationFocusedRow.get(),
                        adsr_ui::AUDITION_ITEM_COUNT - 1U
                    )
                );
                const auto& parameters = source->parameters.adsr;
                switch (row) {
                    case adsr_ui::AuditionItem::ATTACK:
                        steps = adsr_ui::DURATION_COUNT;
                        position = static_cast<float>(adsr_ui::durationIndex(
                            parameters.attack, parameters.timing
                        )) / static_cast<float>(steps - 1U);
                        break;
                    case adsr_ui::AuditionItem::DECAY:
                        steps = adsr_ui::DURATION_COUNT;
                        position = static_cast<float>(adsr_ui::durationIndex(
                            parameters.decay, parameters.timing
                        )) / static_cast<float>(steps - 1U);
                        break;
                    case adsr_ui::AuditionItem::SUSTAIN:
                        steps = adsr_ui::SUSTAIN_STEP_COUNT;
                        position = static_cast<float>(
                            adsr_ui::sustainQ15ToPercent(parameters.sustainQ15)
                        ) / 100.0f;
                        break;
                    case adsr_ui::AuditionItem::RELEASE:
                        steps = adsr_ui::DURATION_COUNT;
                        position = static_cast<float>(adsr_ui::durationIndex(
                            parameters.release, parameters.timing
                        )) / static_cast<float>(steps - 1U);
                        break;
                    case adsr_ui::AuditionItem::DEPTH: {
                        steps = static_cast<uint8_t>(
                            detail_ui::lfo_audition::DEPTH_STEP_COUNT
                        );
                        const int16_t percent =
                            detail_ui::lfo_audition::depthQ15ToPercent(
                                binding->amountQ15
                            );
                        position = static_cast<float>(percent + 100) / 200.0f;
                        break;
                    }
                }
                encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
                encoders_.setPosition(Config::EncoderID::OPT, position);
                return;
            }
            const uint8_t row = std::min<uint8_t>(
                macro_edit_.modulationFocusedRow.get(),
                2U
            );
            if (row == 0U) {
                steps = detail_ui::lfo_audition::SHAPE_COUNT;
                position = static_cast<float>(source->parameters.lfo.shape) /
                    static_cast<float>(steps - 1U);
            } else if (row == 1U) {
                steps = detail_ui::lfo_audition::RATE_COUNT;
                position = static_cast<float>(
                    detail_ui::lfo_audition::rateIndex(
                        source->parameters.lfo.periodTicks
                    )
                ) / static_cast<float>(steps - 1U);
            } else {
                steps = static_cast<uint8_t>(
                    detail_ui::lfo_audition::DEPTH_STEP_COUNT
                );
                const int16_t percent =
                    detail_ui::lfo_audition::depthQ15ToPercent(
                        binding->amountQ15
                    );
                position = static_cast<float>(percent + 100) / 200.0f;
            }
        }
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
        encoders_.setPosition(Config::EncoderID::OPT, position);
        return;
    }
    const auto context = detailContext(services_, macroIndex());
    if (modulationDetailActive()) {
        if (!context.modulationStored) {
            encoders_.setDiscreteSteps(Config::EncoderID::OPT, 1);
            encoders_.setPosition(Config::EncoderID::OPT, 0.0f);
            return;
        }
        const auto rows = modulationRows(pages_, macroIndex());
        if (rows.allRow(macro_edit_.modulationFocusedRow.get())) {
            steps = 201U;
            position = static_cast<float>(
                services_.modulationGlobalDepthQ15(macroIndex())
            ) / static_cast<float>(std::numeric_limits<uint16_t>::max());
            encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
            encoders_.setPosition(Config::EncoderID::OPT, position);
            return;
        }
        const auto* binding = bindingForAssignmentOrdinal(
            pages_,
            rows,
            rows.assignmentOrdinal(macro_edit_.modulationFocusedRow.get())
        );
        if (binding != nullptr) {
            (void)services_.focusModulationBinding(macroIndex(), binding->id);
            steps = static_cast<uint8_t>(
                detail_ui::lfo_audition::DEPTH_STEP_COUNT
            );
            position = std::clamp(
                services_.modulationDepth(macroIndex()) * 0.5f + 0.5f,
                0.0f,
                1.0f
            );
        }
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
        encoders_.setPosition(Config::EncoderID::OPT, position);
        return;
    }
    const auto layout = detail_ui::buildAutomationDetailLayout(context);
    const auto item = layout.at(macro_edit_.automationFocusedRow.get());
    if (item == detail_ui::AutomationDetailItem::PLAYBACK) {
        steps = 2;
        position = context.automationPlayback ? 1.0f : 0.0f;
    } else if (item == detail_ui::AutomationDetailItem::LENGTH) {
        const auto range = macroAutomationLengthEditRange(coarse_edit_active_);
        steps = range.stepCount;
        const auto* slot = services_.automationSlot(macroIndex());
        if (slot != nullptr && slot->automation.active) {
            position = macroAutomationTicksToEncoderPosition(
                slot->automation.durationTicks,
                range
            );
        }
    } else if (item == detail_ui::AutomationDetailItem::OFFSET) {
        const auto* slot = services_.automationSlot(macroIndex());
        const auto range = macroAutomationOffsetEditRange(slot, coarse_edit_active_);
        steps = range.stepCount;
        if (slot != nullptr && slot->automation.active) {
            position = macroAutomationTicksToEncoderPosition(
                slot->automation.windowOffsetTicks,
                range
            );
        }
    }
    encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
    encoders_.setPosition(Config::EncoderID::OPT, position);
}

FLASHMEM void MacroAutomationHandler::setCoarseEditActive(bool active) {
    if (!this->active() || coarse_edit_active_ == active) return;
    coarse_edit_active_ = active;
    configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::backToMacroEdit() {
    if (!active()) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, false
    );
    services_.endDepthGesture();
    if (modulatorAuditionActive()) {
        (void)cancelModulatorAudition();
        return;
    }
    if (modulatorCreateActive()) {
        const auto rows = modulationRows(pages_, macroIndex());
        macro_edit_.closeModulatorCreate(rows.addSourceRow());
        configureOptForFocusedRow();
        return;
    }
    if (modulatorPickerActive()) {
        const auto rows = modulationRows(pages_, macroIndex());
        macro_edit_.closeModulatorPicker(rows.addSourceRow());
        configureOptForFocusedRow();
        return;
    }
    if (conversionPreviewActive()) {
        macro_edit_.closeConvertPreview();
        configureOptForFocusedRow();
        return;
    }
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::MACRO_AUTOMATION);
    if (modulationDetailActive()) macro_edit_.closeModulation();
    else macro_edit_.closeAutomation();
    // A cross-view deep-link restores the detail child without retaining a
    // hidden parent in the overlay stack. Materialize that parent only when
    // Back actually returns to Macro Edit.
    if (!overlays_.hasVisible() && macro_edit_.visible.get()) {
        overlays_.show(core::ui::OverlayType::MACRO_EDIT, false);
    }
}

FLASHMEM void MacroAutomationHandler::toggleFocusedPlayback() {
    if (!active()) return;
    if (conversionPreviewActive() || modulatorCreateActive() ||
        modulatorPickerActive() ||
        modulatorAuditionActive()) return;
    const uint8_t index = macroIndex();
    if (modulationDetailActive()) {
        const auto rows = modulationRows(pages_, index);
        const int row = macro_edit_.modulationFocusedRow.get();
        if (rows.allRow(row)) {
            (void)services_.setModulationPlayback(
                index,
                !services_.modulationPlaybackActiveFor(index)
            );
        } else {
            const auto* binding = bindingForAssignmentOrdinal(
                pages_,
                rows,
                rows.assignmentOrdinal(row)
            );
            if (binding != nullptr) {
                (void)services_.focusModulationBinding(index, binding->id);
                const bool enabled =
                    (binding->flags & core::state::modulation::
                        PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
                (void)services_.setFocusedModulationPlayback(index, !enabled);
            }
        }
    } else if (services_.automationStoredFor(index)) {
        (void)services_.setAutomationPlayback(
            index,
            !services_.automationPlaybackActiveFor(index)
        );
    }
    configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::copyFocusedSource() {
    if (!active()) return;
    if (conversionPreviewActive() || modulatorCreateActive() ||
        modulatorPickerActive() ||
        modulatorAuditionActive()) return;
    if (modulationDetailActive()) {
        const uint8_t index = macroIndex();
        const auto rows = modulationRows(pages_, index);
        const auto* binding = bindingForAssignmentOrdinal(
            pages_,
            rows,
            rows.assignmentOrdinal(macro_edit_.modulationFocusedRow.get())
        );
        if (binding == nullptr) return;
        (void)services_.focusModulationBinding(index, binding->id);
        (void)services_.copyModulation(macroIndex());
    } else {
        (void)services_.copyAutomation(macroIndex());
    }
}

FLASHMEM void MacroAutomationHandler::beginBottomLeftAction() {
    if (!active()) return;
    if (conversionPreviewActive() || modulatorCreateActive() ||
        modulatorPickerActive() ||
        modulatorAuditionActive()) return;
    services_.endDepthGesture();
    const uint8_t index = macroIndex();
    const bool modulation = modulationDetailActive();
    core::state::contextual::ContextActionId action =
        core::state::contextual::ContextActionId::NONE;
    auto target = sourceRef(pages_, index, modulation);
    if (modulation) {
        const auto rows = modulationRows(pages_, index);
        const int row = macro_edit_.modulationFocusedRow.get();
        target.node = static_cast<uint16_t>(row);
        if (rows.allRow(row)) {
            action = core::state::contextual::ContextActionId::CLEAR;
        } else if (rows.assignmentOrdinal(row) >= 0) {
            action = core::state::contextual::ContextActionId::REMOVE;
        }
    } else if (services_.automationStoredFor(index)) {
        action = core::state::contextual::ContextActionId::CLEAR;
    }
    (void)macro::MacroGuardedActionWorkflow::begin(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_LEFT,
        action,
        target,
        target,
        now_provider_ ? now_provider_() : 0U,
        static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
    );
}

FLASHMEM void MacroAutomationHandler::releaseBottomLeftAction() {
    if (!active()) return;
    const uint32_t nowMs = now_provider_ ? now_provider_() : 0U;
    const auto release = macro::MacroGuardedActionWorkflow::release(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_LEFT,
        nowMs
    );
    if (release == core::state::contextual::GuardedActionRelease::TAP) {
        toggleFocusedPlayback();
    } else if (release ==
               core::state::contextual::GuardedActionRelease::COMMITTED) {
        commitGuardedAction(nowMs);
    }
}

FLASHMEM void MacroAutomationHandler::beginBottomRightAction() {
    if (!active()) return;
    core::state::contextual::ContextActionId action =
        core::state::contextual::ContextActionId::NONE;
    if (modulatorAuditionActive()) {
        action = core::state::contextual::ContextActionId::APPLY;
    } else if (modulatorCreateActive() || modulatorPickerActive()) {
        action = core::state::contextual::ContextActionId::NONE;
    } else if (conversionPreviewActive()) {
        if (macro_edit_.conversionPreview.plan.actionable()) {
            action = macro_edit_.conversionPreview.plan.overwritesModulation
                ? core::state::contextual::ContextActionId::OVERWRITE
                : core::state::contextual::ContextActionId::APPLY;
        }
    } else {
        bool pasteContext = true;
        if (modulationDetailActive()) {
            const auto rows = modulationRows(pages_, macroIndex());
            pasteContext = services_.hasModulationAssignmentClipboard() &&
                (rows.assignmentCount == 0U ||
                 rows.assignmentOrdinal(
                     macro_edit_.modulationFocusedRow.get()
                 ) >= 0);
        }
        if (pasteContext) {
            const auto plan = modulationDetailActive()
                ? services_.preflightModulationPaste(macroIndex())
                : services_.preflightAutomationPaste(macroIndex());
            if (plan.actionable()) {
                action = plan.requiresOverwrite()
                    ? core::state::contextual::ContextActionId::OVERWRITE
                    : core::state::contextual::ContextActionId::PASTE;
            }
        }
    }
    auto target = (conversionPreviewActive() ||
                   modulatorCreateActive() ||
                   modulatorPickerActive() ||
                   modulatorAuditionActive())
        ? sourceRef(pages_, macroIndex(), true)
        : sourceRef(pages_, macroIndex(), modulationDetailActive());
    if (modulationDetailActive()) {
        target.node = macro_edit_.modulationFocusedRow.get();
    }
    (void)macro::MacroGuardedActionWorkflow::begin(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_RIGHT,
        action,
        target,
        target,
        now_provider_ ? now_provider_() : 0U,
        static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
    );
}

FLASHMEM void MacroAutomationHandler::releaseBottomRightAction() {
    if (!active()) return;
    const uint32_t nowMs = now_provider_ ? now_provider_() : 0U;
    const auto release = macro::MacroGuardedActionWorkflow::release(
        macro_edit_,
        core::state::MacroContextButton::BOTTOM_RIGHT,
        nowMs
    );
    if (release == core::state::contextual::GuardedActionRelease::TAP) {
        if (modulatorAuditionActive()) {
            const bool applied = applyModulatorAudition();
            macro::MacroGuardedActionWorkflow::complete(
                macro_edit_, applied, nowMs
            );
        } else if (conversionPreviewActive()) {
            const bool applied = applyConversion(false);
            macro::MacroGuardedActionWorkflow::complete(
                macro_edit_, applied, nowMs
            );
        } else {
            copyFocusedSource();
        }
    } else if (release ==
               core::state::contextual::GuardedActionRelease::COMMITTED) {
        commitGuardedAction(nowMs);
    }
}

FLASHMEM void MacroAutomationHandler::commitGuardedAction(uint32_t nowMs) {
    const auto feedback = macro_edit_.contextFeedback.get();
    const uint8_t index = macroIndex();
    const auto expectedTarget = (conversionPreviewActive() ||
                                 modulatorCreateActive() ||
                                 modulatorPickerActive() ||
                                 modulatorAuditionActive())
        ? sourceRef(pages_, index, true)
        : sourceRef(pages_, index, modulationDetailActive());
    auto contextualTarget = expectedTarget;
    if (modulationDetailActive()) {
        contextualTarget.node = macro_edit_.modulationFocusedRow.get();
    }
    if (feedback.target != contextualTarget) {
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, false, nowMs);
        return;
    }

    bool applied = false;
    if (feedback.action == core::state::contextual::ContextActionId::APPLY &&
        modulatorAuditionActive()) {
        applied = applyModulatorAudition();
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
        return;
    }
    if (feedback.action == core::state::contextual::ContextActionId::CLEAR &&
        !conversionPreviewActive()) {
        services_.endDepthGesture();
        applied = modulationDetailActive()
            ? services_.clearModulation(index)
            : services_.clearAutomation(index);
        if (applied && modulationDetailActive()) {
            macro_edit_.modulationFocusedRow.set(0);
        }
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
        configureOptForFocusedRow();
        return;
    }
    if (feedback.action == core::state::contextual::ContextActionId::REMOVE &&
        modulationDetailActive()) {
        services_.endDepthGesture();
        const auto beforeRows = modulationRows(pages_, index);
        const int row = macro_edit_.modulationFocusedRow.get();
        const int ordinal = beforeRows.assignmentOrdinal(row);
        const auto* selected = bindingForAssignmentOrdinal(
            pages_,
            beforeRows,
            ordinal
        );
        const auto* next = bindingForAssignmentOrdinal(
            pages_,
            beforeRows,
            ordinal + 1
        );
        if (next == nullptr) {
            next = bindingForAssignmentOrdinal(
                pages_,
                beforeRows,
                ordinal - 1
            );
        }
        const auto nextId = next != nullptr
            ? next->id
            : core::state::modulation::ModulationBindingId{};
        if (selected != nullptr) {
            (void)services_.focusModulationBinding(index, selected->id);
            applied = services_.removeFocusedModulation(index);
        }
        if (applied) {
            const auto afterRows = modulationRows(pages_, index);
            if (core::state::modulation::valid(nextId)) {
                (void)services_.focusModulationBinding(index, nextId);
                macro_edit_.modulationFocusedRow.set(static_cast<uint8_t>(
                    rowForBinding(pages_, afterRows, nextId)
                ));
            } else {
                macro_edit_.modulationFocusedRow.set(0);
            }
        }
        macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
        configureOptForFocusedRow();
        return;
    }

    if (feedback.action == core::state::contextual::ContextActionId::OVERWRITE &&
        conversionPreviewActive()) {
        applied = applyConversion(true);
    } else if (feedback.action == core::state::contextual::ContextActionId::PASTE ||
               feedback.action == core::state::contextual::ContextActionId::OVERWRITE) {
        const bool expectedOverwrite =
            feedback.action == core::state::contextual::ContextActionId::OVERWRITE;
        if (modulationDetailActive()) {
            const auto plan = services_.preflightModulationPaste(index);
            if (plan.actionable() &&
                plan.requiresOverwrite() == expectedOverwrite) {
                applied = services_.pasteModulation(index, expectedOverwrite);
                if (applied) {
                    const auto rows = modulationRows(pages_, index);
                    const auto focused = services_.focusedModulationBinding(index);
                    if (core::state::modulation::valid(focused)) {
                        macro_edit_.modulationFocusedRow.set(
                            static_cast<uint8_t>(
                                rowForBinding(pages_, rows, focused)
                            )
                        );
                    }
                }
            }
        } else if (automationDetailActive()) {
            const auto plan = services_.preflightAutomationPaste(index);
            if (plan.actionable() &&
                plan.requiresOverwrite() == expectedOverwrite) {
                applied = services_.pasteAutomation(index, expectedOverwrite);
            }
        }
    }
    macro::MacroGuardedActionWorkflow::complete(macro_edit_, applied, nowMs);
    if (applied && !conversionPreviewActive()) configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::update(uint32_t nowMs) {
    macro_edit_.updateModulatorNavigationFeedback(nowMs);
    const bool macroViewActive =
        active_view_.get() == core::ui::ViewType::MACRO;
    const bool macroViewReentered = macroViewActive && !macro_view_was_active_;
    macro_view_was_active_ = macroViewActive;
    const auto flowPhase = macro_edit_.flowPhase.get();
    if (flowPhase != observed_flow_phase_) {
        observed_flow_phase_ = flowPhase;
        coarse_edit_active_ = false;
        if (automationDetailActive() || modulationDetailActive() ||
            modulatorCreateActive() ||
            modulatorPickerActive() || modulatorAuditionActive()) {
            // This handler does not own the parent Macro Edit transition that
            // opens the detail overlay. Synchronize the physical encoder once
            // when that transition becomes observable so the first user turn
            // starts from the value shown on screen.
            configureOptForFocusedRow();
        }
    } else if (macroViewReentered && active()) {
        // The global view lifecycle restores generic OPT mechanics. Reapply
        // the focused source value before the first post-return user turn.
        configureOptForFocusedRow();
    }
    if (!active()) return;
    // Progress may reach COMMITTED while held, but the domain mutation waits
    // for release so this overlay still owns and consumes that release.
    (void)macro::MacroGuardedActionWorkflow::update(macro_edit_, nowMs);
}

FLASHMEM void MacroAutomationHandler::activateFocusedRow() {
    if (!active() || conversionPreviewActive()) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, false
    );
    const auto context = detailContext(services_, macroIndex());
    bool resumeAutomation = false;
    bool convert = false;
    if (modulatorAuditionActive()) {
        return;
    } else if (modulatorCreateActive()) {
        const uint8_t row = macro_edit_.modulationFocusedRow.get();
        if (row == 0U) {
            (void)startLfoAudition();
        } else if (row == 1U) {
            (void)startAdsrAudition();
        } else if (row == 2U) {
            (void)openModulatorPicker();
        }
        return;
    } else if (modulatorPickerActive()) {
        (void)startExistingModulatorAudition();
        return;
    } else if (modulationDetailActive()) {
        if (!context.modulationStored) {
            const uint8_t row = macro_edit_.modulationFocusedRow.get();
            if (row == 0U) {
                (void)startLfoAudition();
            } else if (row == 1U) {
                (void)startAdsrAudition();
            } else if (row == 2U) {
                (void)openModulatorPicker();
            }
        } else {
            const auto rows = modulationRows(pages_, macroIndex());
            const int row = macro_edit_.modulationFocusedRow.get();
            const auto* binding = bindingForAssignmentOrdinal(
                pages_,
                rows,
                rows.assignmentOrdinal(row)
            );
            if (binding != nullptr) {
                (void)services_.focusModulationBinding(
                    macroIndex(),
                    binding->id
                );
                services_.endDepthGesture();
                (void)modulator_navigation::openSourceFromMacro(
                    {
                        overlays_state_,
                        active_view_,
                        project_navigation_,
                        macro_edit_,
                        pages_,
                    },
                    macroIndex(),
                    binding->id,
                    static_cast<uint8_t>(row)
                );
            } else if (rows.addRow(row)) {
                macro_edit_.openModulatorCreate();
                configureOptForFocusedRow();
            }
        }
        return;
    } else {
        const auto layout = detail_ui::buildAutomationDetailLayout(context);
        const auto item = layout.at(macro_edit_.automationFocusedRow.get());
        resumeAutomation = item == detail_ui::AutomationDetailItem::RESUME;
        convert =
            item == detail_ui::AutomationDetailItem::CONVERT_TO_MODULATION;
    }
    if (resumeAutomation) {
        if (services_.resumeSources(macroIndex())) {
            macro_edit_.automationFocusedRow.set(0);
            configureOptForFocusedRow();
        }
    } else if (convert) {
        openConversionPreview();
    }
}

FLASHMEM void MacroAutomationHandler::openConversionPreview() {
    const auto plan = services_.preflightConversion(
        macroIndex(),
        core::state::macro::MacroAutomationConversionPolicy::MEAN
    );
    if (!plan.actionable()) return;
    services_.endDepthGesture();
    macro_edit_.openConvertPreview(plan);
}

FLASHMEM void MacroAutomationHandler::selectConversionPolicy(float delta) {
    if (!conversionPreviewActive() || !nav::hasTurnDelta(delta)) return;
    cancelContextAction(
        macro_edit_, now_provider_ ? now_provider_() : 0U, false
    );
    const int current = static_cast<int>(macro_edit_.conversionPreview.policy);
    const int next = nav::nextWrappedIndex(delta, current, 3);
    const auto policy = static_cast<
        core::state::macro::MacroAutomationConversionPolicy>(next);
    macro_edit_.conversionPreview.setPlan(
        services_.preflightConversion(macroIndex(), policy)
    );
}

FLASHMEM bool MacroAutomationHandler::applyConversion(bool overwriteGesture) {
    if (!conversionPreviewActive()) return false;
    const auto plan = macro_edit_.conversionPreview.plan;
    if (!plan.actionable()) return false;
    if (plan.overwritesModulation && !overwriteGesture) return false;
    if (!services_.applyConversion(
            macroIndex(),
            plan,
            overwriteGesture
        )) {
        macro_edit_.conversionPreview.setPlan(
            services_.preflightConversion(
                macroIndex(),
                macro_edit_.conversionPreview.policy
            )
        );
        return false;
    }
    macro_edit_.closeConvertPreview();
    const auto rows = modulationRows(pages_, macroIndex());
    const auto* createdBinding = bindingForAssignmentOrdinal(
        pages_,
        rows,
        0
    );
    if (createdBinding != nullptr) {
        // Conversion lands on the concrete source it just created/replaced.
        // This keeps source editing and contextual delete one gesture away,
        // instead of silently selecting the aggregate "All" row.
        (void)services_.focusModulationBinding(
            macroIndex(),
            createdBinding->id
        );
        macro_edit_.modulationFocusedRow.set(static_cast<uint8_t>(
            rows.firstAssignmentRow()
        ));
    }
    configureOptForFocusedRow();
    return true;
}

FLASHMEM bool MacroAutomationHandler::startLfoAudition() {
    const auto result = services_.beginDefaultLfoAudition(macroIndex());
    if (!result.changed()) return false;
    macro_edit_.openNewModulatorAudition();
    if (modulator_navigation::openAuditionSourceFromMacro(
            {
                overlays_state_,
                active_view_,
                project_navigation_,
                macro_edit_,
                pages_,
            },
            macroIndex()
        )) {
        return true;
    }
    configureOptForFocusedRow();
    return true;
}

FLASHMEM bool MacroAutomationHandler::startAdsrAudition() {
    const auto result = services_.beginDefaultAdsrAudition(macroIndex());
    if (!result.changed()) return false;
    macro_edit_.openNewModulatorAudition();
    if (modulator_navigation::openAuditionSourceFromMacro(
            {
                overlays_state_,
                active_view_,
                project_navigation_,
                macro_edit_,
                pages_,
            },
            macroIndex()
        )) {
        return true;
    }
    configureOptForFocusedRow();
    return true;
}

FLASHMEM bool MacroAutomationHandler::openModulatorPicker() {
    const int count = static_cast<int>(
        pages_.control.authored.modulation.sourceCount
    );
    if (count <= 0) return false;
    const int selected = std::clamp(
        macro_edit_.modulatorPickerIndex.get(),
        0,
        count - 1
    );
    macro_edit_.openModulatorPicker(selected);
    configureOptForFocusedRow();
    return true;
}

FLASHMEM bool MacroAutomationHandler::startExistingModulatorAudition() {
    if (!modulatorPickerActive()) return false;
    const auto& graph = pages_.control.authored.modulation;
    const int selected = macro_edit_.modulatorPickerIndex.get();
    if (selected < 0 || selected >= static_cast<int>(graph.sourceCount)) {
        return false;
    }
    const auto result = services_.beginExistingModulatorAudition(
        macroIndex(),
        graph.sources[static_cast<uint16_t>(selected)].id
    );
    if (!result.changed()) return false;
    macro_edit_.openExistingModulatorAudition();
    configureOptForFocusedRow();
    return true;
}

FLASHMEM bool MacroAutomationHandler::cancelModulatorAudition() {
    const bool existing = existingModulatorAuditionActive();
    const auto* source = auditionSource(pages_);
    const bool adsr = !existing && source != nullptr &&
        source->kind == core::state::modulation::ModulatorKind::ADSR;
    if (!modulatorAuditionActive() ||
        !services_.cancelModulatorAudition(macroIndex())) {
        return false;
    }
    if (existing) {
        macro_edit_.cancelExistingModulatorAudition();
    } else {
        const auto rows = modulationRows(pages_, macroIndex());
        macro_edit_.cancelNewModulatorAudition(
            rows.assignmentCount == 0U ? 0U
                                       : static_cast<uint8_t>(rows.addSourceRow())
        );
    }
    auto feedback = macro_edit_.contextFeedback.get();
    const core::state::contextual::ContextEntityRef modulation{
        .kind = core::state::contextual::ContextEntityKind::MODULATION_LANE,
        .track = pages_.currentActiveTrack(),
        .page = pages_.currentActivePage(),
        .item = macroIndex(),
        .node = static_cast<uint16_t>(existing ? 1U : (adsr ? 2U : 0U)),
    };
    const uint32_t nowMs = now_provider_ ? now_provider_() : 0U;
    core::state::contextual::setOperationFeedback(
        feedback,
        core::state::contextual::ContextActionId::CANCEL,
        modulation,
        modulation,
        core::state::contextual::OperationFeedbackStatus::CANCELLED,
        core::state::contextual::ContextActionReason::NONE,
        core::state::contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS
    );
    macro_edit_.contextFeedback.set(feedback);
    configureOptForFocusedRow();
    return true;
}

FLASHMEM bool MacroAutomationHandler::applyModulatorAudition() {
    const bool existing = existingModulatorAuditionActive();
    const auto* source = auditionSource(pages_);
    const bool adsr = !existing && source != nullptr &&
        source->kind == core::state::modulation::ModulatorKind::ADSR;
    if (!modulatorAuditionActive() ||
        !services_.applyModulatorAudition(macroIndex())) {
        return false;
    }
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::MACRO_AUTOMATION);
    macro_edit_.applyModulatorAudition();
    auto feedback = macro_edit_.contextFeedback.get();
    const core::state::contextual::ContextEntityRef modulation{
        .kind = core::state::contextual::ContextEntityKind::MODULATION_LANE,
        .track = pages_.currentActiveTrack(),
        .page = pages_.currentActivePage(),
        .item = macroIndex(),
        .node = static_cast<uint16_t>(existing ? 1U : (adsr ? 2U : 0U)),
    };
    const uint32_t nowMs = now_provider_ ? now_provider_() : 0U;
    core::state::contextual::setOperationFeedback(
        feedback,
        core::state::contextual::ContextActionId::APPLY,
        modulation,
        modulation,
        core::state::contextual::OperationFeedbackStatus::APPLIED,
        core::state::contextual::ContextActionReason::NONE,
        core::state::contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        nowMs,
        Config::Timing::CONTEXT_APPLIED_FEEDBACK_MS
    );
    macro_edit_.contextFeedback.set(feedback);
    return true;
}

}  // namespace core::handler
