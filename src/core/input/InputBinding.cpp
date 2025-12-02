#include "InputBinding.hpp"

#include <lvgl.h>

#include "config/System.hpp"
#include "event/Events.hpp"
#include "log/Macros.hpp"

InputBinding::InputBinding(IEventBus& eventBus) : event_bus_(eventBus) {
    encoder_sub_ = event_bus_.on(EventCategory::USER_INPUT, InputEvent::ENCODER_CHANGED,
                                 [this](const Event& e) { onEncoderChanged(e); });

    button_press_sub_ = event_bus_.on(EventCategory::USER_INPUT, InputEvent::BUTTON_PRESS,
                                      [this](const Event& e) { onButtonPress(e); });

    button_release_sub_ = event_bus_.on(EventCategory::USER_INPUT, InputEvent::BUTTON_RELEASE,
                                        [this](const Event& e) { onButtonRelease(e); });

    LOGLN("[InputBinding] Initialized with direct type-safe API");
}

InputBinding::~InputBinding() {
    event_bus_.off(encoder_sub_);
    event_bus_.off(button_press_sub_);
    event_bus_.off(button_release_sub_);
    LOGLN("[InputBinding] Destroyed");
}

void InputBinding::onPressed(ButtonID id, ActionCallback cb) {
    button_bindings_.push_back(
        {.type = ButtonBindingType::PRESS, .buttonId = id, .action = std::move(cb)});
    LOGF("[InputBinding] Added PRESS binding for ButtonID %d\n", static_cast<int>(id));
}

void InputBinding::onReleased(ButtonID id, ActionCallback cb) {
    button_bindings_.push_back(
        {.type = ButtonBindingType::RELEASE, .buttonId = id, .action = std::move(cb)});
    LOGF("[InputBinding] Added RELEASE binding for ButtonID %d\n", static_cast<int>(id));
}

void InputBinding::onLongPress(ButtonID id, ActionCallback cb, uint32_t ms) {
    button_bindings_.push_back({.type = ButtonBindingType::LONG_PRESS,
                                .buttonId = id,
                                .longPressMs = ms,
                                .action = std::move(cb)});
    LOGF("[InputBinding] Added LONG_PRESS binding for ButtonID %d (%dms)\n", static_cast<int>(id),
         ms);
}

void InputBinding::onDoubleTap(ButtonID id, ActionCallback cb) {
    button_bindings_.push_back(
        {.type = ButtonBindingType::DOUBLE_TAP, .buttonId = id, .action = std::move(cb)});
    LOGF("[InputBinding] Added DOUBLE_TAP binding for ButtonID %d\n", static_cast<int>(id));
}

void InputBinding::onCombo(ButtonID btn1, ButtonID btn2, ActionCallback cb) {
    button_bindings_.push_back({.type = ButtonBindingType::COMBO,
                                .buttonId = btn1,
                                .secondaryButton = btn2,
                                .action = std::move(cb)});
    LOGF("[InputBinding] Added COMBO binding for ButtonID %d + %d\n", static_cast<int>(btn1),
         static_cast<int>(btn2));
}

void InputBinding::onTurned(EncoderID id, EncoderActionCallback cb) {
    encoder_bindings_.push_back(
        {.type = EncoderBindingType::TURN, .encoderId = id, .action = std::move(cb)});
    LOGF("[InputBinding] Added TURN binding for EncoderID %d\n", static_cast<int>(id));
}

void InputBinding::onTurnedWhilePressed(EncoderID encoderId, ButtonID buttonId,
                                        EncoderActionCallback cb) {
    encoder_bindings_.push_back({.type = EncoderBindingType::TURN_WHILE_PRESSED,
                                 .encoderId = encoderId,
                                 .requiredButton = buttonId,
                                 .action = std::move(cb)});
    LOGF(
        "[InputBinding] Added TURN_WHILE_PRESSED binding for EncoderID %d (requires ButtonID %d)\n",
        static_cast<int>(encoderId), static_cast<int>(buttonId));
}

void InputBinding::onPressed(ButtonID id, ActionCallback cb, lv_obj_t* scope, bool latch) {
    button_bindings_.push_back({.type = ButtonBindingType::PRESS,
                                .buttonId = id,
                                .action = std::move(cb),
                                .latch = latch,
                                .scope = scope});
    LOGF("[InputBinding] Added SCOPED PRESS binding for ButtonID %d (scope: %p, latch: %d)\n",
         static_cast<int>(id), scope, latch);
}

void InputBinding::onReleased(ButtonID id, ActionCallback cb, lv_obj_t* scope) {
    button_bindings_.push_back({.type = ButtonBindingType::RELEASE,
                                .buttonId = id,
                                .action = std::move(cb),
                                .scope = scope});
    LOGF("[InputBinding] Added SCOPED RELEASE binding for ButtonID %d (scope: %p)\n",
         static_cast<int>(id), scope);
}

void InputBinding::onLongPress(ButtonID id, ActionCallback cb, uint32_t ms, lv_obj_t* scope) {
    button_bindings_.push_back({.type = ButtonBindingType::LONG_PRESS,
                                .buttonId = id,
                                .longPressMs = ms,
                                .action = std::move(cb),
                                .scope = scope});
    LOGF("[InputBinding] Added SCOPED LONG_PRESS binding for ButtonID %d (%dms, scope: %p)\n",
         static_cast<int>(id), ms, scope);
}

void InputBinding::onDoubleTap(ButtonID id, ActionCallback cb, lv_obj_t* scope) {
    button_bindings_.push_back({.type = ButtonBindingType::DOUBLE_TAP,
                                .buttonId = id,
                                .action = std::move(cb),
                                .scope = scope});
    LOGF("[InputBinding] Added SCOPED DOUBLE_TAP binding for ButtonID %d (scope: %p)\n",
         static_cast<int>(id), scope);
}

void InputBinding::onCombo(ButtonID btn1, ButtonID btn2, ActionCallback cb, lv_obj_t* scope) {
    button_bindings_.push_back({.type = ButtonBindingType::COMBO,
                                .buttonId = btn1,
                                .secondaryButton = btn2,
                                .action = std::move(cb),
                                .scope = scope});
    LOGF("[InputBinding] Added SCOPED COMBO binding for ButtonID %d + %d (scope: %p)\n",
         static_cast<int>(btn1), static_cast<int>(btn2), scope);
}

void InputBinding::onTurned(EncoderID id, EncoderActionCallback cb, lv_obj_t* scope) {
    encoder_bindings_.push_back({.type = EncoderBindingType::TURN,
                                 .encoderId = id,
                                 .action = std::move(cb),
                                 .scope = scope});
    LOGF("[InputBinding] Added SCOPED TURN binding for EncoderID %d (scope: %p)\n",
         static_cast<int>(id), scope);
}

void InputBinding::onTurnedWhilePressed(EncoderID encoderId, ButtonID buttonId,
                                        EncoderActionCallback cb, lv_obj_t* scope) {
    encoder_bindings_.push_back({.type = EncoderBindingType::TURN_WHILE_PRESSED,
                                 .encoderId = encoderId,
                                 .requiredButton = buttonId,
                                 .action = std::move(cb),
                                 .scope = scope});
    LOGF(
        "[InputBinding] Added SCOPED TURN_WHILE_PRESSED binding for EncoderID %d (requires "
        "ButtonID %d, scope: %p)\n",
        static_cast<int>(encoderId), static_cast<int>(buttonId), scope);
}

void InputBinding::clearScope(lv_obj_t* scope) {
    auto buttonIt = button_bindings_.begin();
    while (buttonIt != button_bindings_.end()) {
        if (buttonIt->scope == scope) {
            buttonIt = button_bindings_.erase(buttonIt);
        } else {
            ++buttonIt;
        }
    }

    auto encoderIt = encoder_bindings_.begin();
    while (encoderIt != encoder_bindings_.end()) {
        if (encoderIt->scope == scope) {
            encoderIt = encoder_bindings_.erase(encoderIt);
        } else {
            ++encoderIt;
        }
    }

    LOGF("[InputBinding] Cleared all bindings for scope %p\n", scope);
}

bool InputBinding::isLatched(ButtonID btn) const {
    auto it = latch_states_.find(btn);
    return it != latch_states_.end() && it->second;
}

void InputBinding::setLatch(ButtonID btn, bool latched) { latch_states_[btn] = latched; }

void InputBinding::onEncoderChanged(const Event& event) {
    auto& evt = static_cast<const EncoderChangedEvent&>(event);
    triggerMatchingEncoderBindings(evt.encoderId, evt.normalizedValue);
}

void InputBinding::onButtonPress(const Event& event) {
    auto& evt = static_cast<const ButtonPressEvent&>(event);
    ButtonID buttonId = evt.buttonId;
    const uint32_t now = current_time_;

    button_states_[buttonId] = true;
    button_press_time_[buttonId] = now;

    if (now - button_release_time_[buttonId] < System::Input::DOUBLE_TAP_WINDOW_MS) {
        button_tap_count_[buttonId]++;
    } else {
        button_tap_count_[buttonId] = 1;
    }

    // Latch/momentary: only trigger PRESS if not already latched
    if (!latch_states_[buttonId]) {
        triggerMatchingButtonBindings(buttonId, ButtonBindingType::PRESS);
    }
}

void InputBinding::onButtonRelease(const Event& event) {
    auto& evt = static_cast<const ButtonReleaseEvent&>(event);
    ButtonID buttonId = evt.buttonId;
    const uint32_t now = current_time_;

    // Latch/momentary logic - determine state first
    const uint32_t pressDuration = now - button_press_time_[buttonId];
    const bool wasLatched = latch_states_[buttonId];

    // Update latch state BEFORE combo check (so released button isn't considered latched)
    if (wasLatched) { latch_states_[buttonId] = false; }

    // Check combos (only if button was physically pressed, not just latched)
    if (!wasLatched) { checkAndTriggerCombosOnRelease(buttonId); }

    button_states_[buttonId] = false;
    button_release_time_[buttonId] = now;
    long_press_triggered_[buttonId] = false;

    // Check if any active PRESS binding has latch flag enabled
    bool hasLatchBinding = false;
    for (const auto& binding : button_bindings_) {
        if (binding.buttonId == buttonId && binding.type == ButtonBindingType::PRESS &&
            binding.latch && binding.enabled && isBindingActive(binding)) {
            hasLatchBinding = true;
            break;
        }
    }

    // Latch behavior only if a binding requests it AND short press
    if (hasLatchBinding && !wasLatched && pressDuration < System::Input::LATCH_THRESHOLD_MS) {
        // Short tap with latch binding -> latch (no RELEASE)
        latch_states_[buttonId] = true;
    } else {
        // Normal behavior: trigger RELEASE
        triggerMatchingButtonBindings(buttonId, ButtonBindingType::RELEASE);
    }
    checkAndTriggerDoubleTap(buttonId, now);
}

bool InputBinding::triggerScopedButtonBindings(ButtonID buttonId, ButtonBindingType type) {
    bool anyTriggered = false;

    for (auto& binding : button_bindings_) {
        if (!binding.enabled) continue;
        if (binding.buttonId != buttonId) continue;
        if (binding.type != type) continue;
        if (binding.scope == nullptr) continue;   // ONLY scoped bindings
        if (!isBindingActive(binding)) continue;  // Check scope visibility

        if (binding.action) {
            binding.action();
            anyTriggered = true;
        }
    }

    return anyTriggered;
}

bool InputBinding::triggerGlobalButtonBindings(ButtonID buttonId, ButtonBindingType type) {
    bool anyTriggered = false;

    for (auto& binding : button_bindings_) {
        if (!binding.enabled) continue;
        if (binding.buttonId != buttonId) continue;
        if (binding.type != type) continue;
        if (binding.scope != nullptr) continue;  // ONLY global bindings

        if (binding.action) {
            binding.action();
            anyTriggered = true;
        }
    }

    return anyTriggered;
}

void InputBinding::triggerMatchingButtonBindings(ButtonID buttonId, ButtonBindingType type) {
    if (!bindings_enabled_) return;

    // PRIORITY 1: Try scoped bindings first
    if (triggerScopedButtonBindings(buttonId, type)) {
        // Scoped binding(s) handled it - stop propagation to globals
        return;
    }

    // PRIORITY 2: Fall back to global bindings
    triggerGlobalButtonBindings(buttonId, type);
}

bool InputBinding::triggerScopedEncoderBindings(EncoderID encoderId, float encoderValue) {
    bool anyTriggered = false;

    for (auto& binding : encoder_bindings_) {
        if (!binding.enabled) continue;
        if (binding.encoderId != encoderId) continue;
        if (binding.scope == nullptr) continue;   // ONLY scoped bindings
        if (!isBindingActive(binding)) continue;  // Check scope visibility

        // Handle TURN_WHILE_PRESSED condition (also active when button is latched)
        if (binding.type == EncoderBindingType::TURN_WHILE_PRESSED) {
            if (binding.requiredButton.has_value()) {
                ButtonID btn = *binding.requiredButton;
                bool isPressed = button_states_.count(btn) && button_states_.at(btn);
                bool isLatched = latch_states_.count(btn) && latch_states_.at(btn);
                if (!isPressed && !isLatched) {
                    continue;  // Required button not pressed/latched, skip
                }
            }
        }

        if (binding.action) {
            binding.action(encoderValue);
            anyTriggered = true;
        }
    }

    return anyTriggered;
}

bool InputBinding::triggerGlobalEncoderBindings(EncoderID encoderId, float encoderValue) {
    bool anyTriggered = false;

    for (auto& binding : encoder_bindings_) {
        if (!binding.enabled) continue;
        if (binding.encoderId != encoderId) continue;
        if (binding.scope != nullptr) continue;  // ONLY global bindings

        // Handle TURN_WHILE_PRESSED condition (also active when button is latched)
        if (binding.type == EncoderBindingType::TURN_WHILE_PRESSED) {
            if (binding.requiredButton.has_value()) {
                ButtonID btn = *binding.requiredButton;
                bool isPressed = button_states_.count(btn) && button_states_.at(btn);
                bool isLatched = latch_states_.count(btn) && latch_states_.at(btn);
                if (!isPressed && !isLatched) {
                    continue;  // Required button not pressed/latched, skip
                }
            }
        }

        if (binding.action) {
            binding.action(encoderValue);
            anyTriggered = true;
        }
    }

    return anyTriggered;
}

void InputBinding::triggerMatchingEncoderBindings(EncoderID encoderId, float encoderValue) {
    if (!bindings_enabled_) return;

    // PRIORITY 1: Try scoped bindings first
    if (triggerScopedEncoderBindings(encoderId, encoderValue)) {
        // Scoped binding(s) handled it - stop propagation to globals
        return;
    }

    // PRIORITY 2: Fall back to global bindings
    triggerGlobalEncoderBindings(encoderId, encoderValue);
}

void InputBinding::checkAndTriggerLongPress(ButtonID buttonId, uint32_t now) {
    if (!button_states_[buttonId]) return;
    if (long_press_triggered_[buttonId]) return;

    auto pressTimeIt = button_press_time_.find(buttonId);
    if (pressTimeIt == button_press_time_.end()) return;

    bool scopedTriggered = false;

    // PASS 1: Check scoped bindings first (higher priority)
    for (auto& binding : button_bindings_) {
        if (!binding.enabled) continue;
        if (binding.type != ButtonBindingType::LONG_PRESS) continue;
        if (binding.buttonId != buttonId) continue;
        if (binding.scope == nullptr) continue;   // ONLY scoped bindings
        if (!isBindingActive(binding)) continue;  // Check scope visibility

        const uint32_t duration =
            binding.longPressMs > 0 ? binding.longPressMs : System::Input::LONG_PRESS_DEFAULT_MS;
        if ((now - pressTimeIt->second) >= duration) {
            long_press_triggered_[buttonId] = true;
            if (binding.action) {
                binding.action();
                scopedTriggered = true;
            }
        }
    }

    if (scopedTriggered) return;  // Stop propagation if scoped binding triggered

    // PASS 2: Check global bindings (lower priority)
    for (auto& binding : button_bindings_) {
        if (!binding.enabled) continue;
        if (binding.type != ButtonBindingType::LONG_PRESS) continue;
        if (binding.buttonId != buttonId) continue;
        if (binding.scope != nullptr) continue;  // ONLY global bindings

        const uint32_t duration =
            binding.longPressMs > 0 ? binding.longPressMs : System::Input::LONG_PRESS_DEFAULT_MS;
        if ((now - pressTimeIt->second) >= duration) {
            long_press_triggered_[buttonId] = true;
            if (binding.action) { binding.action(); }
        }
    }
}

void InputBinding::checkAndTriggerDoubleTap(ButtonID buttonId, uint32_t now) {
    auto tapIt = button_tap_count_.find(buttonId);
    if (tapIt == button_tap_count_.end() || tapIt->second < 2) return;

    auto releaseIt = button_release_time_.find(buttonId);
    if (releaseIt == button_release_time_.end()) return;

    if ((now - releaseIt->second) < System::Input::DOUBLE_TAP_WINDOW_MS) {
        triggerMatchingButtonBindings(buttonId, ButtonBindingType::DOUBLE_TAP);
        button_tap_count_[buttonId] = 0;
    }
}

void InputBinding::checkAndTriggerCombosOnRelease(ButtonID releasedButtonID) {
    bool scopedTriggered = false;

    // PASS 1: Check scoped combo bindings first (higher priority)
    for (auto& binding : button_bindings_) {
        if (!binding.enabled) continue;
        if (binding.type != ButtonBindingType::COMBO) continue;
        if (binding.scope == nullptr) continue;   // ONLY scoped bindings
        if (!isBindingActive(binding)) continue;  // Check scope visibility

        bool isPartOfCombo =
            (binding.buttonId == releasedButtonID) ||
            (binding.secondaryButton.has_value() && *binding.secondaryButton == releasedButtonID);

        if (!isPartOfCombo) continue;

        if (binding.secondaryButton.has_value()) {
            if (isButtonComboActive(binding.buttonId, *binding.secondaryButton)) {
                if (binding.action) {
                    binding.action();
                    scopedTriggered = true;
                }
            }
        }
    }

    if (scopedTriggered) return;  // Stop propagation if scoped combo triggered

    // PASS 2: Check global combo bindings (lower priority)
    for (auto& binding : button_bindings_) {
        if (!binding.enabled) continue;
        if (binding.type != ButtonBindingType::COMBO) continue;
        if (binding.scope != nullptr) continue;  // ONLY global bindings

        bool isPartOfCombo =
            (binding.buttonId == releasedButtonID) ||
            (binding.secondaryButton.has_value() && *binding.secondaryButton == releasedButtonID);

        if (!isPartOfCombo) continue;

        if (binding.secondaryButton.has_value()) {
            if (isButtonComboActive(binding.buttonId, *binding.secondaryButton)) {
                if (binding.action) { binding.action(); }
            }
        }
    }
}

bool InputBinding::isButtonComboActive(ButtonID btn1, ButtonID btn2) const {
    // Combo requires BOTH buttons physically pressed (not just latched)
    auto isPressed = [this](ButtonID btn) {
        return button_states_.count(btn) && button_states_.at(btn);
    };
    return isPressed(btn1) && isPressed(btn2);
}

void InputBinding::processTick(uint32_t currentTimeMs) {
    current_time_ = currentTimeMs;

    for (const auto& [buttonId, isPressed] : button_states_) {
        if (isPressed) { checkAndTriggerLongPress(buttonId, current_time_); }
    }
}

void InputBinding::clearBindings() {
    button_bindings_.clear();
    encoder_bindings_.clear();
    LOGLN("[InputBinding] Cleared all bindings");
}

void InputBinding::setBindingsEnabled(bool enabled) {
    bindings_enabled_ = enabled;
    LOGF("[InputBinding] Bindings %s\n", enabled ? "enabled" : "disabled");
}

bool InputBinding::isBindingActive(const ButtonBinding& binding) const {
    if (binding.scope == nullptr) { return true; }

    // LV_OBJ_FLAG_HIDDEN is an enum value from LVGL, always available
    return !lv_obj_has_flag(binding.scope, LV_OBJ_FLAG_HIDDEN);
}

bool InputBinding::isBindingActive(const EncoderBinding& binding) const {
    if (binding.scope == nullptr) { return true; }

    // LV_OBJ_FLAG_HIDDEN is an enum value from LVGL, always available
    return !lv_obj_has_flag(binding.scope, LV_OBJ_FLAG_HIDDEN);
}
