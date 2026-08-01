#include "SequencerMacroPropertyHandler.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>

#include "SequencerInputUtils.hpp"
#include "SequencerInteractionPolicyAdapter.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"

namespace core::handler {
namespace input_utils = core::handler::sequencer::input_utils;
namespace interaction_policy = core::handler::sequencer::interaction_policy;

namespace {

inline oc::type::IsActiveFn canEditSequencerProperty(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus
) {
    return [&overlays, &sequencer, &trackUi, &navigationFocus]() {
        const auto policy = interaction_policy::build(
            sequencer,
            trackUi,
            navigationFocus.get(),
            overlays.hasVisible()
        );
        return interaction_policy::canEditVisibleStepProperty(policy) ||
               interaction_policy::canEditMusicalPropertyVariation(policy);
    };
}

inline oc::type::IsActiveFn canQuickEditFocusedStep(
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays,
    core::state::sequencer::SequencerState& sequencer,
    core::state::TrackNavigationState& trackUi,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus
) {
    return [&overlays, &sequencer, &trackUi, &navigationFocus]() {
        const auto policy = interaction_policy::build(
            sequencer,
            trackUi,
            navigationFocus.get(),
            overlays.hasVisible()
        );
        return interaction_policy::canEditStepProperty(policy);
    };
}

FLASHMEM uint8_t currentNodeLocalVariationRange(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    core::state::sequencer::StepProperty property
) {
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    if (graph == nullptr) {
        return 0;
    }

    const auto* node = graph->stepNode(nodeId);
    if (node == nullptr) {
        return 0;
    }

    return core::state::sequencer::nodeLocalVariationRange(*node, property);
}

FLASHMEM core::state::sequencer::SequencerCoalescedPatternPayloadPlan
activeContentPayloadPlan(
    const core::state::sequencer::SequencerState& sequencer
) {
    return core::state::sequencer::isRootContentView(sequencer)
        ? core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly
        : core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload;
}

}  // namespace

FLASHMEM SequencerMacroPropertyHandler::SequencerMacroPropertyHandler(
    StateRefs state,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId,
    NowProvider nowProvider
)
    : overlays_(state.overlays)
    , sequencer_(state.sequencer)
    , track_bank_(state.trackBank)
    , track_ui_(state.trackNavigation)
    , navigation_focus_(state.navigationFocus)
    , history_(state.history)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId)
    , now_provider_(nowProvider) {
    setupBindings();
}

FLASHMEM void SequencerMacroPropertyHandler::setupBindings() {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope_id_)
            .when(canEditSequencerProperty(overlays_, sequencer_, track_ui_, navigation_focus_))
            .then([this, i](float value) { handleTurn(i, value); });
    }

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when(canQuickEditFocusedStep(overlays_, sequencer_, track_ui_, navigation_focus_))
        .then([this](float value) { handleFocusedStepTurn(value); });
}

FLASHMEM void SequencerMacroPropertyHandler::handleTurn(uint8_t indexInPage, float normalized) {
    uint8_t abs = 0;
    if (!core::state::sequencer::resolveActiveContentStepInPage(
            sequencer_,
            sequencer_.page.get(),
            indexInPage,
            abs
        )) {
        return;
    }
    const auto property = sequencer_.activeStepProperty.get();
    const uint32_t now = now_provider_ ? now_provider_() : 0;

    const bool propertySelectorLocalVariationLayer =
        sequencer_.stepPropertyInlineSelector.selecting.get() &&
        buttons_.isPressed(Config::ButtonID::LEFT_BOTTOM);

    if (propertySelectorLocalVariationLayer) {
        if (!core::state::sequencer::stepPropertySupportsLocalVariation(property)) {
            return;
        }
        const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer_, abs);
        const uint8_t range = input_utils::normalizedToVariationRange(property, normalized);
        if (currentNodeLocalVariationRange(sequencer_, abs, property) == range) {
            return;
        }

        if (!history_.beginCoalescedPatternEdit(
                abs,
                property,
                now,
                core::state::sequencer::
                    SequencerCoalescedPatternPayloadPlan::FullWithProspectiveGraph
            )) {
            return;
        }
        // Arm the authored-value projection before the graph revision is
        // published. The first redraw must not reuse previous runtime
        // variation telemetry for this direct edit.
        sequencer_.stepInlineFeedback.show(abs, property, now);
        const bool changed = core::state::sequencer::setNodeLocalVariationRange(
            sequencer_.pattern,
            nodeId,
            property,
            range
        );
        if (!history_.sealCoalescedPatternEdit(changed)) return;
        if (!changed) return;

        auto& selector = sequencer_.stepPropertyInlineSelector;
        selector.macroLocalVariationEditActive.set(true);
        selector.localVariationStepIndex = abs;
        sequencer_.invalidateVariationTelemetry();
        return;
    }

    if (sequencer_.stepPropertyInlineSelector.selecting.get()) {
        return;
    }

    if (sequencer_.stepStatePropertyActive.get()) {
        if (!history_.beginCoalescedPatternEdit(
                abs,
                core::state::sequencer::StepProperty::NOTE,
                now,
                activeContentPayloadPlan(sequencer_),
                true
            )) {
            return;
        }
        const bool changed = core::state::sequencer::setActiveContentStepEnabled(
            sequencer_,
            abs,
            normalized >= 0.5f
        );
        if (!history_.sealCoalescedPatternEdit(changed)) return;
        return;
    }

    if (!history_.beginCoalescedPatternEdit(
            abs,
            property,
            now,
            activeContentPayloadPlan(sequencer_)
        )) {
        return;
    }
    sequencer_.stepInlineFeedback.show(abs, property, now);
    const bool changed = core::state::sequencer::setActiveContentStepFromNormalized(
        sequencer_,
        abs,
        property,
        normalized,
        sequencer_.pattern.pitchEditMode,
        core::state::sequencer::resolveEffectiveScaleSettings(
            track_bank_.projectScaleSettings(),
            sequencer_.pattern.scalePolicy,
            sequencer_.pattern.scaleOverride
        )
    );
    if (!history_.sealCoalescedPatternEdit(changed)) return;
}

FLASHMEM void SequencerMacroPropertyHandler::handleFocusedStepTurn(float normalized) {
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer_);
    if (len == 0) return;

    const uint8_t abs = std::min<uint8_t>(
        sequencer_.focusedStep.get(),
        static_cast<uint8_t>(len - 1U)
    );
    const auto property = sequencer_.activeStepProperty.get();
    const uint32_t now = now_provider_ ? now_provider_() : 0;

    if (sequencer_.stepStatePropertyActive.get()) {
        if (!history_.beginCoalescedPatternEdit(
                abs,
                core::state::sequencer::StepProperty::NOTE,
                now,
                activeContentPayloadPlan(sequencer_),
                true
            )) {
            return;
        }
        const bool changed = core::state::sequencer::setActiveContentStepEnabled(
            sequencer_,
            abs,
            normalized >= 0.5f
        );
        if (!history_.sealCoalescedPatternEdit(changed)) return;
        return;
    }

    if (!history_.beginCoalescedPatternEdit(
            abs,
            property,
            now,
            activeContentPayloadPlan(sequencer_)
        )) {
        return;
    }
    sequencer_.stepInlineFeedback.show(abs, property, now);
    const bool changed = core::state::sequencer::setActiveContentStepFromNormalized(
        sequencer_,
        abs,
        property,
        normalized,
        sequencer_.pattern.pitchEditMode,
        core::state::sequencer::resolveEffectiveScaleSettings(
            track_bank_.projectScaleSettings(),
            sequencer_.pattern.scalePolicy,
            sequencer_.pattern.scaleOverride
        )
    );
    if (!history_.sealCoalescedPatternEdit(changed)) return;
}

}  // namespace core::handler
