# How To Add a View

> **Type**: Step-by-step Tutorial
> **Audience**: Developers creating new full-screen views
> **Time**: 45-60 minutes
> **Prerequisites**: [HOW_TO_ADD_WIDGET.md](HOW_TO_ADD_WIDGET.md), [HOW_TO_ADD_HANDLER.md](HOW_TO_ADD_HANDLER.md)

This guide walks through creating a complete View with widgets, state bindings, and handlers.

---

## Table of Contents

1. [View Architecture](#1-view-architecture)
2. [Step 1: Create the View Class](#step-1-create-the-view-class)
3. [Step 2: Create the Layout](#step-2-create-the-layout)
4. [Step 3: Create Widgets](#step-3-create-widgets)
5. [Step 4: Bind to State](#step-4-bind-to-state)
6. [Step 5: Register in Context](#step-5-register-in-context)
7. [ViewContainer Integration](#viewcontainer-integration)
8. [Complete Example](#complete-example)

---

## 1. View Architecture

### IView Interface

```cpp
class IView : public IElement {
public:
    virtual void onActivate() = 0;    // Called when view becomes visible
    virtual void onDeactivate() = 0;  // Called when view is hidden
    virtual const char* getViewId() const = 0;  // Unique identifier
    // From IElement:
    virtual lv_obj_t* getElement() const = 0;
};
```

### View Lifecycle

```
Context creates View
         │
         ▼
    ┌─────────────┐
    │  INACTIVE   │ ──────────────────────────────┐
    │  (hidden)   │                               │
    └─────────────┘                               │
         │                                        │
         │ onActivate()                           │ onDeactivate()
         ▼                                        │
    ┌─────────────┐                               │
    │   ACTIVE    │ ──────────────────────────────┘
    │  (visible)  │
    │             │
    │ - Show UI   │
    │ - Respond   │
    │   to input  │
    └─────────────┘
```

### View Components

```
MacroView
├── container_ (lv_obj_t*)          # Root element
├── top_bar_ (TopBar)               # Status bar
├── macros_[] (MacroKnobWidget[8])  # Widgets
├── subscriptions_ (vector)         # State bindings
└── update_timer_ (lv_timer_t*)     # Debounce timer
```

---

## Step 1: Create the View Class

### Header

```cpp
// File: src/ui/view/VolumeView.hpp
#pragma once

/**
 * @file VolumeView.hpp
 * @brief Volume control view with slider and mute
 */

#include <memory>
#include <vector>

#include <lvgl.h>

#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include "state/CoreState.hpp"
#include "ui/widget/VolumeSliderWidget.hpp"

namespace core::ui {

class VolumeView : public oc::ui::lvgl::IView {
public:
    VolumeView(lv_obj_t* parent, core::state::CoreState& state);
    ~VolumeView() override;

    // IView interface
    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.volume"; }
    lv_obj_t* getElement() const override { return container_; }

private:
    void createLayout(lv_obj_t* parent);
    void createWidgets();
    void bindToState();

    core::state::CoreState& state_;
    std::vector<oc::state::Subscription> subscriptions_;

    lv_obj_t* container_ = nullptr;
    std::unique_ptr<VolumeSliderWidget> volume_widget_;
};

}  // namespace core::ui
```

### Implementation Structure

```cpp
// File: src/ui/view/VolumeView.cpp
#include "VolumeView.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

namespace core::ui {

namespace Theme = oc::ui::lvgl::BaseTheme;
namespace style = oc::ui::lvgl::style;

VolumeView::VolumeView(lv_obj_t* parent, core::state::CoreState& state)
    : state_(state)
{
    createLayout(parent);
    createWidgets();
    bindToState();
}

VolumeView::~VolumeView() {
    subscriptions_.clear();
    volume_widget_.reset();

    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}

void VolumeView::onActivate() {
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void VolumeView::onDeactivate() {
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

// ... layout, widgets, bindings
}  // namespace core::ui
```

---

## Step 2: Create the Layout

### Flex Column Layout (Recommended)

```cpp
void VolumeView::createLayout(lv_obj_t* parent) {
    // Main container
    container_ = lv_obj_create(parent);
    style::apply(container_)
        .fullSize()
        .pad(0)
        .bgColor(Theme::color::BACKGROUND);

    // Flex column layout
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(container_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container_, 0, LV_STATE_DEFAULT);
}
```

### Grid Layout

```cpp
void MacroView::createLayout(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    style::apply(container_).fullSize().pad(0).bgColor(Theme::color::BACKGROUND);

    // Grid: 4 columns x 2 rows
    static lv_coord_t col_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };
    static lv_coord_t row_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };

    lv_obj_set_grid_dsc_array(container_, col_dsc, row_dsc);
    lv_obj_set_layout(container_, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
}
```

### With TopBar Zone

```cpp
void MacroView::createLayout(lv_obj_t* parent) {
    // Main container (flex column)
    container_ = lv_obj_create(parent);
    style::apply(container_).fullSize().pad(0).bgColor(Theme::color::BACKGROUND);
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);

    // TopBar zone (content height)
    top_bar_container_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_container_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(top_bar_container_).transparent();

    // Body zone (fills remaining space)
    body_container_ = lv_obj_create(container_);
    lv_obj_set_size(body_container_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(body_container_, 1);  // Grow to fill
    style::apply(body_container_).transparent();
}
```

---

## Step 3: Create Widgets

### Single Widget

```cpp
void VolumeView::createWidgets() {
    volume_widget_ = std::make_unique<VolumeSliderWidget>(container_);

    // Center in container
    lv_obj_align(volume_widget_->getElement(), LV_ALIGN_CENTER, 0, 0);
}
```

### Widget Array

```cpp
void MacroView::createWidgets() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        uint8_t col = i % COLS;
        uint8_t row = i / COLS;

        macros_[i] = std::make_unique<MacroKnobWidget>(body_container_, i);

        // Position in grid
        lv_obj_set_grid_cell(macros_[i]->getElement(),
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);
    }
}
```

### With TopBar

```cpp
void MacroView::createTopBar() {
    top_bar_ = std::make_unique<TopBar>(top_bar_container_, state_.statusBar);
}
```

---

## Step 4: Bind to State

### Simple Binding

```cpp
void VolumeView::bindToState() {
    subscriptions_.push_back(
        state_.volume.level.subscribe([this](float level) {
            volume_widget_->setLevel(level);
        })
    );

    subscriptions_.push_back(
        state_.volume.muted.subscribe([this](bool muted) {
            volume_widget_->setMuted(muted);
        })
    );

    // Initialize with current values
    volume_widget_->setLevel(state_.volume.level.get());
    volume_widget_->setMuted(state_.volume.muted.get());
}
```

### Debounced Binding (High-Frequency Updates)

For encoders and continuous values, use dirty flags:

```cpp
// In header:
std::array<bool, MACRO_COUNT> dirty_flags_{};
lv_timer_t* update_timer_ = nullptr;

// In constructor:
VolumeView::VolumeView(...) {
    // ...
    constexpr uint32_t periodMs = 1000 / 60;  // 60 Hz
    update_timer_ = lv_timer_create(onUpdateTimer, periodMs, this);
    bindToState();
}

// Binding sets dirty flag instead of updating immediately
void MacroView::bindToState() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        subscriptions_.push_back(
            state_.macros[i].value.subscribe([this, i](float) {
                dirty_flags_[i] = true;  // Mark for update
            })
        );

        // Initialize
        macros_[i]->setValue(state_.macros[i].value.get());
    }
}

// Timer processes dirty flags
void MacroView::onUpdateTimer(lv_timer_t* timer) {
    auto* self = static_cast<MacroView*>(lv_timer_get_user_data(timer));
    self->processDirtyFlags();
}

void MacroView::processDirtyFlags() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        if (dirty_flags_[i]) {
            dirty_flags_[i] = false;
            macros_[i]->setValue(state_.macros[i].value.get());
        }
    }
}

// Cleanup in destructor
VolumeView::~VolumeView() {
    if (update_timer_) {
        lv_timer_delete(update_timer_);
        update_timer_ = nullptr;
    }
    // ...
}
```

---

## Step 5: Register in Context

```cpp
// In context/StandaloneContext.cpp

bool StandaloneContext::initialize() {
    // 1. Create view container
    view_container_ = std::make_unique<ViewContainer>(lv_screen_active());

    // 2. Create view
    view_ = std::make_unique<MacroView>(
        view_container_->getMainZone(),
        core_state_
    );

    // 3. Create handlers (with view's scope element)
    input_handler_ = std::make_unique<MacroValueHandler>(
        core_state_,
        encoders(),
        midi(),
        view_->getElement()
    );

    // 4. Activate view
    view_->onActivate();

    return true;
}
```

---

## ViewContainer Integration

`ViewContainer` manages view zones:

```cpp
// ViewContainer provides:
// - getMainZone(): Main content area
// - getBottomZone(): Bottom bar area (optional)
// - getContainer(): Root container

// Usage pattern:
auto view = std::make_unique<MacroView>(
    view_container_->getMainZone(),  // Parent for view
    state
);

// View owns its container, ViewContainer owns zones
```

---

## Complete Example

### MacroView

```cpp
// File: src/ui/view/MacroView.hpp
#pragma once

#include <array>
#include <memory>
#include <vector>

#include <lvgl.h>

#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include "config/InputIDs.hpp"
#include "state/CoreState.hpp"
#include "ui/topbar/TopBar.hpp"
#include "ui/widget/IMacroWidget.hpp"
#include "ui/widget/MacroKnobWidget.hpp"

namespace core::ui {

class MacroView : public oc::ui::lvgl::IView {
public:
    static constexpr uint8_t MACRO_COUNT = Config::MACRO_COUNT;
    static constexpr uint8_t COLS = 4;
    static constexpr uint8_t ROWS = 2;

    MacroView(lv_obj_t* parent, core::state::CoreState& coreState);
    ~MacroView() override;

    // IView interface
    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.macro"; }
    lv_obj_t* getElement() const override { return container_; }

    // Widget access
    IMacroWidget& macro(uint8_t index) { return *macros_[index]; }

private:
    void createLayout(lv_obj_t* parent);
    void createTopBar();
    void createMacros();
    void bindToState();

    // Debounced update system
    void markDirty(uint8_t index);
    void processDirtyFlags();
    static void onUpdateTimer(lv_timer_t* timer);

    core::state::CoreState& core_state_;
    std::vector<oc::state::Subscription> subscriptions_;
    std::array<bool, MACRO_COUNT> dirty_flags_{};
    lv_timer_t* update_timer_ = nullptr;

    // UI structure
    lv_obj_t* container_ = nullptr;
    lv_obj_t* top_bar_container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    std::unique_ptr<TopBar> top_bar_;
    std::array<std::unique_ptr<IMacroWidget>, MACRO_COUNT> macros_;
};

}  // namespace core::ui
```

---

## Checklist

Before committing a new view:

- [ ] Implements `IView` interface (onActivate, onDeactivate, getViewId, getElement)
- [ ] Constructor creates layout, widgets, bindings
- [ ] Destructor cleans up in correct order (subs → widgets → timer → container)
- [ ] `onActivate` clears hidden flag
- [ ] `onDeactivate` sets hidden flag
- [ ] Subscriptions stored in `std::vector<Subscription>`
- [ ] High-frequency updates use debouncing
- [ ] Initial values set after binding (not just waiting for change)
- [ ] ViewId is unique (e.g., "core.volume", "bitwig.device")
- [ ] Rule of 5: copy/move deleted (if applicable)

---

## See Also

- [HOW_TO_ADD_WIDGET.md](HOW_TO_ADD_WIDGET.md) - Creating widgets for views
- [HOW_TO_ADD_HANDLER.md](HOW_TO_ADD_HANDLER.md) - Input handling for views
- [HOW_TO_ADD_OVERLAY.md](HOW_TO_ADD_OVERLAY.md) - Modal overlays on views
- [EXTENSION_CHECKLIST.md](EXTENSION_CHECKLIST.md) - Full checklist for new features
