# midi-studio/core - Migration Plan & Tracking

## État Global
- **Phase actuelle:** Phase 4 ✅ (MIDI + UI) - TERMINÉ
- **Dernière màj:** 2025-01-14
- **Validation hardware:** Phase 4 OK - Standalone fonctionnel
- **Note:** Ce plan est pour midi-studio/core. Voir bitwig-desktop-plan.md pour plugin-bitwig desktop.

---

# PHASE 1: Boot minimal (display seul)

**Objectif:** Écran qui s'allume et affiche couleur LVGL
**Critère de validation:** Écran affiche quelque chose (blanc, couleur, logo)

---

## 1.1 platformio.ini
- [x] Complété

**Contenu attendu:**
```ini
[platformio]
default_envs = dev

[env]
platform = teensy
board = teensy41
framework = arduino
board_build.f_cpu = 450000000L
build_flags =
    -std=gnu++17
    -D USB_MIDI_SERIAL
    -I src
    -I src/config
lib_ignore = lvgl_demos, lvgl_examples

[env:dev]
lib_deps =
    open-control=symlink://../open-control/framework
    open-control-hal-common=symlink://../open-control/hal-common
    oc-hal-teensy=symlink://../open-control/hal-teensy
    oc-ui-lvgl=symlink://../open-control/ui-lvgl
```

**Points d'attention:**
```

```

**Modifications vs plan:**
```

```

---

## 1.2 config/App.hpp (minimal)
- [x] Complété
- **Fichier:** `src/config/App.hpp`

**Contenu attendu:**
```cpp
#pragma once
#include <cstdint>

namespace Config {

namespace Timing {
constexpr uint32_t LVGL_HZ = 60;  // Source unique framerate
}

}  // namespace Config
```

**Points d'attention:**
```
Version minimale - sera enrichi Phase 2 et 3
```

**Modifications vs plan:**
```

```

---

## 1.3 config/Hardware.hpp (display only)
- [x] Complété
- **Fichier:** `src/config/Hardware.hpp`

**Contenu attendu:**
```cpp
#pragma once
#include <oc/teensy/Ili9341.hpp>
#include <oc/ui/lvgl/Bridge.hpp>
#include "App.hpp"

namespace Hardware {

namespace Display {
constexpr uint8_t VSYNC_SPACING = 2;

constexpr oc::teensy::Ili9341Config CONFIG = {
    .width = 320, .height = 240,
    .csPin = 28, .dcPin = 0, .rstPin = 29,
    .mosiPin = 26, .sckPin = 27, .misoPin = 1,
    .spiSpeed = 20'000'000,
    .rotation = 3,
    .invertDisplay = false,
    .vsyncSpacing = VSYNC_SPACING,
    .refreshRate = Config::Timing::LVGL_HZ * VSYNC_SPACING
};
constexpr size_t BUFFER_SIZE = CONFIG.framebufferSize();
constexpr size_t DIFF_SIZE = 16384;
}

namespace LVGL {
constexpr oc::ui::lvgl::BridgeConfig CONFIG = {
    .renderMode = LV_DISPLAY_RENDER_MODE_FULL,
    .buffer2 = nullptr,
    .refreshHz = Config::Timing::LVGL_HZ
};
}

}  // namespace Hardware
```

**Points d'attention:**
```
Vérifier pins SPI dans config actuelle
```

**Modifications vs plan:**
```

```

---

## 1.4 config/Buffer.hpp
- [x] Complété
- **Fichier:** `src/config/Buffer.hpp`

**Contenu attendu:**
```cpp
#pragma once
#include "Hardware.hpp"

namespace Buffer {

DMAMEM alignas(32) uint16_t framebuffer[Hardware::Display::BUFFER_SIZE];
DMAMEM alignas(32) uint16_t diff1[Hardware::Display::DIFF_SIZE];
DMAMEM alignas(32) uint16_t diff2[Hardware::Display::DIFF_SIZE];
DMAMEM alignas(32) uint16_t lvgl[Hardware::Display::BUFFER_SIZE];

}  // namespace Buffer
```

**Points d'attention:**
```

```

**Modifications vs plan:**
```

```

---

## 1.5 config/lv_conf.h
- [x] Complété
- **Fichier:** `src/config/lv_conf.h`

**Contenu attendu:**
- Copie depuis example_teensy41_lvgl
- SANS `LV_DEF_REFR_PERIOD` (géré en C++)
- Memory pool path: `#define LV_MEM_POOL_INCLUDE "config/LvglMemory.hpp"`

**Points d'attention:**
```
Ne pas définir LV_DEF_REFR_PERIOD
LV_MEM_CUSTOM = 1
```

**Modifications vs plan:**
```

```

---

## 1.6 config/LvglMemory.hpp + .cpp
- [x] Complété
- **Fichiers:** `src/config/LvglMemory.hpp`, `src/config/LvglMemory.cpp`

**LvglMemory.hpp:**
```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint8_t* getLvglMemoryPool(size_t size);
#ifdef __cplusplus
}
#endif
```

**LvglMemory.cpp:**
```cpp
#include "LvglMemory.hpp"
#include <Arduino.h>
EXTMEM static uint8_t lvgl_memory_pool[2 * 1024 * 1024];  // 2MB PSRAM
extern "C" { uint8_t* getLvglMemoryPool(size_t) { return lvgl_memory_pool; } }
```

**Points d'attention:**
```
EXTMEM = PSRAM Teensy 4.1
```

**Modifications vs plan:**
```

```

---

## 1.7 main.cpp (boot display)
- [x] Complété
- **Fichier:** `src/main.cpp`

**Contenu attendu:**
```cpp
#include "config/App.hpp"
#include "config/Hardware.hpp"
#include "config/Buffer.hpp"

#include <optional>
#include <Arduino.h>
#include <oc/teensy/Teensy.hpp>

static std::optional<oc::teensy::Ili9341> display;
static std::optional<oc::ui::lvgl::Bridge> bridge;

static bool initDisplay() {
    using oc::teensy::Ili9341;
    display = Ili9341(
        Hardware::Display::CONFIG,
        {.framebuffer = Buffer::framebuffer, .diff1 = Buffer::diff1, .diff2 = Buffer::diff2});
    return display->init();
}

static bool initLVGL() {
    using oc::ui::lvgl::Bridge;
    using oc::teensy::defaultTimeProvider;
    bridge = Bridge(*display, Buffer::lvgl, defaultTimeProvider, Hardware::LVGL::CONFIG);
    return bridge->init();
}

void setup() {
    while (!Serial && millis() < 3000) {}
    Serial.printf("\n[MIDI Studio] LVGL %luHz\n\n", Config::Timing::LVGL_HZ);
    
    if (!initDisplay()) { Serial.println("[ERROR] Display"); while (true); }
    if (!initLVGL()) { Serial.println("[ERROR] LVGL"); while (true); }
    
    Serial.println("[OK] Ready\n");
}

constexpr uint32_t LVGL_PERIOD_US = 1'000'000 / Config::Timing::LVGL_HZ;

void loop() {
    static uint32_t lastMicros = 0;
    const uint32_t now = micros();
    if (now - lastMicros < LVGL_PERIOD_US) return;
    lastMicros = now;
    bridge->refresh();
}
```

**Points d'attention:**
```
Pas de emplace, utiliser assignment
Pas encore d'AppBuilder
```

**Modifications vs plan:**
```

```

---

## ✅ Validation Phase 1
- [x] **TEST HARDWARE:** Écran affiche couleur/blanc

**Résultat:**
```
Boot OK - Écran noir (normal, pas de widgets LVGL)
Logs: "[MIDI Studio] Phase 1 - LVGL 60Hz" + "[OK] Display ready"
```

**Synthèse Phase 1:**
```
- Legacy code déplacé vers _ui_backup/ (hors src/)
- Fonts data conservés dans src/ui/shared/font/data/
- Symlinks open-control: ../../open-control/* (path corrigé)
- Buffer.hpp: inline DMAMEM + lv_color_t pour lvgl buffer
- Compilation OK, firmware.hex 884KB
```

---

# PHASE 2: Contextes sans inputs

**Objectif:** Navigation Boot → Standalone fonctionne
**Critère de validation:** Animation boot puis écran standalone statique
**Prérequis:** Phase 1 validée

---

## 2.1 config/App.hpp (+ ContextID)
- [x] Complété
- **Fichier:** `src/config/App.hpp`

**Ajouts:**
```cpp
enum class ContextID : uint8_t {
    BOOT = 0,
    STANDALONE = 1,
};
```

**Points d'attention:**
```

```

**Modifications vs plan:**
```

```

---

## 2.2 context/BootContext.hpp
- [x] Complété
- **Fichier:** `src/context/BootContext.hpp`

**Contenu attendu:**
```cpp
#pragma once
#include <oc/context/IContext.hpp>
#include "config/App.hpp"

namespace context {

class BootContext : public oc::context::IContext {
public:
    static constexpr oc::context::Requirements REQUIRES{};
    
    bool initialize() override { return true; }
    
    void update() override {
        if (++frame_ > 60) {
            switchTo(Config::ContextID::STANDALONE);
        }
    }
    
    void cleanup() override {}
    const char* getName() const override { return "Boot"; }
    
private:
    uint16_t frame_ = 0;
};

}  // namespace context
```

**Points d'attention:**
```
onEnter: créer UI LVGL simple
onExit: cleanup UI
Animation minimaliste pour test
```

**Modifications vs plan:**
```

```

---

## 2.3 context/StandaloneContext.hpp (minimal)
- [x] Complété
- **Fichier:** `src/context/StandaloneContext.hpp`

**Contenu attendu:**
```cpp
#pragma once
#include <oc/context/IContext.hpp>
#include "config/App.hpp"

namespace context {

class StandaloneContext : public oc::context::IContext {
public:
    static constexpr oc::context::Requirements REQUIRES{};
    
    bool initialize() override { return true; }
    void update() override {}
    void cleanup() override {}
    const char* getName() const override { return "Standalone"; }
};

}  // namespace context
```

**Points d'attention:**
```
Version minimale sans inputs
Requirements vide pour l'instant
```

**Modifications vs plan:**
```

```

---

## 2.4 main.cpp (+ AppBuilder minimal)
- [x] Complété
- **Fichier:** `src/main.cpp`

**Ajouts:**
```cpp
#include "context/BootContext.hpp"
#include "context/StandaloneContext.hpp"
#include <oc/app/OpenControlApp.hpp>

static std::optional<oc::app::OpenControlApp> app;

static bool initApp() {
    app = oc::teensy::AppBuilder();  // Pas de .buttons() ni .encoders()
    
    app->registerContext<context::BootContext>(Config::ContextID::BOOT, "Boot");
    app->registerContext<context::StandaloneContext>(Config::ContextID::STANDALONE, "Standalone");
    
    return app->begin();
}

// setup(): ajouter initApp()
// loop(): ajouter app->update()
```

**Points d'attention:**
```
AppBuilder sans .buttons() ni .encoders() pour l'instant
```

**Modifications vs plan:**
```

```

---

## ✅ Validation Phase 2
- [x] **TEST HARDWARE:** Boot animation → Standalone screen

**Résultat:**
```
[MIDI Studio] Phase 2 - App 1000Hz, LVGL 60Hz
[Boot] Starting...
[OK] Ready
[Boot] Complete, switching to Standalone
[Boot] Cleanup
[Standalone] Active
```

**Synthèse Phase 2:**
```
- App.hpp: Ajout ContextID enum (BOOT, STANDALONE) + APP_HZ 1000
- BootContext: Switch vers Standalone après 60 frames (~1s)
- StandaloneContext: Version minimale sans requirements
- main.cpp: AppBuilder sans inputs, loop dual-rate (1000Hz app, 60Hz LVGL)
- Lifecycle contextes validé: initialize → update → cleanup → switch
```

---

# PHASE 3: Inputs (buttons + encoders + MUX)

**Objectif:** Les boutons et encodeurs répondent
**Critère de validation:** Appui bouton → Serial.println
**Prérequis:** Phase 2 validée

---

## 3.1 config/Hardware.hpp (+ inputs)
- [x] Complété
- **Fichier:** `src/config/Hardware.hpp`

**Ajouts:**
```cpp
#include <array>
#include <oc/common/ButtonDef.hpp>
#include <oc/common/EncoderDef.hpp>
#include <oc/teensy/GenericMux.hpp>

namespace Mux {
constexpr oc::teensy::CD74HC4067::Config CONFIG = {
    .selectPins = {3, 2, 5, 6},
    .signalPin = 4,
    .settleTimeUs = 20,
    .signalPullup = true
};
}

namespace Encoder {
using namespace oc::common;
using ID = Config::EncoderID;
constexpr std::array DEFS = {
    EncoderDef(ID::MACRO_1, 22, 23, 24, 270, 1, false),
    EncoderDef(ID::MACRO_2, 18, 19, 24, 270, 1, false),
    // ... autres encodeurs
    EncoderDef(ID::NAV, 31, 30, 24, 270, 4, false),
    EncoderDef(ID::OPT, 34, 33, 600, 270, 1, false),
};
}

namespace Button {
using namespace oc::common;
using ID = Config::ButtonID;
using Src = oc::hal::GpioPin::Source;
constexpr std::array BUTTONS = {
    ButtonDef(ID::NAV, {32, Src::MCU}, true),
    ButtonDef(ID::LEFT_TOP, {9, Src::MUX}, true),
    // ... autres boutons
};
}
```

**Points d'attention:**
```
Récupérer pins exacts de config actuelle
Un seul array BUTTONS avec MCU + MUX
GenericMux<4> = 4 select pins → 16 channels
```

**Modifications vs plan:**
```

```

---

## 3.2 config/App.hpp (+ IDs)
- [x] Complété
- **Fichier:** `src/config/App.hpp`

**Ajouts:**
```cpp
enum class ButtonID : uint16_t {
    LEFT_TOP = 10, LEFT_CENTER = 11, LEFT_BOTTOM = 12,
    BOTTOM_LEFT = 20, BOTTOM_CENTER = 21, BOTTOM_RIGHT = 22,
    MACRO_1 = 31, MACRO_2 = 32, MACRO_3 = 33, MACRO_4 = 34,
    MACRO_5 = 35, MACRO_6 = 36, MACRO_7 = 37, MACRO_8 = 38,
    NAV = 40,
};

enum class EncoderID : uint16_t {
    MACRO_1 = 301, MACRO_2 = 302, MACRO_3 = 303, MACRO_4 = 304,
    MACRO_5 = 305, MACRO_6 = 306, MACRO_7 = 307, MACRO_8 = 308,
    NAV = 400, OPT = 410,
};

namespace Timing {
constexpr uint8_t DEBOUNCE_MS = 5;
}
```

**Points d'attention:**
```
Reprendre IDs existants pour compatibilité
```

**Modifications vs plan:**
```

```

---

## 3.3 main.cpp (+ inputs)
- [x] Complété
- **Fichier:** `src/main.cpp`

**Ajouts:**
```cpp
static std::optional<oc::teensy::CD74HC4067> mux;

static bool initMux() {
    using oc::teensy::CD74HC4067;
    using oc::teensy::gpio;
    mux = CD74HC4067(Hardware::Mux::CONFIG, gpio());
    return mux->init();
}

static bool initApp() {
    app = oc::teensy::AppBuilder()
        .encoders(Hardware::Encoder::DEFS)
        .buttons(Hardware::Button::BUTTONS, *mux, Config::Timing::DEBOUNCE_MS);
    // ...
}

// setup(): ajouter initMux() AVANT initApp()
```

**Points d'attention:**
```
mux init AVANT AppBuilder
gpio() singleton de oc::teensy
```

**Modifications vs plan:**
```

```

---

## 3.4 StandaloneContext (+ input handling)
- [x] Complété
- **Fichier:** `src/context/StandaloneContext.hpp`

**Ajouts:**
```cpp
static constexpr oc::context::Requirements REQUIRES{
    .button = true,
    .encoder = true,
};

bool initialize() override {
    button(Config::ButtonID::NAV).press().then([] {
        Serial.println("NAV pressed");
    });
    encoder(Config::EncoderID::NAV).turn().then([](int delta) {
        Serial.printf("NAV turn: %d\n", delta);
    });
    return true;
}
```

**Points d'attention:**
```
Juste debug Serial pour valider
```

**Modifications vs plan:**
```

```

---

## ✅ Validation Phase 3
- [ ] **TEST HARDWARE:** Boutons + encodeurs → Serial output

**Résultat:**
```

```

**Synthèse Phase 3:**
```

```

---

# PHASE 4: MIDI + UI complète

**Objectif:** Fonctionnalités complètes
**Critère de validation:** Fonctionnement équivalent à version actuelle
**Prérequis:** Phase 3 validée

---

## 4.1 main.cpp (+ MIDI)
- [x] Complété
- **Fichier:** `src/main.cpp`

**Ajouts:**
```cpp
app = oc::teensy::AppBuilder()
    .midi()  // Ajouter
    .encoders(Hardware::Encoder::DEFS)
    .buttons(Hardware::Button::BUTTONS, *mux, Config::Timing::DEBOUNCE_MS)
    .inputConfig(Config::Input::CONFIG);
```

**Points d'attention:**
```
USB MIDI via UsbMidi
```

**Modifications vs plan:**
```

```

---

## 4.2 config/App.hpp (+ InputConfig)
- [x] Complété
- **Fichier:** `src/config/App.hpp`

**Ajouts:**
```cpp
#include <oc/core/input/InputConfig.hpp>

namespace Timing {
constexpr uint16_t LONG_PRESS_MS = 500;
constexpr uint16_t DOUBLE_TAP_MS = 300;
}

namespace Input {
constexpr oc::core::InputConfig CONFIG = {
    .longPressMs = Timing::LONG_PRESS_MS,
    .doubleTapWindowMs = Timing::DOUBLE_TAP_MS
};
}
```

**Points d'attention:**
```
Reprendre valeurs timing actuelles
```

**Modifications vs plan:**
```

```

---

## 4.3 StandaloneContext (UI réelle)
- [x] Complété
- **Fichier:** `src/context/StandaloneContext.hpp`

**Contenu attendu:**
- Port UI depuis version actuelle
- Logique métier complète
- Interactions MIDI

**Points d'attention:**
```
Utiliser oc::ui::lvgl::Scope pour binding
```

**Modifications vs plan:**
```

```

---

## ✅ Validation Phase 4
- [ ] **TEST HARDWARE:** Fonctionnement complet

**Résultat:**
```

```

**Synthèse Phase 4:**
```

```

---

# PHASE 5: Plugin context (futur)

**Objectif:** Migration plugin-bitwig comme contexte
**Prérequis:** Phase 4 validée

## API Mapping (référence)

| Current (ControllerAPI) | Open-Control Framework |
|------------------------|----------------------|
| `onPressed(id, cb)` | `button(id).press().then(cb)` |
| `onReleased(id, cb)` | `button(id).release().then(cb)` |
| `onTurned(id, cb)` | `encoder(id).turn().then(cb)` |
| `onSysEx(cb)` | `midi().onSysEx(cb)` |
| `sendSysEx(data, len)` | `midi().sendSysEx(data, len)` |

---

# Journal des modifications

| Date | Phase/Étape | Modification | Raison |
|------|-------------|--------------|--------|
| 2025-12-08 | Phase 1 | Legacy → _ui_backup/ hors src/ | Éviter compilation legacy |
| 2025-12-08 | Phase 1 | Symlinks: ../../open-control/* | Path relatif depuis core/ |
| 2025-12-08 | Phase 1 | main.cpp: millis au lieu de defaultTimeProvider | Simplicité |

