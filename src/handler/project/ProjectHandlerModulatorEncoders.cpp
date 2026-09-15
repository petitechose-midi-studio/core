#include "handler/project/ProjectHandlerInternals.hpp"

namespace core::handler {

using namespace project_handler_internal;
using namespace core::state::modulation;
namespace modulators = core::state::project::modulators;

FLASHMEM bool ProjectHandler::directModulatorInputActive() const {
    using Phase = core::state::contextual::GuardedActionPhase;
    const auto guarding = [](Phase phase) {
        return phase != Phase::IDLE && phase != Phase::CANCELLED;
    };
    return active_view_.get() == core::ui::ViewType::MODULATORS &&
        navigation_.currentNode.get() == project::ProjectNodeId::MODULATOR_SOURCE_DETAIL &&
        regularProjectInputActive() && !recorded_shape_capture_button_active_ &&
        !guarding(navigation_.modulatorGuard.get().phase) &&
        !guarding(navigation_.modulatorClipboardGuard.get().phase) &&
        !buttons_.isPressed(ButtonID::NAV) &&
        !buttons_.isPressed(ButtonID::LEFT_CENTER) &&
        !buttons_.isPressed(ButtonID::LEFT_TOP) &&
        !buttons_.isPressed(ButtonID::BOTTOM_LEFT) &&
        !buttons_.isPressed(ButtonID::BOTTOM_RIGHT);
}

FLASHMEM bool ProjectHandler::syncModulatorEncoders(bool force) {
    const auto* source = directModulatorInputActive() ? focusedModulator() : nullptr;
    const auto session = source
        ? resolveProjectModulatorSourceSession(pages_.control, source->id)
        : ProjectModulatorSourceSessionDescriptor{};
    core::state::macro::MacroAutomationSlotAddress address{};
    if (!source || (source->kind != ModulatorKind::LFO && source->kind != ModulatorKind::ADSR) ||
        !session.valid() || (session.audition() && !modulatorAuditionAddress(address))) {
        if (valid(direct_source_)) macro_history_.endCoalescing();
        direct_source_ = {};
        return false;
    }
    const uint32_t generation = session.audition() ? pages_.control.audition.generation : 0U;
    const bool ownerChanged = direct_source_ != source->id ||
        direct_binding_ != session.bindingId || direct_generation_ != generation;
    if (!force && !ownerChanged && direct_revision_ == pages_.control.authoredRevision) return false;
    if (ownerChanged) macro_history_.endCoalescing();
    for (uint8_t index = 0U; index < Config::MACRO_COUNT; ++index) {
        const auto target = modulators::sourceMainEncoderTarget(source->kind, session, index);
        if (target.editable) {
            syncModulatorItemEncoder(Config::MACRO_ENCODERS[index], target.item);
        } else {
            configureProjectEncoder(encoders_, Config::MACRO_ENCODERS[index], 1, 0.0f);
        }
    }
    direct_source_ = source->id;
    direct_binding_ = session.bindingId;
    direct_generation_ = generation;
    direct_revision_ = pages_.control.authoredRevision;
    return true;
}

FLASHMEM void ProjectHandler::setDirectModulatorValue(uint8_t index, float value) {
    // Input is normalized by the physical encoder before dispatch. If ownership
    // or the value changed without a sync, consume that stale event and rebase.
    if (syncModulatorEncoders() || !valid(direct_source_)) return;
    const auto* source = focusedModulator();
    if (!source) return;
    const auto session = resolveProjectModulatorSourceSession(pages_.control, source->id);
    const auto target = modulators::sourceMainEncoderTarget(source->kind, session, index);
    if (!target.editable) return;
    if (navigation_.focusedRow.get() != target.row) macro_history_.endCoalescing();
    const uint32_t beforeRevision = pages_.control.authoredRevision;
    if (!setModulatorItemValue(target.item, value)) {
        syncModulatorItemEncoder(Config::MACRO_ENCODERS[index], target.item);
        return;
    }
    if (target.item == modulators::SourceDetailItem::TIMING &&
        beforeRevision != pages_.control.authoredRevision) {
        syncModulatorItemEncoder(Config::EncoderID::MACRO_2, modulators::SourceDetailItem::RATE);
    }
    // Preserve sub-step travel and the quantizer of every turning encoder.
    // Resetting the bank on each emitted value would repeatedly snap to the
    // previous detent and prevent a physical knob from crossing its threshold.
    direct_revision_ = pages_.control.authoredRevision;
    navigation_.focusedRow.set(target.row);
    if (!session.audition()) navigation_.clearLifecycleFeedback();
    syncFocusedEncoder(false);
}

}  // namespace core::handler
