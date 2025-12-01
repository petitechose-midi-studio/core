#include "BootManager.hpp"

#include "adapter/display/driver/Ili9341Driver.hpp"
#include "adapter/display/ui/LVGLBridge.hpp"
#include "adapter/multiplexer/MultiplexerController.hpp"
#include "adapter/input/encoder/EncoderController.hpp"
#include "adapter/input/button/ButtonController.hpp"
#include "adapter/midi/TeensyUsbMidiIn.hpp"
#include "adapter/midi/TeensyUsbMidiOut.hpp"
#include "manager/ViewManager.hpp"
#include "manager/InputManager.hpp"
#include "core/event/IEventBus.hpp"
#include "ui/shared/font/FontLoader.hpp"
#include "log/Macros.hpp"

namespace Boot {

BootManager::BootManager(Components components)
    : components_(components) {
    LOGLN("[Boot] Starting...");
}

bool BootManager::tick() {
    if (status_.isComplete()) return true;

    switch (status_.currentPhase) {
        case Phase::NOT_STARTED:
            setPhase(Phase::HARDWARE_INIT);
            break;
        case Phase::HARDWARE_INIT:
            executeHardwareInit();
            setPhase(Phase::DISPLAY_INIT);
            break;
        case Phase::DISPLAY_INIT:
            executeDisplayInit();
            setPhase(Phase::MINIMAL_UI);
            break;
        case Phase::MINIMAL_UI:
            executeMinimalUI();
            setPhase(Phase::LOADING_FONTS);
            break;
        case Phase::LOADING_FONTS:
            if (executeLoadingFonts()) {
                setPhase(Phase::INPUT_INIT);
            }
            break;
        case Phase::INPUT_INIT:
            executeInputInit();
            setPhase(Phase::MIDI_INIT);
            break;
        case Phase::MIDI_INIT:
            executeMidiInit();
            executeReady();
            break;
        default:
            break;
    }

    return status_.isComplete();
}

void BootManager::executeHardwareInit() {
    LOGLN("[Boot] Hardware init");
    components_.multiplexer.init();
    components_.displayDriver.init();
    components_.encoders.init();
}

void BootManager::executeDisplayInit() {
    LOGLN("[Boot] Display init");
    components_.lvglBridge.init();
    components_.lvglBridge.refresh();  // Process LVGL init before loading fonts
    components_.viewManager.initScreens();
}

void BootManager::executeMinimalUI() {
    LOGLN("[Boot] Minimal UI");

    fonts_register_core();
    fonts_load_essential();
    components_.viewManager.initSplash();

    SplashScreenView* splash = components_.viewManager.getSplashView();
    if (splash) {
        splash->setBootMode(true);
    }

    components_.lvglBridge.refresh();
    total_fonts_ = fonts_get_pending_count();
    loaded_fonts_ = 0;
}

bool BootManager::executeLoadingFonts() {
    const char* fontName = nullptr;

    if (fonts_load_next(&fontName)) {
        loaded_fonts_++;
        uint8_t progress = total_fonts_ > 0 ? (loaded_fonts_ * 100) / total_fonts_ : 100;
        updateSplash(progress, fontName);
        components_.lvglBridge.refresh();
        return false;
    }

    LOGLN("[Boot] Fonts loaded");
    return true;
}

void BootManager::executeInputInit() {
    LOGLN("[Boot] Input init");
    components_.encoders.flushAllEvents();
    updateSplash(100, "Input");
    components_.lvglBridge.refresh();
}

void BootManager::executeMidiInit() {
    LOGLN("[Boot] MIDI init");
    components_.midiIn.init();
    components_.midiIn.processPendingMessages();
    updateSplash(100, "MIDI");
    components_.lvglBridge.refresh();
}

void BootManager::executeReady() {
    LOGLN("[Boot] Ready");
    setPhase(Phase::READY);

    SplashScreenView* splash = components_.viewManager.getSplashView();
    if (splash) {
        splash->markBootComplete();
    }

    components_.lvglBridge.refresh();
    components_.viewManager.emitBootComplete();
}

void BootManager::setPhase(Phase phase, const char* text) {
    status_.currentPhase = phase;
    status_.progress = 0;
    status_.text = text;
}

void BootManager::updateSplash(uint8_t progress, const char* text) {
    SplashScreenView* splash = components_.viewManager.getSplashView();
    if (splash) {
        splash->setBootProgress(progress);
        splash->setBootStatus(text);
    }
}

} // namespace Boot
