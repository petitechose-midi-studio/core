#include "handler/project/ProjectHandlerInternals.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ModulationDepthParameterMapping.hpp"
#include "state/modulation/ModulatorEnvelopeParameterMapping.hpp"
#include "state/modulation/ModulatorLfoParameterMapping.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/modulation/ProjectModulatorSourceSession.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

namespace core::handler {

using namespace project_handler_internal;
namespace depth_parameter = core::state::modulation::depth;
namespace envelope_parameter = core::state::modulation::envelope;
namespace lfo_parameter = core::state::modulation::lfo;

namespace {

FLASHMEM void configureModulationDepthEncoder(
    oc::api::EncoderAPI& encoders,
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulationBindingState* binding
) {
    const auto scale = binding != nullptr
        ? depth_parameter::scaleFor(
              control.authored.modulation,
              control.authored.curves,
              *binding
          )
        : depth_parameter::Scale::STANDARD;
    const float position = binding != nullptr
        ? depth_parameter::normalizedPosition(binding->amountQ15)
        : 0.5f;
    if (depth_parameter::stepCount(scale) > 255) {
        configureOptContinuous(encoders, position);
    } else {
        configureOptDiscrete(
            encoders,
            depth_parameter::stepCount(scale),
            position
        );
    }
}

}  // namespace

FLASHMEM void ProjectHandler::syncFocusedEncoder() {
    using core::state::project::ProjectNodeId;

    const auto node = navigation_.currentNode.get();
    const uint8_t row = navigation_.focusedRow.get();

    core::state::macro::MacroAutomationSlotAddress auditionAddress{};
    if (node == ProjectNodeId::MODULATOR_DESTINATION_PICKER &&
        modulatorAuditionAddress(auditionAddress)) {
        const auto* binding =
            core::state::modulation::findProjectModulationBinding(
                pages_.control.authored.modulation,
                pages_.control.audition.bindingId
            );
        configureModulationDepthEncoder(encoders_, pages_.control, binding);
        return;
    }

    if (node == ProjectNodeId::MODULATORS_ROOT) {
        const auto* source = focusedModulator();
        if (source == nullptr ||
            source->kind != core::state::modulation::ModulatorKind::LFO) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        const auto& lfo = source->parameters.lfo;
        if (lfo.timing == core::state::modulation::ModulatorTimingMode::FREE) {
            const int count = static_cast<int>(
                PROJECT_MODULATOR_FREE_PERIODS_MS.size()
            );
            configureOptDiscrete(
                encoders_,
                count,
                indexToNormalized(
                    projectModulatorFreePeriodIndex(lfo.freePeriodMs),
                    count
                )
            );
        } else {
            configureOptDiscrete(
                encoders_,
                lfo_parameter::RATE_COUNT,
                indexToNormalized(
                    lfo_parameter::rateIndex(lfo.periodTicks),
                    lfo_parameter::RATE_COUNT
                )
            );
        }
        return;
    }

    if (node == ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER) {
        configureOptDiscrete(encoders_, 1, 0.0f);
        return;
    }

    if (node == ProjectNodeId::MODULATOR_TRIGGER) {
        using core::state::project::modulators::TriggerDetailItem;
        const auto* source = focusedModulator();
        const auto session = source != nullptr
            ? core::state::modulation::resolveProjectModulatorSourceSession(
                  pages_.control,
                  source->id
              )
            : core::state::modulation::
                  ProjectModulatorSourceSessionDescriptor{};
        if (pages_.control.audition.active() && !session.valid()) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        const auto* trigger = source
            ? core::state::modulation::findProjectModulationTriggerForSource(
                  pages_.control.authored.modulation,
                  source->id
              )
            : nullptr;
        if (!trigger) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        const auto& route = trigger->trigger;
        if (row >= core::state::project::modulators::
                MODULATOR_TRIGGER_DETAIL_COUNT) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        const auto item = static_cast<TriggerDetailItem>(row);
        if (item == TriggerDetailItem::TRACK) {
            configureOptDiscrete(
                encoders_,
                16,
                indexToNormalized(route.track, 16)
            );
        } else {
            const uint8_t current = item == TriggerDetailItem::NOTE_LOW
                ? route.noteMin
                : (item == TriggerDetailItem::NOTE_HIGH
                    ? route.noteMax
                    : (item == TriggerDetailItem::VELOCITY_LOW
                        ? trigger->velocityMin
                        : trigger->velocityMax));
            configureOptDiscrete(
                encoders_,
                128,
                indexToNormalized(current, 128)
            );
        }
        return;
    }

    if (node == ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        node == ProjectNodeId::MODULATOR_SOURCE_OPTIONS) {
        const auto* source = focusedModulator();
        if (!source) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        const auto session = core::state::modulation::
            resolveProjectModulatorSourceSession(pages_.control, source->id);
        if (pages_.control.audition.active() && !session.valid()) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        const bool options = node == ProjectNodeId::MODULATOR_SOURCE_OPTIONS;
        const auto layout = core::state::project::modulators::
            sourceWorkspaceLayout(source->kind, options, session.audition());
        const auto item = layout.at(row);
        using Item = core::state::project::modulators::SourceDetailItem;
        using namespace core::state::modulation;
        switch (item) {
            case Item::RECORD:
                configureOptDiscrete(encoders_, 1, 0.0f);
                return;
            case Item::LENGTH: {
                const auto* curve = findProjectCurve(
                    pages_.control.authored.curves,
                    source->parameters.recordedCurveId
                );
                const uint16_t duration = curve != nullptr
                    ? curve->durationTicks
                    : PROJECT_CONTROL_TICKS_PER_BEAT;
                const int beats = std::clamp<int>(
                    (duration + PROJECT_CONTROL_TICKS_PER_BEAT / 2U) /
                        PROJECT_CONTROL_TICKS_PER_BEAT,
                    1,
                    64
                );
                configureOptDiscrete(
                    encoders_, 64, indexToNormalized(beats - 1, 64)
                );
                return;
            }
            case Item::ENABLED:
                configureOptDiscrete(
                    encoders_,
                    2,
                    (source->flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U
                        ? 1.0f : 0.0f
                );
                return;
            case Item::SHAPE:
                configureOptDiscrete(
                    encoders_,
                    lfo_parameter::SHAPE_COUNT,
                    indexToNormalized(
                        static_cast<int>(source->parameters.lfo.shape),
                        lfo_parameter::SHAPE_COUNT
                    )
                );
                return;
            case Item::RATE: {
                const auto& lfo = source->parameters.lfo;
                if (lfo.timing == ModulatorTimingMode::FREE) {
                    const int count = static_cast<int>(
                        PROJECT_MODULATOR_FREE_PERIODS_MS.size()
                    );
                    configureOptDiscrete(
                        encoders_,
                        count,
                        indexToNormalized(
                            projectModulatorFreePeriodIndex(lfo.freePeriodMs),
                            count
                        )
                    );
                } else {
                    configureOptDiscrete(
                        encoders_,
                        lfo_parameter::RATE_COUNT,
                        indexToNormalized(
                            lfo_parameter::rateIndex(
                                lfo.periodTicks
                            ),
                            lfo_parameter::RATE_COUNT
                        )
                    );
                }
                return;
            }
            case Item::TIMING:
                configureOptDiscrete(
                    encoders_,
                    2,
                    (source->kind == ModulatorKind::ADSR
                         ? modulatorAdsrTiming(
                               source->parameters.adsr.traits
                           )
                         : source->parameters.lfo.timing) ==
                            ModulatorTimingMode::FREE
                        ? 1.0f : 0.0f
                );
                return;
            case Item::PHASE:
                configureOptDiscrete(
                    encoders_,
                    101,
                    std::clamp(
                        (static_cast<float>(source->parameters.lfo.phaseQ15) +
                         32767.0f) /
                            65534.0f,
                        0.0f,
                        1.0f
                    )
                );
                return;
            case Item::RETRIGGER:
                if (source->kind == ModulatorKind::ADSR) {
                    configureOptDiscrete(
                        encoders_,
                        2,
                        indexToNormalized(
                            static_cast<int>(
                                modulatorAdsrRetrigger(
                                    source->parameters.adsr.traits
                                )
                            ),
                            2
                        )
                    );
                    return;
                }
                if (source->parameters.lfo.retrigger ==
                    ModulatorRetriggerPolicy::EXPLICIT_TRIGGER) {
                    configureOptDiscrete(encoders_, 1, 0.0f);
                    return;
                }
                configureOptDiscrete(
                    encoders_,
                    2,
                    indexToNormalized(
                        static_cast<int>(source->parameters.lfo.retrigger),
                        2
                    )
                );
                return;
            case Item::DEPTH: {
                const auto* binding = findProjectModulationBinding(
                    pages_.control.authored.modulation,
                    pages_.control.audition.bindingId
                );
                configureModulationDepthEncoder(
                    encoders_,
                    pages_.control,
                    binding
                );
                return;
            }
            case Item::DELAY:
            case Item::ATTACK:
            case Item::HOLD:
            case Item::DECAY:
            case Item::RELEASE:
            case Item::SMOOTH: {
                const auto parameter = item == Item::DELAY
                    ? ModulatorEnvelopeTimeParameter::DELAY
                    : (item == Item::ATTACK
                        ? ModulatorEnvelopeTimeParameter::ATTACK
                        : (item == Item::HOLD
                            ? ModulatorEnvelopeTimeParameter::HOLD
                            : (item == Item::DECAY
                                ? ModulatorEnvelopeTimeParameter::DECAY
                                : (item == Item::RELEASE
                                    ? ModulatorEnvelopeTimeParameter::RELEASE
                                    : ModulatorEnvelopeTimeParameter::SMOOTH))));
                const auto timing = modulatorAdsrTiming(
                    source->parameters.adsr.traits
                );
                const int count = envelope_parameter::durationCount(
                    timing,
                    parameter
                );
                configureOptDiscrete(
                    encoders_,
                    count,
                    indexToNormalized(
                        envelope_parameter::durationIndex(
                            modulatorEnvelopeDuration(
                                source->parameters.adsr,
                                parameter
                            ),
                            timing,
                            parameter
                        ),
                        count
                    )
                );
                return;
            }
            case Item::SUSTAIN:
                configureOptDiscrete(
                    encoders_,
                    101,
                    std::clamp(
                        static_cast<float>(source->parameters.adsr.sustainQ15) /
                            static_cast<float>(
                                PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15
                            ),
                        0.0f,
                        1.0f
                    )
                );
                return;
            case Item::RESPONSE:
                configureOptDiscrete(
                    encoders_,
                    3,
                    indexToNormalized(
                        static_cast<int>(modulatorAdsrCurve(
                            source->parameters.adsr.traits
                        )),
                        3
                    )
                );
                return;
            default:
                configureOptDiscrete(encoders_, 1, 0.0f);
                return;
        }
    }

    if (node == ProjectNodeId::MODULATOR_DESTINATIONS) {
        const auto* binding = focusedModulationBinding();
        if (!binding) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        configureModulationDepthEncoder(encoders_, pages_.control, binding);
        return;
    }

    if (node == ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        configureOptDiscrete(encoders_, 1, 0.0f);
        return;
    }

    if (node == ProjectNodeId::MUSIC_SCALE && row <= 2) {
        const int count = scale_settings_.choiceCount(row);
        if (count > 0) {
            configureOptDiscrete(
                encoders_,
                count,
                indexToNormalized(scale_settings_.currentChoiceIndex(row), count)
            );
        }
        return;
    }

    if (node == ProjectNodeId::MUSIC_ROOT) {
        if (row == 3U) {
            configureOptDiscrete(
                encoders_,
                project::PROJECT_STEP_PASTE_MODE_COUNT,
                indexToNormalized(
                    static_cast<int>(navigation_.stepPasteMode),
                    project::PROJECT_STEP_PASTE_MODE_COUNT
                )
            );
            return;
        }
    }

    if (node == ProjectNodeId::MUSIC_CC_DEFAULTS &&
        row < project::PROJECT_CC_LANE_DEFAULT_COUNT) {
        const uint8_t lane = row;
        configureOptDiscrete(
            encoders_,
            project::PROJECT_MIDI_CC_COUNT,
            indexToNormalized(
                navigation_.ccLaneDefaultControllers[lane],
                project::PROJECT_MIDI_CC_COUNT
            )
        );
        return;
    }

    if (node == ProjectNodeId::TRANSPORT_ROOT) {
        switch (row) {
            case 0:
                configureOptContinuous(
                    encoders_,
                    tempoToNormalized(status_bar_.tempo.get()),
                    normalizedTurnsForStepRate(
                        project::PROJECT_TEMPO_RANGE_STEPS,
                        PROJECT_OPT_TEMPO_STEPS_PER_TURN
                    )
                );
                return;
            case 1:
                configureOptDiscrete(
                    encoders_,
                    project::PROJECT_SWING_STEPS,
                    indexToNormalized(navigation_.transportSwingPercent, project::PROJECT_SWING_STEPS),
                    normalizedTurnsForStepRate(
                        project::PROJECT_SWING_STEPS,
                        PROJECT_OPT_PERCENT_STEPS_PER_TURN
                    )
                );
                return;
            case 2:
                configureOptDiscrete(
                    encoders_,
                    3,
                    indexToNormalized(device_settings_.currentChoiceIndex(0U), 3)
                );
                return;
            case 3:
                configureOptDiscrete(
                    encoders_,
                    project::PROJECT_RUN_MODE_COUNT,
                    indexToNormalized(navigation_.transportRunMode, project::PROJECT_RUN_MODE_COUNT)
                );
                return;
            default:
                return;
        }
    }

    if (node == ProjectNodeId::ROUTING_ROOT &&
        row < core::state::project::PROJECT_TRACK_COUNT) {
        const uint8_t channel =
            core::state::project::projectTrackMidiChannel(
                project_tracks_,
                row
            );
        configureOptDiscrete(
            encoders_,
            project::PROJECT_MIDI_CHANNEL_COUNT,
            indexToNormalized(channel, project::PROJECT_MIDI_CHANNEL_COUNT)
        );
        return;
    }

    if (isProjectNameEditorNode(node)) {
        navigation_.projectNameOptRawPosition = 0.0f;
        navigation_.projectNameOptRowAccumulator = 0.0f;
        configureOptRaw(encoders_);
        return;
    }
}

}  // namespace core::handler
