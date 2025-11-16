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
    : bindingService_(bindings),
      eventBus_(events),
      midiOut_(midiOut),
      encoders_(encoders),
      viewManager_(viewManager) {}

/*
 * INPUT BINDING API - Delegate to InputBinding service
 */
void ControllerAPI::onPressed(ButtonID buttonId, ActionCallback callback) {
    bindingService_.onPressed(buttonId, callback);
}

void ControllerAPI::onReleased(ButtonID buttonId, ActionCallback callback) {
    bindingService_.onReleased(buttonId, callback);
}

void ControllerAPI::onLongPress(ButtonID buttonId, ActionCallback callback, uint32_t ms) {
    bindingService_.onLongPress(buttonId, callback, ms);
}

void ControllerAPI::onDoubleTap(ButtonID buttonId, ActionCallback callback) {
    bindingService_.onDoubleTap(buttonId, callback);
}

void ControllerAPI::onCombo(ButtonID btn1, ButtonID btn2, ActionCallback callback) {
    bindingService_.onCombo(btn1, btn2, callback);
}

void ControllerAPI::onTurned(EncoderID encoderId, EncoderActionCallback callback) {
    bindingService_.onTurned(encoderId, callback);
}

void ControllerAPI::onTurnedWhilePressed(EncoderID encoderId, ButtonID buttonId,
                                         EncoderActionCallback callback) {
    bindingService_.onTurnedWhilePressed(encoderId, buttonId, callback);
}

/*
 * SCOPED INPUT BINDING API - Delegate to InputBinding service with scope
 */
void ControllerAPI::onPressed(ButtonID buttonId, ActionCallback callback, lv_obj_t* scope) {
    bindingService_.onPressed(buttonId, std::move(callback), scope);
}

void ControllerAPI::onReleased(ButtonID buttonId, ActionCallback callback, lv_obj_t* scope) {
    bindingService_.onReleased(buttonId, std::move(callback), scope);
}

void ControllerAPI::onLongPress(ButtonID buttonId, ActionCallback callback, uint32_t ms, lv_obj_t* scope) {
    bindingService_.onLongPress(buttonId, std::move(callback), ms, scope);
}

void ControllerAPI::onDoubleTap(ButtonID buttonId, ActionCallback callback, lv_obj_t* scope) {
    bindingService_.onDoubleTap(buttonId, std::move(callback), scope);
}

void ControllerAPI::onCombo(ButtonID btn1, ButtonID btn2, ActionCallback callback, lv_obj_t* scope) {
    bindingService_.onCombo(btn1, btn2, std::move(callback), scope);
}

void ControllerAPI::onTurned(EncoderID encoderId, EncoderActionCallback callback, lv_obj_t* scope) {
    bindingService_.onTurned(encoderId, std::move(callback), scope);
}

void ControllerAPI::onTurnedWhilePressed(EncoderID encoderId, ButtonID buttonId,
                                         EncoderActionCallback callback, lv_obj_t* scope) {
    bindingService_.onTurnedWhilePressed(encoderId, buttonId, std::move(callback), scope);
}

void ControllerAPI::clearScope(lv_obj_t* scope) {
    bindingService_.clearScope(scope);
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

/*
 * SEND API - MIDI output
 */
void ControllerAPI::sendSysEx(const uint8_t* data, size_t length) {
    midiOut_.sendSysEx(data, length);
}

void ControllerAPI::sendCC(uint8_t channel, uint8_t cc, uint8_t value) {
    midiOut_.sendControlChange(channel, cc, value);
}

void ControllerAPI::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    midiOut_.sendNoteOn(channel, note, velocity);
}

void ControllerAPI::sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    midiOut_.sendNoteOff(channel, note, velocity);
}

/*
 * VIEW MANAGEMENT API - Delegate to ViewManager
 */
lv_obj_t* ControllerAPI::getParentContainer() {
    return viewManager_.getPluginContainer();
}

void ControllerAPI::showPluginView(UI::IView& view) {
    viewManager_.showPluginView(view);
}

void ControllerAPI::hidePluginView() {
    viewManager_.hidePluginView();
}

/*
 * LOGGING API - Debug output
 */
void ControllerAPI::log(const char* message) {
    LOGLN(message);
}
