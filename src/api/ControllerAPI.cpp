#include "api/ControllerAPI.hpp"
#include "adapter/input/encoder/EncoderController.hpp"
#include "adapter/midi/TeensyUsbMidiOut.hpp"
#include "core/event/IEventBus.hpp"
#include "core/input/InputBinding.hpp"
#include "log/Macros.hpp"
#include "manager/ViewManager.hpp"

/*
 * Constructor
 */
ControllerAPI::ControllerAPI(InputBinding& bindings, IEventBus& events, TeensyUsbMidiOut& midiOut,
                             EncoderController& encoders, ViewManager& viewManager)
    : binding_service_(bindings),
      event_bus_(events),
      midi_out_(midiOut),
      encoders_(encoders),
      view_manager_(viewManager) {}

/*
 * INPUT BINDING API - Delegate to InputBinding service
 */
void ControllerAPI::onPressed(ButtonID buttonId, ActionCallback callback) {
    binding_service_.onPressed(buttonId, callback);
}

void ControllerAPI::onReleased(ButtonID buttonId, ActionCallback callback) {
    binding_service_.onReleased(buttonId, callback);
}

void ControllerAPI::onLongPress(ButtonID buttonId, ActionCallback callback, uint32_t ms) {
    binding_service_.onLongPress(buttonId, callback, ms);
}

void ControllerAPI::onDoubleTap(ButtonID buttonId, ActionCallback callback) {
    binding_service_.onDoubleTap(buttonId, callback);
}

void ControllerAPI::onCombo(ButtonID btn1, ButtonID btn2, ActionCallback callback) {
    binding_service_.onCombo(btn1, btn2, callback);
}

void ControllerAPI::onTurned(EncoderID encoderId, EncoderActionCallback callback) {
    binding_service_.onTurned(encoderId, callback);
}

void ControllerAPI::onTurnedWhilePressed(EncoderID encoderId, ButtonID buttonId,
                                         EncoderActionCallback callback) {
    binding_service_.onTurnedWhilePressed(encoderId, buttonId, callback);
}

/*
 * SCOPED INPUT BINDING API - Delegate to InputBinding service with scope
 */
void ControllerAPI::onPressed(ButtonID buttonId, ActionCallback callback, lv_obj_t* scope, bool latch) {
    binding_service_.onPressed(buttonId, std::move(callback), scope, latch);
}

void ControllerAPI::onReleased(ButtonID buttonId, ActionCallback callback, lv_obj_t* scope) {
    binding_service_.onReleased(buttonId, std::move(callback), scope);
}

void ControllerAPI::onLongPress(ButtonID buttonId, ActionCallback callback, uint32_t ms, lv_obj_t* scope) {
    binding_service_.onLongPress(buttonId, std::move(callback), ms, scope);
}

void ControllerAPI::onDoubleTap(ButtonID buttonId, ActionCallback callback, lv_obj_t* scope) {
    binding_service_.onDoubleTap(buttonId, std::move(callback), scope);
}

void ControllerAPI::onCombo(ButtonID btn1, ButtonID btn2, ActionCallback callback, lv_obj_t* scope) {
    binding_service_.onCombo(btn1, btn2, std::move(callback), scope);
}

void ControllerAPI::onTurned(EncoderID encoderId, EncoderActionCallback callback, lv_obj_t* scope) {
    binding_service_.onTurned(encoderId, std::move(callback), scope);
}

void ControllerAPI::onTurnedWhilePressed(EncoderID encoderId, ButtonID buttonId,
                                         EncoderActionCallback callback, lv_obj_t* scope) {
    binding_service_.onTurnedWhilePressed(encoderId, buttonId, std::move(callback), scope);
}

void ControllerAPI::clearScope(lv_obj_t* scope) {
    binding_service_.clearScope(scope);
}

bool ControllerAPI::isLatched(ButtonID btn) const {
    return binding_service_.isLatched(btn);
}

void ControllerAPI::setLatch(ButtonID btn, bool latched) {
    binding_service_.setLatch(btn, latched);
}

/*
 * ENCODER CONTROL API - Control the hardware
 */
void ControllerAPI::setEncoderPosition(EncoderID encoderId, float normalizedValue) {
    encoders_.resetEncoderPosition(encoderId, normalizedValue);
}

void ControllerAPI::setEncoderDiscreteSteps(EncoderID encoderId, uint8_t steps) {
    encoders_.setDiscreteSteps(encoderId, steps);
}

void ControllerAPI::setEncoderContinuous(EncoderID encoderId) {
    encoders_.setContinuous(encoderId);
}

void ControllerAPI::setEncoderMode(EncoderID encoderId, Hardware::EncoderMode mode) {
    encoders_.setMode(encoderId, mode);
}

void ControllerAPI::setEncoderBounds(EncoderID encoderId, float min, float max) {
    encoders_.setBounds(encoderId, min, max);
}

void ControllerAPI::setEncoderDelta(EncoderID encoderId, float delta) {
    encoders_.setDelta(encoderId, delta);
}

/*
 * SEND API - MIDI output
 */
void ControllerAPI::sendSysEx(const uint8_t* data, size_t length) {
    midi_out_.sendSysEx(data, length);
}

void ControllerAPI::sendCC(uint8_t channel, uint8_t cc, uint8_t value) {
    midi_out_.sendControlChange(channel, cc, value);
}

void ControllerAPI::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    midi_out_.sendNoteOn(channel, note, velocity);
}

void ControllerAPI::sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    midi_out_.sendNoteOff(channel, note, velocity);
}

/*
 * VIEW MANAGEMENT API - Delegate to ViewManager
 */
lv_obj_t* ControllerAPI::getParentContainer() {
    return view_manager_.getPluginContainer();
}

void ControllerAPI::showPluginView(IView& view) {
    view_manager_.showPluginView(view);
}

void ControllerAPI::hidePluginView() {
    view_manager_.hidePluginView();
}

/*
 * LOGGING API - Debug output
 */
void ControllerAPI::log(const char* message) {
    LOGLN(message);
}
