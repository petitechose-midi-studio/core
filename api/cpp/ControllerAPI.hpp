#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config/InputID.hpp"
#include "log/Macros.hpp"

typedef struct _lv_obj_t lv_obj_t;

using ::ButtonID;
using ::EncoderID;

namespace Theme {
namespace Color {}
}  // namespace Theme

class EncoderController;
class IEventBus;
class InputBinding;
class TeensyUsbMidiOut;
class ViewManager;

namespace UI {
class IView;
}

namespace Plugins {
class MidiOut;
}  // namespace Plugins

/**
 * @brief Controller API - Facade for plugin-to-controller communication
 *
 * This class provides a clean, abstract API for plugins to interact with
 * the MIDI controller hardware without direct coupling to core services.
 *
 * Design Pattern: Facade
 * Purpose: Decouple plugins from core implementation details
 *
 * Responsibilities:
 * - LISTEN: React to controller events (buttons, encoders) via on()
 * - SET: Send values to controller (encoders, LEDs, display) via set*()
 * - SEND: Send MIDI messages out via sendMIDI()
 * - EVENTS: Subscribe to system events via subscribe()
 */
class ControllerAPI {
public:
    using ActionCallback = std::function<void()>;
    using EncoderActionCallback = std::function<void(float normalizedValue)>;

    ControllerAPI(InputBinding& bindings, IEventBus& events, TeensyUsbMidiOut& midiOut,
                  EncoderController& encoders, ViewManager& viewManager);

    // ===== INPUT BINDING API - React to controller input =====

    /**
     * @brief Register callback for button press event
     * @param buttonId Button identifier
     * @param callback Action to execute on press
     */
    void onPressed(ButtonID buttonId, ActionCallback callback);

    /**
     * @brief Register callback for button release event
     * @param buttonId Button identifier
     * @param callback Action to execute on release
     */
    void onReleased(ButtonID buttonId, ActionCallback callback);

    /**
     * @brief Register callback for long press event
     * @param buttonId Button identifier
     * @param callback Action to execute on long press
     * @param ms Long press duration threshold in milliseconds (default: 500ms)
     */
    void onLongPress(ButtonID buttonId, ActionCallback callback, uint32_t ms = 500);

    /**
     * @brief Register callback for double tap event
     * @param buttonId Button identifier
     * @param callback Action to execute on double tap
     */
    void onDoubleTap(ButtonID buttonId, ActionCallback callback);

    /**
     * @brief Register callback for button combo event
     * @param btn1 First button identifier
     * @param btn2 Second button identifier
     * @param callback Action to execute when both buttons pressed
     */
    void onCombo(ButtonID btn1, ButtonID btn2, ActionCallback callback);

    /**
     * @brief Register callback for encoder turn event
     * @param encoderId Encoder identifier
     * @param callback Action to execute with normalized value (0.0-1.0)
     */
    void onTurned(EncoderID encoderId, EncoderActionCallback callback);

    /**
     * @brief Register callback for encoder turn while button pressed
     * @param encoderId Encoder identifier
     * @param buttonId Button that must be held
     * @param callback Action to execute with normalized value (0.0-1.0)
     */
    void onTurnedWhilePressed(EncoderID encoderId, ButtonID buttonId,
                              EncoderActionCallback callback);

    // ===== SCOPED INPUT BINDING API - Active only when LVGL object visible =====

    /**
     * @brief Register scoped callback for button press event
     * @param buttonId Button identifier
     * @param callback Action to execute on press
     * @param scope LVGL object (binding active only if visible)
     */
    void onPressed(ButtonID buttonId, ActionCallback callback, lv_obj_t* scope);

    /**
     * @brief Register scoped callback for button release event
     * @param buttonId Button identifier
     * @param callback Action to execute on release
     * @param scope LVGL object (binding active only if visible)
     */
    void onReleased(ButtonID buttonId, ActionCallback callback, lv_obj_t* scope);

    /**
     * @brief Register scoped callback for long press event
     * @param buttonId Button identifier
     * @param callback Action to execute on long press
     * @param ms Long press duration threshold in milliseconds
     * @param scope LVGL object (binding active only if visible)
     */
    void onLongPress(ButtonID buttonId, ActionCallback callback, uint32_t ms, lv_obj_t* scope);

    /**
     * @brief Register scoped callback for double tap event
     * @param buttonId Button identifier
     * @param callback Action to execute on double tap
     * @param scope LVGL object (binding active only if visible)
     */
    void onDoubleTap(ButtonID buttonId, ActionCallback callback, lv_obj_t* scope);

    /**
     * @brief Register scoped callback for button combo event
     * @param btn1 First button identifier
     * @param btn2 Second button identifier
     * @param callback Action to execute when both buttons pressed
     * @param scope LVGL object (binding active only if visible)
     */
    void onCombo(ButtonID btn1, ButtonID btn2, ActionCallback callback, lv_obj_t* scope);

    /**
     * @brief Register scoped callback for encoder turn event
     * @param encoderId Encoder identifier
     * @param callback Action to execute with normalized value (0.0-1.0)
     * @param scope LVGL object (binding active only if visible)
     */
    void onTurned(EncoderID encoderId, EncoderActionCallback callback, lv_obj_t* scope);

    /**
     * @brief Register scoped callback for encoder turn while button pressed
     * @param encoderId Encoder identifier
     * @param buttonId Button that must be held
     * @param callback Action to execute with normalized value (0.0-1.0)
     * @param scope LVGL object (binding active only if visible)
     */
    void onTurnedWhilePressed(EncoderID encoderId, ButtonID buttonId,
                              EncoderActionCallback callback, lv_obj_t* scope);

    /**
     * @brief Clear all bindings scoped to LVGL object
     * @param scope LVGL object
     */
    void clearScope(lv_obj_t* scope);

    // ===== MIDI INPUT API - React to incoming MIDI messages =====

    /**
     * @brief Register callback for incoming SysEx messages
     * @param callback Function to execute when SysEx received
     *
     * Callback signature: void(const uint8_t* data, uint16_t length)
     * Plugin should filter by manufacturer ID/protocol inside callback
     */
    template <typename Callback>
    void onSysEx(Callback callback);

    /**
     * @brief Register callback for incoming Control Change messages
     * @param callback Function to execute when CC received
     *
     * Callback signature: void(uint8_t channel, uint8_t controller, uint8_t value)
     */
    template <typename Callback>
    void onCC(Callback callback);

    /**
     * @brief Register callback for incoming Note On messages
     * @param callback Function to execute when Note On received
     *
     * Callback signature: void(uint8_t channel, uint8_t note, uint8_t velocity)
     */
    template <typename Callback>
    void onNoteOn(Callback callback);

    /**
     * @brief Register callback for incoming Note Off messages
     * @param callback Function to execute when Note Off received
     *
     * Callback signature: void(uint8_t channel, uint8_t note, uint8_t velocity)
     */
    template <typename Callback>
    void onNoteOff(Callback callback);

    // ===== ENCODER CONTROL API - Control hardware encoders =====

    /**
     * @brief Reset encoder position to match external value
     * @param encoderId Encoder input ID
     * @param normalizedValue Value between 0.0 and 1.0
     *
     * Use case: Sync encoder with DAW parameter value
     */
    void setEncoderPosition(EncoderID encoderId, float normalizedValue);

    /**
     * @brief Configure encoder for discrete value steps
     * @param encoderId Encoder input ID
     * @param steps Number of discrete values (e.g., 4 for button with 4 states)
     *
     * Encoder will only emit events at discrete steps (0.0, 0.33, 0.67, 1.0 for 4 steps)
     * Reduces MIDI traffic for discrete parameters (buttons, lists)
     */
    void setEncoderDiscreteSteps(EncoderID encoderId, uint8_t steps);

    /**
     * @brief Configure encoder for continuous values
     * @param encoderId Encoder input ID
     *
     * Encoder will emit events for all value changes (default mode for knobs)
     */
    void setEncoderContinuous(EncoderID encoderId);

    // ===== SEND API - MIDI output =====

    /**
     * @brief Send SysEx message via MIDI out
     * @param data SysEx data buffer
     * @param length Data length in bytes
     */
    void sendSysEx(const uint8_t* data, size_t length);

    /**
     * @brief Send control change message
     * @param channel MIDI channel (0-15)
     * @param cc Control change number
     * @param value CC value (0-127)
     */
    void sendCC(uint8_t channel, uint8_t cc, uint8_t value);

    /**
     * @brief Send Note On message
     * @param channel MIDI channel (0-15)
     * @param note MIDI note number (0-127)
     * @param velocity Note velocity (0-127)
     */
    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);

    /**
     * @brief Send Note Off message
     * @param channel MIDI channel (0-15)
     * @param note MIDI note number (0-127)
     * @param velocity Note velocity (0-127)
     */
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);

    // ===== VIEW MANAGEMENT API - Display plugin views =====

    /**
     * @brief Get plugin screen for creating plugin UI
     * @return LVGL screen (pluginScreen_) managed by Core ViewManager
     *
     * Plugins should create their root LVGL containers with this screen as parent.
     * The screen is owned and managed by Core (never destroyed).
     */
    lv_obj_t* getParentContainer();

    /**
     * @brief Show a plugin view (switches to pluginScreen_)
     * @param view Reference to IView implementation (plugin keeps ownership)
     */
    void showPluginView(UI::IView& view);

    /**
     * @brief Hide current plugin view and return to Core (switches to coreScreen_)
     */
    void hidePluginView();

    // ===== LOGGING API - Debug output =====

    /**
     * @brief Log a message to serial output (debug build only)
     * @param message Message to log (will add newline)
     */
    void log(const char* message);

    /**
     * @brief Log a formatted message to serial output (debug build only)
     * @param format Printf-style format string
     * @param ... Variable arguments for formatting
     */
    template <typename... Args>
    void logf(const char* format, Args... args) {
        LOGF(format, args...);
    }

private:
    InputBinding& bindingService_;
    IEventBus& eventBus_;
    TeensyUsbMidiOut& midiOut_;
    EncoderController& encoders_;
    ViewManager& viewManager_;
};

// ===== TEMPLATE IMPLEMENTATIONS =====

#include "core/event/Events.hpp"
#include "core/event/IEventBus.hpp"
#include "core/event/UnifiedEventTypes.hpp"

template <typename Callback>
void ControllerAPI::onSysEx(Callback callback) {
    eventBus_.on(EventCategory::MIDI, MidiEvent::SysEx, [callback](const Event& e) {
        auto& sysex = static_cast<const SysExEvent&>(e);
        callback(sysex.data, sysex.length);
    });
}

template <typename Callback>
void ControllerAPI::onCC(Callback callback) {
    eventBus_.on(EventCategory::MIDI, MidiEvent::CC, [callback](const Event& e) {
        auto& cc = static_cast<const MidiCCEvent&>(e);
        callback(cc.channel, cc.controller, cc.value);
    });
}

template <typename Callback>
void ControllerAPI::onNoteOn(Callback callback) {
    eventBus_.on(EventCategory::MIDI, MidiEvent::NoteOn, [callback](const Event& e) {
        auto& note = static_cast<const MidiNoteOnEvent&>(e);
        callback(note.channel, note.note, note.velocity);
    });
}

template <typename Callback>
void ControllerAPI::onNoteOff(Callback callback) {
    eventBus_.on(EventCategory::MIDI, MidiEvent::NoteOff, [callback](const Event& e) {
        auto& note = static_cast<const MidiNoteOffEvent&>(e);
        callback(note.channel, note.note, note.velocity);
    });
}
