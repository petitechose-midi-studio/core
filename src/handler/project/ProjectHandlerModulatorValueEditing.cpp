#include "handler/project/ProjectHandlerInternals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

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

const char FEEDBACK_DEPTH_PREVIEW_FORMAT[] PROGMEM =
    "Depth %+d%% - Preview";
const char FEEDBACK_SHARED_SOURCE_READ_ONLY[] PROGMEM =
    "Shared source - Apply then edit";

FLASHMEM uint16_t convertEnvelopeDurationTiming(
    uint16_t duration,
    core::state::modulation::ModulatorTimingMode previous,
    core::state::modulation::ModulatorTimingMode next,
    core::state::modulation::ModulatorEnvelopeTimeParameter parameter,
    core::state::modulation::ModulatorEnvelopeFeel feel,
    float tempoBpm
) {
    using namespace core::state::modulation;
    if (previous == next || duration == 0U) return duration;
    const double bpm = std::max(1.0, static_cast<double>(tempoBpm));
    if (previous == ModulatorTimingMode::SYNC) {
        const uint32_t effectiveTicks = resolveModulatorEnvelopeSyncTicks(
            duration,
            feel
        );
        const double milliseconds =
            static_cast<double>(effectiveTicks) * 60000.0 /
            (bpm * static_cast<double>(MODULATOR_ENVELOPE_TICKS_PER_BEAT));
        return static_cast<uint16_t>(std::clamp<long>(
            std::lround(milliseconds),
            0L,
            maximumModulatorEnvelopeFreeMilliseconds(parameter)
        ));
    }

    const double targetTicks = static_cast<double>(duration) * bpm *
        static_cast<double>(MODULATOR_ENVELOPE_TICKS_PER_BEAT) / 60000.0;
    uint16_t nearest = 0U;
    double nearestDistance = targetTicks;
    const uint16_t maximum = maximumModulatorEnvelopeSyncBaseTicks(parameter);
    for (const uint16_t candidate : MODULATOR_ENVELOPE_SYNC_BASE_TICKS) {
        if (candidate > maximum) break;
        const double effective = static_cast<double>(
            resolveModulatorEnvelopeSyncTicks(candidate, feel)
        );
        const double distance = std::fabs(effective - targetTicks);
        if (distance < nearestDistance) {
            nearest = candidate;
            nearestDistance = distance;
        }
    }
    return nearest;
}

}  // namespace

FLASHMEM bool ProjectHandler::setFocusedModulatorValue(float normalized) {
    using namespace core::state::modulation;
    using Item = core::state::project::modulators::SourceDetailItem;
    core::state::macro::MacroAutomationSlotAddress auditionAddress{};
    const bool auditioning = modulatorAuditionAddress(auditionAddress);
    if (auditioning && navigation_.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        auto* binding = findProjectModulationBinding(
            pages_.control.authored.modulation,
            pages_.control.audition.bindingId
        );
        if (!binding) return true;
        const auto scale = depth_parameter::scaleFor(
            pages_.control.authored.modulation,
            pages_.control.authored.curves,
            *binding
        );
        const int16_t amount = depth_parameter::amountQ15AtNormalized(
            clampNormalized(normalized),
            scale
        );
        const int16_t percent =
            depth_parameter::amountQ15ToPercent(amount, scale);
        if (binding->amountQ15 != amount) {
            binding->amountQ15 = amount;
            pages_.control.markAuthoredMutation();
            refreshModulatorPreview(false);
        }
        char feedback[48]{};
        std::snprintf(
            feedback,
            sizeof(feedback),
            FEEDBACK_DEPTH_PREVIEW_FORMAT,
            static_cast<int>(percent)
        );
        navigation_.setLifecycleFeedback(feedback);
        return true;
    }
    if (navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS) {
        auto* binding = focusedModulationBinding();
        if (!binding) return false;
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            binding->destination.track,
            binding->destination.page,
            binding->destination.macro,
        };
        const auto scale = depth_parameter::scaleFor(
            pages_.control.authored.modulation,
            pages_.control.authored.curves,
            *binding
        );
        const int16_t amount = depth_parameter::amountQ15AtNormalized(
            clampNormalized(normalized),
            scale
        );
        const float depth = static_cast<float>(amount) / 32767.0f;
        if (macro_history_.setModulationBindingDepthCoalesced(
                pages_, address, binding->id, depth
            )) {
            publishModulatorMutation(false);
        }
        return true;
    }
    if (navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_TRIGGER) {
        using core::state::project::modulators::TriggerDetailItem;
        auto* source = focusedModulator();
        if (!source || source->kind != ModulatorKind::ADSR) return false;
        const auto session = resolveProjectModulatorSourceSession(
            pages_.control,
            source->id
        );
        if (pages_.control.audition.active() && !session.valid()) return true;
        if (session.audition() && !session.allows(
                ProjectModulatorSourceSessionCapability::EDIT_TRIGGER
            )) {
            navigation_.setLifecycleFeedback(
                FEEDBACK_SHARED_SOURCE_READ_ONLY
            );
            return true;
        }
        auto* binding = findProjectModulationTriggerForSource(
            pages_.control.authored.modulation,
            source->id
        );
        if (!binding || binding->trigger.kind !=
                ModulationTriggerKind::TRACK_NOTE) {
            return false;
        }
        auto trigger = binding->trigger;
        const uint8_t row = navigation_.focusedRow.get();
        if (row >= core::state::project::modulators::
                MODULATOR_TRIGGER_DETAIL_COUNT) {
            return false;
        }
        const auto item = static_cast<TriggerDetailItem>(row);
        const int choice = item == TriggerDetailItem::TRACK
            ? normalizedToIndex(clampNormalized(normalized), 16)
            : normalizedToIndex(clampNormalized(normalized), 128);
        uint8_t velocityMin = binding->velocityMin;
        uint8_t velocityMax = binding->velocityMax;
        if (item == TriggerDetailItem::TRACK) {
            trigger.track = static_cast<uint8_t>(choice);
        } else if (item == TriggerDetailItem::NOTE_LOW) {
            trigger.noteMin = static_cast<uint8_t>(choice);
            if (trigger.noteMax < trigger.noteMin) {
                trigger.noteMax = trigger.noteMin;
            }
        } else if (item == TriggerDetailItem::NOTE_HIGH) {
            trigger.noteMax = static_cast<uint8_t>(choice);
            if (trigger.noteMin > trigger.noteMax) {
                trigger.noteMin = trigger.noteMax;
            }
        } else if (item == TriggerDetailItem::VELOCITY_LOW) {
            velocityMin = static_cast<uint8_t>(choice);
            if (velocityMax < velocityMin) velocityMax = velocityMin;
        } else if (item == TriggerDetailItem::VELOCITY_HIGH) {
            velocityMax = static_cast<uint8_t>(choice);
            if (velocityMin > velocityMax) velocityMin = velocityMax;
        } else {
            return false;
        }
        const bool provisional = session.newAudition();
        if (provisional) {
            if (binding->trigger != trigger ||
                binding->velocityMin != velocityMin ||
                binding->velocityMax != velocityMax) {
                binding->trigger = trigger;
                binding->velocityMin = velocityMin;
                binding->velocityMax = velocityMax;
                pages_.control.markAuthoredMutation();
                refreshModulatorPreview(false);
            }
        } else if (macro_history_.setProjectModulationTriggerCoalesced(
                       pages_,
                       source->id,
                       trigger,
                       (binding->flags &
                        PROJECT_MODULATION_TRIGGER_FLAG_ENABLED) != 0U,
                       velocityMin,
                       velocityMax
                   )) {
            publishModulatorMutation(false);
        }
        return true;
    }
    auto* source = focusedModulator();
    if (!source) return false;

    const auto node = navigation_.currentNode.get();
    const auto session = resolveProjectModulatorSourceSession(
        pages_.control,
        source->id
    );
    if (pages_.control.audition.active() && !session.valid()) return true;
    const bool sourceAudition = session.audition();
    const bool provisional = session.newAudition();
    Item item = Item::RATE;
    if (node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS) {
        const bool options = node ==
            core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS;
        item = core::state::project::modulators::sourceWorkspaceLayout(
            source->kind,
            options,
            sourceAudition
        ).at(navigation_.focusedRow.get());
    } else if (node != core::state::project::ProjectNodeId::MODULATORS_ROOT) {
        return false;
    }

    const float value = clampNormalized(normalized);
    if (item == Item::DEPTH) {
        auto* binding = findProjectModulationBinding(
            pages_.control.authored.modulation,
            pages_.control.audition.bindingId
        );
        if (!session.allows(
                ProjectModulatorSourceSessionCapability::EDIT_DEPTH
            ) || binding == nullptr) {
            return false;
        }
        const auto scale = depth_parameter::scaleFor(
            pages_.control.authored.modulation,
            pages_.control.authored.curves,
            *binding
        );
        const int16_t amount =
            depth_parameter::amountQ15AtNormalized(value, scale);
        const int16_t percent =
            depth_parameter::amountQ15ToPercent(amount, scale);
        if (binding->amountQ15 != amount) {
            binding->amountQ15 = amount;
            pages_.control.markAuthoredMutation();
            refreshModulatorPreview(false);
        }
        char feedback[48]{};
        std::snprintf(
            feedback,
            sizeof(feedback),
            FEEDBACK_DEPTH_PREVIEW_FORMAT,
            static_cast<int>(percent)
        );
        navigation_.setLifecycleFeedback(feedback);
        return true;
    }
    if (sourceAudition && !session.allows(
            ProjectModulatorSourceSessionCapability::EDIT_SOURCE
        )) {
        navigation_.setLifecycleFeedback(FEEDBACK_SHARED_SOURCE_READ_ONLY);
        return true;
    }
    if (item == Item::RECORD) {
        // Recording is an explicit hold + RAW-turn gesture. Merely moving OPT
        // without the modifier must never mutate the durable curve.
        return true;
    }
    if (item == Item::LENGTH) {
        const uint8_t beats = static_cast<uint8_t>(
            normalizedToIndex(value, 64) + 1
        );
        return resizeFocusedRecordedShape(beats);
    }
    if (item == Item::ENABLED) {
        const bool enabled = value >= 0.5f;
        if (macro_history_.setProjectModulatorEnabled(
                pages_, source->id, enabled
            )) {
            publishModulatorMutation(false);
        }
        return true;
    }
    if (source->kind == ModulatorKind::ADSR) {
        auto parameters = source->parameters.adsr;
        const auto timing = modulatorAdsrTiming(parameters.traits);
        switch (item) {
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
                const uint16_t count = envelope_parameter::durationCount(
                    timing,
                    parameter
                );
                (void)setModulatorEnvelopeDuration(
                    parameters,
                    parameter,
                    envelope_parameter::durationAt(
                        static_cast<uint16_t>(normalizedToIndex(value, count)),
                        timing,
                        parameter
                    )
                );
                break;
            }
            case Item::SUSTAIN:
                parameters.sustainQ15 = static_cast<uint16_t>(std::lround(
                    value * static_cast<float>(
                        PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15
                    )
                ));
                break;
            case Item::TIMING: {
                const auto next = value >= 0.5f
                    ? ModulatorTimingMode::FREE
                    : ModulatorTimingMode::SYNC;
                if (next == timing) return true;
                const auto previous = timing;
                constexpr ModulatorEnvelopeTimeParameter temporal[]{
                    ModulatorEnvelopeTimeParameter::DELAY,
                    ModulatorEnvelopeTimeParameter::ATTACK,
                    ModulatorEnvelopeTimeParameter::HOLD,
                    ModulatorEnvelopeTimeParameter::DECAY,
                    ModulatorEnvelopeTimeParameter::RELEASE,
                    ModulatorEnvelopeTimeParameter::SMOOTH,
                };
                for (const auto parameter : temporal) {
                    const uint16_t current = modulatorEnvelopeDuration(
                        parameters,
                        parameter
                    );
                    const uint16_t converted = convertEnvelopeDurationTiming(
                        current,
                        previous,
                        next,
                        parameter,
                        modulatorAdsrFeel(parameters.traits, parameter),
                        status_bar_.tempo.get()
                    );
                    (void)setModulatorEnvelopeDuration(
                        parameters,
                        parameter,
                        converted
                    );
                }
                parameters.traits = withModulatorAdsrTiming(
                    parameters.traits,
                    next
                );
                break;
            }
            case Item::RESPONSE:
                parameters.traits = withModulatorAdsrCurve(
                    parameters.traits,
                    static_cast<ModulatorAdsrCurve>(
                        normalizedToIndex(value, 3)
                    )
                );
                break;
            case Item::RETRIGGER:
                parameters.traits = withModulatorAdsrRetrigger(
                    parameters.traits,
                    static_cast<ModulatorAdsrRetriggerMode>(
                        normalizedToIndex(value, 2)
                    )
                );
                break;
            default:
                return false;
        }
        if (provisional && std::memcmp(
                &source->parameters.adsr,
                &parameters,
                sizeof(parameters)
            ) != 0) {
            source->parameters.adsr = parameters;
            pages_.control.markAuthoredMutation();
            refreshModulatorPreview(false);
        } else if (!provisional &&
                   macro_history_.setProjectAdsrParametersCoalesced(
                       pages_, source->id, parameters
                   )) {
            publishModulatorMutation(false);
        }
        return true;
    }
    if (source->kind != ModulatorKind::LFO) return false;

    auto parameters = source->parameters.lfo;
    switch (item) {
        case Item::SHAPE:
            parameters.shape = static_cast<ModulatorLfoShape>(
                normalizedToIndex(
                    value,
                    lfo_parameter::SHAPE_COUNT
                )
            );
            break;
        case Item::RATE:
            if (parameters.timing == ModulatorTimingMode::FREE) {
                parameters.freePeriodMs = PROJECT_MODULATOR_FREE_PERIODS_MS[
                    static_cast<size_t>(normalizedToIndex(
                        value,
                        static_cast<int>(PROJECT_MODULATOR_FREE_PERIODS_MS.size())
                    ))
                ];
            } else {
                parameters.periodTicks =
                    lfo_parameter::ratePeriodTicks(
                        static_cast<uint8_t>(normalizedToIndex(
                            value,
                            lfo_parameter::RATE_COUNT
                        ))
                    );
            }
            break;
        case Item::TIMING:
            parameters.timing = value >= 0.5f
                ? ModulatorTimingMode::FREE
                : ModulatorTimingMode::SYNC;
            break;
        case Item::PHASE: {
            const int32_t phase = static_cast<int32_t>(value * 65534.0f + 0.5f) -
                32767;
            parameters.phaseQ15 = static_cast<int16_t>(
                std::clamp<int32_t>(phase, -32767, 32767)
            );
            break;
        }
        case Item::RETRIGGER:
            if (parameters.retrigger ==
                ModulatorRetriggerPolicy::EXPLICIT_TRIGGER) {
                return false;
            }
            parameters.retrigger = static_cast<ModulatorRetriggerPolicy>(
                normalizedToIndex(value, 2)
            );
            break;
        default:
            return false;
    }
    if (provisional && std::memcmp(
            &source->parameters.lfo,
            &parameters,
            sizeof(parameters)
        ) != 0) {
        source->parameters.lfo = parameters;
        pages_.control.markAuthoredMutation();
        refreshModulatorPreview(false);
    } else if (!provisional &&
               macro_history_.setProjectLfoParametersCoalesced(
                   pages_, source->id, parameters
               )) {
        publishModulatorMutation(false);
    }
    return true;
}

}  // namespace core::handler
