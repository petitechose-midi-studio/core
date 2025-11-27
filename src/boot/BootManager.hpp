#pragma once

#include "BootPhase.hpp"

class Ili9341Driver;
class LVGLBridge;
class ViewManager;
class EncoderController;
class ButtonController;
class TeensyUsbMidiIn;
class TeensyUsbMidiOut;
class InputManager;
class IEventBus;

namespace Boot {

class BootManager {
public:
    struct Components {
        Ili9341Driver& displayDriver;
        LVGLBridge& lvglBridge;
        ViewManager& viewManager;
        EncoderController& encoders;
        ButtonController& buttons;
        TeensyUsbMidiIn& midiIn;
        TeensyUsbMidiOut& midiOut;
        InputManager& inputManager;
        IEventBus& eventBus;
    };

    explicit BootManager(Components components);
    ~BootManager() = default;

    BootManager(const BootManager&) = delete;
    BootManager& operator=(const BootManager&) = delete;

    bool tick();
    bool isComplete() const { return status_.isComplete(); }

private:
    Components components_;
    Status status_;
    uint8_t totalFonts_ = 0;
    uint8_t loadedFonts_ = 0;

    void executeHardwareInit();
    void executeDisplayInit();
    void executeMinimalUI();
    bool executeLoadingFonts();
    void executeInputInit();
    void executeMidiInit();
    void executeReady();

    void setPhase(Phase phase, const char* text = "");
    void updateSplash(uint8_t progress, const char* text);
};

} // namespace Boot
