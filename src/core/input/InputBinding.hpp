#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "config/InputID.hpp"
#include "core/event/IEventBus.hpp"
#include "core/struct/Binding.hpp"

/**
 * @brief Centralized input state management and binding system
 *
 * Provides a simple, type-safe API for binding actions to hardware controls.
 * Subscribes to EventBus input events and tracks button/encoder states.
 * Enables complex input patterns (combos, long press, double tap).
 *
 * Example usage:
 * @code
 * bindings.onPressed(ButtonID::LEFT_TOP, [this]() { uiManager.show(); });
 * bindings.onTurned(EncoderID::MACRO_1, [this](float val) { setParam(0, val); });
 * bindings.onCombo(ButtonID::LEFT_TOP, ButtonID::LEFT_CENTER, []() { reset(); });
 * bindings.onTurnedWhilePressed(EncoderID::NAV, ButtonID::NAV, [](float v) { fineTune(v); });
 * @endcode
 */
class InputBinding {
public:
    using ActionCallback = std::function<void()>;
    using EncoderActionCallback = std::function<void(float normalizedValue)>;

    explicit InputBinding(IEventBus& eventBus);
    ~InputBinding();

    InputBinding(const InputBinding&) = delete;
    InputBinding& operator=(const InputBinding&) = delete;

    void onPressed(ButtonID id, ActionCallback cb);
    void onReleased(ButtonID id, ActionCallback cb);
    void onLongPress(ButtonID id, ActionCallback cb, uint32_t ms = 500);
    void onDoubleTap(ButtonID id, ActionCallback cb);
    void onCombo(ButtonID btn1, ButtonID btn2, ActionCallback cb);

    void onTurned(EncoderID id, EncoderActionCallback cb);
    void onTurnedWhilePressed(EncoderID encoderId, ButtonID buttonId, EncoderActionCallback cb);

    void onPressed(ButtonID id, ActionCallback cb, lv_obj_t* scope);
    void onReleased(ButtonID id, ActionCallback cb, lv_obj_t* scope);
    void onLongPress(ButtonID id, ActionCallback cb, uint32_t ms, lv_obj_t* scope);
    void onDoubleTap(ButtonID id, ActionCallback cb, lv_obj_t* scope);
    void onCombo(ButtonID btn1, ButtonID btn2, ActionCallback cb, lv_obj_t* scope);

    void onTurned(EncoderID id, EncoderActionCallback cb, lv_obj_t* scope);
    void onTurnedWhilePressed(EncoderID encoderId, ButtonID buttonId, EncoderActionCallback cb, lv_obj_t* scope);

    void clearScope(lv_obj_t* scope);

    void processTick(uint32_t currentTimeMs);
    void clearBindings();
    void setBindingsEnabled(bool enabled);

private:
    std::vector<ButtonBinding> buttonBindings_;
    std::vector<EncoderBinding> encoderBindings_;

    void onEncoderChanged(const Event& event);
    void onButtonPress(const Event& event);
    void onButtonRelease(const Event& event);

    void triggerMatchingButtonBindings(ButtonID buttonId, ButtonBindingType type);
    void triggerMatchingEncoderBindings(EncoderID encoderId, float encoderValue);

    // Priority-based triggering helpers (scoped bindings have priority over global)
    bool triggerScopedButtonBindings(ButtonID buttonId, ButtonBindingType type);
    bool triggerGlobalButtonBindings(ButtonID buttonId, ButtonBindingType type);
    bool triggerScopedEncoderBindings(EncoderID encoderId, float encoderValue);
    bool triggerGlobalEncoderBindings(EncoderID encoderId, float encoderValue);

    bool isBindingActive(const ButtonBinding& binding) const;
    bool isBindingActive(const EncoderBinding& binding) const;

    void checkAndTriggerLongPress(ButtonID buttonId, uint32_t now);
    void checkAndTriggerDoubleTap(ButtonID buttonId, uint32_t now);
    void checkAndTriggerCombosOnRelease(ButtonID releasedButtonID);
    bool isButtonComboActive(ButtonID btn1, ButtonID btn2) const;

    std::unordered_map<ButtonID, bool> buttonStates_;
    std::unordered_map<ButtonID, uint32_t> buttonPressTime_;
    std::unordered_map<ButtonID, uint32_t> buttonReleaseTime_;
    std::unordered_map<ButtonID, uint8_t> buttonTapCount_;
    std::unordered_map<ButtonID, bool> longPressTriggered_;

    IEventBus& eventBus_;
    SubscriptionId encoderSub_;
    SubscriptionId buttonPressSub_;
    SubscriptionId buttonReleaseSub_;

    bool bindingsEnabled_ = true;
    uint32_t currentTime_ = 0;

};
