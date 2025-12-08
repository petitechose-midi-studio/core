# UI Shared

Reusable UI components, theme, and utilities for LVGL-based interfaces.

## Overview

This directory contains:

- **Widgets** — Reusable UI components (knobs, lists, labels)
- **Theme** — Colors, layout constants, animations
- **Interfaces** — Abstract view/component contracts
- **Fonts** — Custom font loader and font data
- **Utilities** — Text helpers and common functions

## Directory Structure

```
ui/shared/
├── interface/
│   ├── IView.hpp           # View lifecycle interface
│   ├── IComponent.hpp      # Visibility interface
│   ├── IWidget.hpp         # Widget interface
│   ├── ISelector.hpp       # Selection interface
│   └── IElement.hpp        # Base element interface
├── widget/
│   ├── ParameterKnobWidget.hpp/.cpp   # Rotary knob display
│   ├── ParameterButtonWidget.hpp/.cpp # Button state display
│   ├── ParameterListWidget.hpp/.cpp   # List parameter display
│   ├── ListOverlay.hpp/.cpp           # Scrollable list overlay
│   ├── HintBar.hpp/.cpp               # Bottom hint bar
│   ├── Label.hpp/.cpp                 # Auto-scrolling label
│   ├── TitleItem.hpp/.cpp             # Title with optional value
│   ├── ButtonIndicator.hpp/.cpp       # Button state indicator
│   ├── BaseSelector.hpp/.cpp          # Base for selectors
│   └── IParameterWidget.hpp           # Parameter widget interface
├── theme/
│   └── BaseTheme.hpp       # Colors, layout, animation constants
├── font/
│   ├── FontLoader.hpp/.cpp # Font registration and loading
│   └── data/               # Font binary data (.c.inc, .hpp)
└── util/
    └── TextUtils.hpp/.cpp  # Text measurement and helpers
```

---

## Interfaces

### IView

Lifecycle interface for views (screens/pages).

```cpp
class IView {
public:
    virtual ~IView() = default;
    virtual void onActivate() = 0;    // Called when view becomes visible
    virtual void onDeactivate() = 0;  // Called when view is hidden
    virtual const char* getViewId() const = 0;
};
```

### IComponent

Visibility management interface.

```cpp
class IComponent {
public:
    virtual ~IComponent() = default;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual bool isVisible() const = 0;
};
```

---

## Widgets

### ParameterKnobWidget

Displays a parameter value as a rotary arc with indicator line.

```cpp
ParameterKnobWidget(
    lv_obj_t* parent,
    uint16_t width,
    uint16_t height,
    uint8_t color_index = 0,  // 0-7 for macro colors
    bool centered = false     // Bipolar (origin at center)
);

void setName(const std::string& name);
void setValue(float value);           // 0.0 - 1.0
void setOrigin(float origin);         // For bipolar display
void setVisible(bool visible);
lv_obj_t* getContainer();
```

**Features:**
- Arc visualization with indicator line
- Value change flash animation
- Configurable origin for bipolar parameters
- Macro color support (8 colors)

### ListOverlay

Scrollable list overlay for selections.

```cpp
ListOverlay(lv_obj_t* parent, ControllerAPI& api);

void show(const std::vector<std::string>& items, int selectedIndex);
void hide();
void setOnSelect(std::function<void(int)> callback);
void setOnCancel(std::function<void()> callback);
```

**Features:**
- Encoder navigation (NAV encoder)
- Button confirm/cancel
- Auto-scroll to selection
- Scoped input bindings (auto-cleanup)

### HintBar

Bottom bar showing contextual button hints.

```cpp
HintBar(lv_obj_t* parent);

void setHints(const char* left, const char* center, const char* right);
void show();
void hide();
```

### Label

Text label with auto-scroll for overflow.

```cpp
Label(lv_obj_t* parent);

void setText(const std::string& text);
void setFont(const lv_font_t* font);
void setColor(uint32_t color);
void setAlignment(lv_align_t align);
```

**Features:**
- Auto-scrolls if text overflows container
- Configurable scroll delay and speed

### ButtonIndicator

Visual indicator for button state.

```cpp
ButtonIndicator(lv_obj_t* parent, uint32_t color);

void setActive(bool active);
void setColor(uint32_t color);
```

---

## Theme (BaseTheme.hpp)

### Colors

```cpp
namespace BaseTheme::Color {
    // Macro colors (parameter widgets)
    constexpr uint32_t MACRO_1_RED = 0xF41B3E;
    constexpr uint32_t MACRO_2_ORANGE = 0xFF7F17;
    constexpr uint32_t MACRO_3_YELLOW = 0xFCEB23;
    constexpr uint32_t MACRO_4_GREEN = 0x5BC515;
    constexpr uint32_t MACRO_5_CYAN = 0x65CE92;
    constexpr uint32_t MACRO_6_BLUE = 0x5CA6EE;
    constexpr uint32_t MACRO_7_PURPLE = 0xC36EFF;
    constexpr uint32_t MACRO_8_PINK = 0xFF54B0;

    constexpr uint32_t MACROS[8] = { ... };

    // Get color by index
    inline uint32_t getMacroColor(uint8_t index);

    // UI colors
    constexpr uint32_t BACKGROUND = 0x000000;
    constexpr uint32_t INACTIVE = 0x333333;
    constexpr uint32_t INACTIVE_LIGHTER = 0x666666;
    constexpr uint32_t ACTIVE = 0xECA747;
    constexpr uint32_t TEXT_PRIMARY = 0xFFFFFF;
    constexpr uint32_t TEXT_PRIMARY_INVERTED = 0x292929;
    constexpr uint32_t TEXT_SECONDARY = 0xD9D9D9;

    // Status colors
    constexpr uint32_t STATUS_SUCCESS = 0x00FF00;
    constexpr uint32_t STATUS_WARNING = 0xFFA500;
    constexpr uint32_t STATUS_ERROR = 0xFF0000;

    // Knob specific
    constexpr uint32_t KNOB_BACKGROUND = 0x202020;
    constexpr uint32_t KNOB_VALUE = 0x909090;
    constexpr uint32_t KNOB_TRACK = 0x606060;
}
```

### Layout

```cpp
namespace BaseTheme::Layout {
    // Margins (base unit: 2px)
    constexpr int16_t MARGIN_XS = 2;
    constexpr int16_t MARGIN_SM = 4;
    constexpr int16_t MARGIN_MD = 8;
    constexpr int16_t MARGIN_LG = 16;

    // Button padding
    constexpr int16_t PAD_BUTTON_H = 8;
    constexpr int16_t PAD_BUTTON_V = 6;

    // List specific
    constexpr int16_t LIST_ITEM_GAP = 2;
    constexpr int16_t LIST_PAD = 4;
    constexpr int16_t SCROLLBAR_WIDTH = 3;

    // Row gaps
    constexpr int16_t ROW_GAP_SM = 2;
    constexpr int16_t ROW_GAP_MD = 4;
}
```

### Animation

```cpp
namespace BaseTheme::Animation {
    constexpr uint32_t SCROLL_ANIM_MS = 50;
    constexpr uint32_t SCROLL_START_DELAY_MS = 500;
    constexpr uint32_t OVERFLOW_CHECK_DELAY_MS = 50;
}
```

---

## Fonts

### FontLoader

Manages font registration and progressive loading.

```cpp
// Register all fonts (call early in boot)
void fontsRegisterCore();

// Load essential fonts for splash screen
void fontsLoadEssential();

// Get pending font count for progress display
uint8_t fontsGetPendingCount();

// Load next pending font (returns true if more to load)
bool fontsLoadNext(const char** fontName);
```

### Available Fonts

| Font | Sizes | Weight |
|------|-------|--------|
| Inter Display | 13, 14, 20 | Bold |
| Inter Display | 14 | Light, Regular, Medium, SemiBold |
| JetBrains Mono NL | 13 | Medium |

---

## Usage Examples

### Creating a Parameter Page

```cpp
class ParameterPage {
public:
    ParameterPage(lv_obj_t* parent, ControllerAPI& api) {
        container_ = lv_obj_create(parent);
        lv_obj_set_size(container_, 320, 200);

        // Create 8 knobs in a grid
        for (int i = 0; i < 8; i++) {
            knobs_[i] = std::make_unique<ParameterKnobWidget>(
                container_, 60, 80, i, false);
            knobs_[i]->setName("Param " + std::to_string(i + 1));
        }

        // Setup input bindings
        for (int i = 0; i < 8; i++) {
            api.onTurned(encoderForIndex(i), [this, i](float v) {
                knobs_[i]->setValue(v);
            }, container_);
        }
    }

private:
    lv_obj_t* container_;
    std::array<std::unique_ptr<ParameterKnobWidget>, 8> knobs_;
};
```

### Using Theme Colors

```cpp
lv_obj_set_style_bg_color(obj,
    lv_color_hex(BaseTheme::Color::BACKGROUND), LV_STATE_DEFAULT);

lv_obj_set_style_text_color(label,
    lv_color_hex(BaseTheme::Color::TEXT_PRIMARY), LV_STATE_DEFAULT);

uint32_t paramColor = BaseTheme::Color::getMacroColor(paramIndex);
```

---

## See Also

- [Plugin Development](../../../docs/PLUGIN_DEVELOPMENT.md) — Using widgets in plugins
- [Architecture](../../../docs/ARCHITECTURE.md) — UI layer design
- [LVGL Documentation](https://docs.lvgl.io/) — LVGL reference
