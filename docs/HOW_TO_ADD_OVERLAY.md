# How To Add an Overlay

> **Type**: Step-by-step Tutorial
> **Audience**: Developers creating modal overlays
> **Time**: 45-60 minutes
> **Prerequisites**: [HOW_TO_ADD_VIEW.md](HOW_TO_ADD_VIEW.md), [HOW_TO_ADD_HANDLER.md](HOW_TO_ADD_HANDLER.md)

This guide explains how to create modal overlays with proper lifecycle and input management.

---

## Table of Contents

1. [Overlay Architecture](#1-overlay-architecture)
2. [Step 1: Define Overlay Type](#step-1-define-overlay-type)
3. [Step 2: Create State for Overlay](#step-2-create-state-for-overlay)
4. [Step 3: Create the Overlay Component](#step-3-create-the-overlay-component)
5. [Step 4: Create the Handler](#step-4-create-the-handler)
6. [Step 5: Wire in Context](#step-5-wire-in-context)
7. [Two-Level Scoping](#two-level-scoping)
8. [Complete Example](#complete-example)

---

## 1. Overlay Architecture

### What is an Overlay?

An overlay is a modal UI element that:
- Appears on top of the current view
- Captures input while visible
- Has a defined lifecycle (show/hide)

### Key Components

```
OverlayController ─────► Manages visibility of overlay types
       │
       ▼
OverlayType ───────────► Enum of all overlay types
       │
       ▼
Overlay Component ─────► UI rendering (stateless, props-based)
       │
       ▼
Overlay Handler ───────► Input bindings for overlay
       │
       ▼
Overlay State ─────────► Signals for overlay data
```

### Props Pattern

Overlays use a **stateless props pattern**:

```cpp
// Stateless: Overlay receives props, renders them
struct MacroEditOverlayProps {
    uint8_t editingIndex;
    uint8_t channel;
    uint8_t cc;
    bool visible;
};

class MacroEditOverlay {
    void render(const MacroEditOverlayProps& props);  // Pure rendering
};
```

**Why props pattern?**
- Clear data flow (state → props → UI)
- No subscriptions in overlay
- Context/orchestrator controls rendering
- Easy to test

---

## Step 1: Define Overlay Type

Add your overlay to the enum:

```cpp
// File: src/ui/OverlayTypes.hpp
#pragma once

#include <cstdint>

namespace core::ui {

/**
 * @brief Overlay types managed by ExclusiveVisibilityStack
 *
 * Convention: <DOMAIN>_SELECTOR or <DOMAIN>_EDIT
 */
enum class OverlayType : uint8_t {
    NONE = 0,           // Must be 0
    PAGE_SELECTOR,
    MACRO_EDIT,
    VOLUME_EDIT,        // ← Add your overlay here
    COUNT               // Must be last
};

}  // namespace core::ui
```

**Rules:**
- `NONE` must equal 0
- `COUNT` must be last (used for array sizing)
- Use descriptive names: `*_SELECTOR`, `*_EDIT`

---

## Step 2: Create State for Overlay

```cpp
// File: src/state/VolumeEditState.hpp
#pragma once

#include <cstdint>
#include <oc/state/Signal.hpp>

namespace core::state {

using oc::state::Signal;

/**
 * @brief Transient state for volume edit overlay
 */
struct VolumeEditState {
    Signal<float> tempLevel{0.75f};    ///< Temporary value being edited
    Signal<bool> isEditing{false};     ///< Whether edit is active

    /// Start editing with initial value
    void startEditing(float currentLevel) {
        tempLevel.set(currentLevel);
        isEditing.set(true);
    }

    /// Clear editing state
    void stopEditing() {
        isEditing.set(false);
    }
};

}  // namespace core::state
```

Add to `CoreState`:

```cpp
// In state/CoreState.hpp
#include "state/VolumeEditState.hpp"

struct CoreState {
    // ... other state
    VolumeEditState volumeEdit;
};
```

---

## Step 3: Create the Overlay Component

### Props Structure

```cpp
// File: src/ui/overlay/VolumeEditOverlay.hpp
#pragma once

#include <memory>
#include <lvgl.h>
#include <oc/ui/lvgl/widget/Label.hpp>

namespace core::ui {

/**
 * @brief Props for VolumeEditOverlay
 */
struct VolumeEditOverlayProps {
    float level = 0.75f;    ///< Volume level [0.0, 1.0]
    bool visible = false;   ///< Overlay visibility

    bool operator==(const VolumeEditOverlayProps& other) const {
        return level == other.level && visible == other.visible;
    }
    bool operator!=(const VolumeEditOverlayProps& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Stateless overlay for editing volume
 */
class VolumeEditOverlay {
public:
    explicit VolumeEditOverlay(lv_obj_t* parent);
    ~VolumeEditOverlay();

    // Non-copyable
    VolumeEditOverlay(const VolumeEditOverlay&) = delete;
    VolumeEditOverlay& operator=(const VolumeEditOverlay&) = delete;

    /**
     * @brief Render with props (pure rendering)
     */
    void render(const VolumeEditOverlayProps& props);

    /**
     * @brief Get element for scoping
     */
    lv_obj_t* getElement() const { return overlay_; }

private:
    void createLayout(lv_obj_t* parent);

    lv_obj_t* overlay_ = nullptr;      ///< Fullscreen backdrop
    lv_obj_t* container_ = nullptr;    ///< Center dialog

    std::unique_ptr<oc::ui::lvgl::Label> title_label_;
    std::unique_ptr<oc::ui::lvgl::Label> value_label_;

    VolumeEditOverlayProps current_props_;  ///< Cache for optimization
};

}  // namespace core::ui
```

### Implementation

```cpp
// File: src/ui/overlay/VolumeEditOverlay.cpp
#include "VolumeEditOverlay.hpp"
#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

namespace core::ui {

namespace Theme = oc::ui::lvgl::BaseTheme;
namespace style = oc::ui::lvgl::style;

VolumeEditOverlay::VolumeEditOverlay(lv_obj_t* parent) {
    createLayout(parent);
}

VolumeEditOverlay::~VolumeEditOverlay() {
    value_label_.reset();
    title_label_.reset();

    if (overlay_) {
        lv_obj_delete(overlay_);
        overlay_ = nullptr;
    }
}

void VolumeEditOverlay::createLayout(lv_obj_t* parent) {
    // Fullscreen semi-transparent backdrop
    overlay_ = lv_obj_create(parent);
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    style::apply(overlay_)
        .bgColor(0x000000)
        .bgOpa(LV_OPA_70);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);  // Start hidden

    // Center dialog container
    container_ = lv_obj_create(overlay_);
    lv_obj_set_size(container_, 200, 100);
    lv_obj_align(container_, LV_ALIGN_CENTER, 0, 0);
    style::apply(container_)
        .bgColor(Theme::color::SURFACE)
        .radius(8)
        .pad(16);

    // Title
    title_label_ = std::make_unique<oc::ui::lvgl::Label>(container_);
    title_label_->setText("Edit Volume");
    lv_obj_align(title_label_->getElement(), LV_ALIGN_TOP_MID, 0, 0);

    // Value
    value_label_ = std::make_unique<oc::ui::lvgl::Label>(container_);
    lv_obj_align(value_label_->getElement(), LV_ALIGN_CENTER, 0, 0);
}

void VolumeEditOverlay::render(const VolumeEditOverlayProps& props) {
    // Optimization: skip if unchanged
    if (props == current_props_) return;

    // Visibility
    if (props.visible) {
        lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    }

    // Update value display
    if (props.visible) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f%%", props.level * 100.0f);
        value_label_->setText(buf);
    }

    current_props_ = props;
}

}  // namespace core::ui
```

---

## Step 4: Create the Handler

### Two-Level Scoping

Overlays typically need two scopes:
1. **Parent view scope**: Trigger to open overlay
2. **Overlay scope**: Actions when overlay is visible

```cpp
// File: src/handler/volume/VolumeEditHandler.hpp
#pragma once

#include <lvgl.h>
#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "state/CoreState.hpp"
#include "ui/OverlayController.hpp"
#include "ui/OverlayTypes.hpp"

namespace core::handler {

class VolumeEditHandler {
public:
    VolumeEditHandler(
        core::state::CoreState& state,
        core::ui::OverlayController<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* viewScope,      // Parent view scope
        lv_obj_t* overlayScope    // Overlay scope
    );

private:
    void setupBindings();
    void openEdit();
    void adjustValue(float delta);
    void saveAndClose();
    void cancel();

    core::state::CoreState& state_;
    core::ui::OverlayController<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;

    lv_obj_t* view_scope_;
    lv_obj_t* overlay_scope_;
};

}  // namespace core::handler
```

```cpp
// File: src/handler/volume/VolumeEditHandler.cpp
#include "VolumeEditHandler.hpp"
#include <oc/ui/lvgl/Scope.hpp>

using oc::ui::lvgl::scope;

namespace core::handler {

VolumeEditHandler::VolumeEditHandler(
    core::state::CoreState& state,
    core::ui::OverlayController<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* viewScope,
    lv_obj_t* overlayScope)
    : state_(state)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , view_scope_(viewScope)
    , overlay_scope_(overlayScope)
{
    setupBindings();
}

void VolumeEditHandler::setupBindings() {
    // ===== VIEW SCOPE: Open trigger =====
    buttons_.button(Config::ButtonID::VOLUME)
        .longPress()
        .threshold(500)
        .scope(scope(view_scope_))
        .then([this]() { openEdit(); });

    // ===== OVERLAY SCOPE: Edit actions =====
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope(overlay_scope_))
        .then([this](float delta) { adjustValue(delta); });

    buttons_.button(Config::ButtonID::CONFIRM)
        .press()
        .scope(scope(overlay_scope_))
        .then([this]() { saveAndClose(); });

    buttons_.button(Config::ButtonID::CANCEL)
        .press()
        .scope(scope(overlay_scope_))
        .then([this]() { cancel(); });
}

void VolumeEditHandler::openEdit() {
    state_.volumeEdit.startEditing(state_.volume.level.get());
    overlays_.show(core::ui::OverlayType::VOLUME_EDIT);
}

void VolumeEditHandler::adjustValue(float delta) {
    float current = state_.volumeEdit.tempLevel.get();
    float newLevel = std::clamp(current + delta * 0.01f, 0.0f, 1.0f);
    state_.volumeEdit.tempLevel.set(newLevel);
}

void VolumeEditHandler::saveAndClose() {
    state_.volume.level.set(state_.volumeEdit.tempLevel.get());
    overlays_.hide();
}

void VolumeEditHandler::cancel() {
    overlays_.hide();
}

}  // namespace core::handler
```

---

## Step 5: Wire in Context

```cpp
// In Context initialization

void MyContext::createOverlays() {
    // Create overlay component
    volume_edit_overlay_ = std::make_unique<VolumeEditOverlay>(
        lv_screen_active()  // Parent is screen
    );

    // Register with OverlayController
    overlays_.registerCleanup(
        OverlayType::VOLUME_EDIT,
        overlays_.getScopeFor(OverlayType::VOLUME_EDIT),
        0  // No latch button
    );
}

void MyContext::createHandlers() {
    // Handler with two scopes
    volume_edit_handler_ = std::make_unique<VolumeEditHandler>(
        core_state_,
        overlays_,
        encoders(),
        buttons(),
        view_->getElement(),                    // View scope
        volume_edit_overlay_->getElement()      // Overlay scope
    );
}

void MyContext::setupOverlayRendering() {
    // Subscribe to state changes and render
    volume_edit_subs_.push_back(
        core_state_.volumeEdit.tempLevel.subscribe([this](float) {
            renderVolumeEdit();
        })
    );
}

void MyContext::renderVolumeEdit() {
    VolumeEditOverlayProps props{
        .level = core_state_.volumeEdit.tempLevel.get(),
        .visible = overlays_.isCurrent(OverlayType::VOLUME_EDIT)
    };
    volume_edit_overlay_->render(props);
}
```

---

## Two-Level Scoping

### Why Two Scopes?

```
┌─────────────────────────────────────────────────┐
│ View (view_scope_)                               │
│                                                  │
│  Button press → Opens overlay                    │
│                                                  │
│  ┌─────────────────────────────────────────┐    │
│  │ Overlay (overlay_scope_)                │    │
│  │                                          │    │
│  │  Encoder → Adjust value                  │    │
│  │  Confirm → Save and close                │    │
│  │  Cancel → Close without save             │    │
│  │                                          │    │
│  └─────────────────────────────────────────┘    │
│                                                  │
└─────────────────────────────────────────────────┘
```

### Authority Flow

1. When overlay is hidden, **view scope** has authority
2. When overlay shows, **overlay scope** gets authority
3. View scope bindings are blocked while overlay visible
4. When overlay hides, authority returns to view

---

## Complete Example

See [MacroEditOverlay.hpp](../src/ui/macro/MacroEditOverlay.hpp) and [MacroEditHandler.cpp](../src/handler/macro/MacroEditHandler.cpp) for a full implementation.

**Key files:**
- `src/ui/OverlayTypes.hpp` - Overlay enum
- `src/state/MacroEditState.hpp` - Overlay state
- `src/ui/macro/MacroEditOverlay.hpp/.cpp` - Overlay component
- `src/handler/macro/MacroEditHandler.hpp/.cpp` - Handler

---

## Checklist

Before committing a new overlay:

- [ ] Added to `OverlayType` enum (before COUNT)
- [ ] Created state struct with Signals
- [ ] Created Props struct with `operator==`
- [ ] Overlay is stateless (no subscriptions)
- [ ] `render()` method optimizes with props cache
- [ ] Overlay starts hidden (`LV_OBJ_FLAG_HIDDEN`)
- [ ] Handler has two scopes (view + overlay)
- [ ] Handler uses `overlays_.show()` and `overlays_.hide()`
- [ ] Registered cleanup with `OverlayController`
- [ ] Context subscribes to state and calls `render()`
- [ ] Destructor cleans up LVGL objects

---

## See Also

- [INVARIANTS.md](INVARIANTS.md) - Overlay lifecycle rules
- [EXTENSION_CHECKLIST.md](EXTENSION_CHECKLIST.md) - Complete feature checklist
- [HOW_TO_ADD_HANDLER.md](HOW_TO_ADD_HANDLER.md) - Input binding patterns
