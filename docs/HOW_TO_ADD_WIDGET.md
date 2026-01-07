# How To Add a Widget

> **Type**: Step-by-step Tutorial
> **Audience**: Developers creating new UI components
> **Time**: 30-60 minutes
> **Prerequisites**: [STATE_MANAGEMENT.md](STATE_MANAGEMENT.md)

This guide walks through creating a new widget from scratch, using the existing `MacroKnobWidget` as a reference.

---

## Table of Contents

1. [Widget Architecture](#1-widget-architecture)
2. [Step 1: Define the Interface](#step-1-define-the-interface)
3. [Step 2: Create the Base Class (optional)](#step-2-create-the-base-class-optional)
4. [Step 3: Implement the Widget](#step-3-implement-the-widget)
5. [Step 4: Add to a View](#step-4-add-to-a-view)
6. [LVGL Patterns](#lvgl-patterns)
7. [Complete Example](#complete-example)

---

## 1. Widget Architecture

### Widget Hierarchy

```
oc::ui::lvgl::IWidget          (framework interface)
       ▲
       │
core::ui::IMacroWidget         (domain interface)
       ▲
       │
core::ui::BaseMacroWidget      (shared implementation)
       ▲
       ├──────────────┐
       │              │
MacroKnobWidget  MacroButtonWidget  (concrete widgets)
```

### Key Principles

| Principle | Description |
|-----------|-------------|
| **Interface-first** | Define `I*Widget` interface for polymorphism |
| **Base class for DRY** | Extract common code to `Base*Widget` |
| **Composition over inheritance** | Widgets wrap framework widgets (`KnobWidget`, `Label`) |
| **RAII cleanup** | Destructor cleans up LVGL objects |

---

## Step 1: Define the Interface

Create an interface that extends `IWidget`:

```cpp
// File: src/ui/widget/IVolumeWidget.hpp
#pragma once

#include <cstdint>
#include <oc/ui/lvgl/IWidget.hpp>

namespace core::ui {

/**
 * @brief Interface for volume widgets
 */
class IVolumeWidget : public oc::ui::lvgl::IWidget {
public:
    ~IVolumeWidget() override = default;

    /// Set the volume level [0.0, 1.0]
    virtual void setLevel(float level) = 0;

    /// Set muted state
    virtual void setMuted(bool muted) = 0;
};

}  // namespace core::ui
```

**Interface guidelines:**

- Inherit from `oc::ui::lvgl::IWidget`
- Keep methods pure virtual
- Use normalized values (0.0 to 1.0) for continuous parameters
- Use semantic names (`setLevel`, not `setValue`)

---

## Step 2: Create the Base Class (optional)

If multiple widgets share code, create a base class:

```cpp
// File: src/ui/widget/BaseVolumeWidget.hpp
#pragma once

#include <memory>
#include <oc/ui/lvgl/Label.hpp>
#include "ui/widget/IVolumeWidget.hpp"

namespace core::ui {

/**
 * @brief Base implementation for volume widgets
 */
class BaseVolumeWidget : public IVolumeWidget {
public:
    ~BaseVolumeWidget() override;

    // Non-copyable, non-movable
    BaseVolumeWidget(const BaseVolumeWidget&) = delete;
    BaseVolumeWidget& operator=(const BaseVolumeWidget&) = delete;
    BaseVolumeWidget(BaseVolumeWidget&&) = delete;
    BaseVolumeWidget& operator=(BaseVolumeWidget&&) = delete;

    // IWidget
    lv_obj_t* getElement() const override { return container_; }

    // IVolumeWidget
    void setMuted(bool muted) override;

protected:
    BaseVolumeWidget();  // Protected: only derived classes instantiate

    void createContainer(lv_obj_t* parent);
    void createMuteIndicator();

    lv_obj_t* container_ = nullptr;
    std::unique_ptr<oc::ui::lvgl::Label> mute_indicator_;
};

}  // namespace core::ui
```

```cpp
// File: src/ui/widget/BaseVolumeWidget.cpp
#include "BaseVolumeWidget.hpp"
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

namespace core::ui {

namespace style = oc::ui::lvgl::style;

BaseVolumeWidget::BaseVolumeWidget() = default;

BaseVolumeWidget::~BaseVolumeWidget() {
    mute_indicator_.reset();

    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}

void BaseVolumeWidget::createContainer(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    style::apply(container_).transparent().noScroll();
}

void BaseVolumeWidget::createMuteIndicator() {
    mute_indicator_ = std::make_unique<oc::ui::lvgl::Label>(container_);
    mute_indicator_->setText("M");
    lv_obj_add_flag(mute_indicator_->getElement(), LV_OBJ_FLAG_HIDDEN);
}

void BaseVolumeWidget::setMuted(bool muted) {
    if (mute_indicator_) {
        if (muted) {
            lv_obj_clear_flag(mute_indicator_->getElement(), LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(mute_indicator_->getElement(), LV_OBJ_FLAG_HIDDEN);
        }
    }
}

}  // namespace core::ui
```

---

## Step 3: Implement the Widget

Create the concrete widget:

```cpp
// File: src/ui/widget/VolumeSliderWidget.hpp
#pragma once

#include <memory>
#include <oc/ui/lvgl/SliderWidget.hpp>
#include "ui/widget/BaseVolumeWidget.hpp"

namespace core::ui {

/**
 * @brief Volume slider widget with mute indicator
 */
class VolumeSliderWidget : public BaseVolumeWidget {
public:
    VolumeSliderWidget(lv_obj_t* parent);
    ~VolumeSliderWidget() override;

    // IVolumeWidget
    void setLevel(float level) override;

private:
    void createUI(lv_obj_t* parent);

    std::unique_ptr<oc::ui::lvgl::SliderWidget> slider_;
};

}  // namespace core::ui
```

```cpp
// File: src/ui/widget/VolumeSliderWidget.cpp
#include "VolumeSliderWidget.hpp"
#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

namespace core::ui {

namespace Theme = oc::ui::lvgl::BaseTheme;
namespace style = oc::ui::lvgl::style;

VolumeSliderWidget::VolumeSliderWidget(lv_obj_t* parent) {
    createUI(parent);
}

VolumeSliderWidget::~VolumeSliderWidget() {
    slider_.reset();
    // BaseVolumeWidget destructor handles container cleanup
}

void VolumeSliderWidget::createUI(lv_obj_t* parent) {
    createContainer(parent);

    // Create slider
    slider_ = std::make_unique<oc::ui::lvgl::SliderWidget>(container_);
    slider_->bgColor(Theme::color::KNOB_BACKGROUND)
           .trackColor(Theme::color::ACCENT);
    lv_obj_set_size(slider_->getElement(), LV_PCT(80), 20);
    lv_obj_align(slider_->getElement(), LV_ALIGN_CENTER, 0, 0);

    // Create mute indicator from base
    createMuteIndicator();
    lv_obj_align(mute_indicator_->getElement(), LV_ALIGN_TOP_RIGHT, -4, 4);
}

void VolumeSliderWidget::setLevel(float level) {
    if (slider_) {
        slider_->setValue(level);
    }
}

}  // namespace core::ui
```

---

## Step 4: Add to a View

Use the widget in a View:

```cpp
// In VolumeView.hpp
#include "ui/widget/VolumeSliderWidget.hpp"

class VolumeView : public IView {
private:
    std::unique_ptr<VolumeSliderWidget> volume_widget_;
};
```

```cpp
// In VolumeView.cpp
void VolumeView::createWidgets() {
    volume_widget_ = std::make_unique<VolumeSliderWidget>(container_);

    // Position in grid
    lv_obj_set_grid_cell(volume_widget_->getElement(),
        LV_GRID_ALIGN_STRETCH, 0, 1,
        LV_GRID_ALIGN_STRETCH, 0, 1);
}

void VolumeView::bindToState() {
    subs_.push_back(state_.volume.level.subscribe([this](float level) {
        volume_widget_->setLevel(level);
    }));

    subs_.push_back(state_.volume.muted.subscribe([this](bool muted) {
        volume_widget_->setMuted(muted);
    }));
}
```

---

## LVGL Patterns

### StyleBuilder API

Use `StyleBuilder` for consistent styling:

```cpp
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

namespace style = oc::ui::lvgl::style;

// Create and style in one chain
lv_obj_t* container = lv_obj_create(parent);
style::apply(container)
    .fullSize()           // width/height = 100%
    .transparent()        // no bg, no border
    .pad(8)               // padding all sides
    .noScroll();          // disable scrollbars
```

### Common StyleBuilder Methods

| Method | Effect |
|--------|--------|
| `.fullSize()` | `lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100))` |
| `.transparent()` | No background, no border |
| `.pad(n)` | Padding all sides |
| `.bgColor(c)` | Background color |
| `.textOpa(o)` | Text opacity |
| `.noScroll()` | Disable scrollbars |

### Grid Layout

```cpp
// Define grid: 2 columns, 1 row
static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static const int32_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

lv_obj_set_grid_dsc_array(container, col_dsc, row_dsc);
lv_obj_set_layout(container, LV_LAYOUT_GRID);

// Position child in grid
lv_obj_set_grid_cell(child,
    LV_GRID_ALIGN_STRETCH, 0, 1,  // column: stretch, col 0, span 1
    LV_GRID_ALIGN_CENTER, 0, 1);  // row: center, row 0, span 1
```

### Flex Layout

```cpp
lv_obj_set_layout(container, LV_LAYOUT_FLEX);
lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(container,
    LV_FLEX_ALIGN_CENTER,   // main axis (vertical)
    LV_FLEX_ALIGN_CENTER,   // cross axis (horizontal)
    LV_FLEX_ALIGN_CENTER);  // track cross axis

// Child grows to fill space
lv_obj_set_flex_grow(child, 1);
```

### Floating Elements

For overlay elements that don't affect layout:

```cpp
lv_obj_t* overlay = lv_obj_create(parent);
lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
lv_obj_align(overlay, LV_ALIGN_BOTTOM_MID, 0, -8);  // Offset from anchor
```

### Visibility

```cpp
// Hide
lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);

// Show
lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
```

---

## Complete Example

### File Structure

```
src/ui/widget/
├── IVolumeWidget.hpp        # Interface
├── BaseVolumeWidget.hpp     # Base class (header)
├── BaseVolumeWidget.cpp     # Base class (impl)
├── VolumeSliderWidget.hpp   # Concrete widget (header)
└── VolumeSliderWidget.cpp   # Concrete widget (impl)
```

### Checklist

Before committing a new widget:

- [ ] Interface defined with pure virtual methods
- [ ] Inherits from `oc::ui::lvgl::IWidget`
- [ ] `getElement()` returns the root LVGL object
- [ ] Destructor cleans up LVGL objects in correct order
- [ ] Rule of 5: copy/move deleted
- [ ] Uses `StyleBuilder` for styling
- [ ] Private members use `snake_case_` suffix
- [ ] Header has `@file` and `@brief` documentation

---

## See Also

- [STATE_MANAGEMENT.md](STATE_MANAGEMENT.md) - Binding widgets to state
- [HOW_TO_ADD_VIEW.md](HOW_TO_ADD_VIEW.md) - Views that contain widgets
- [CODE_STYLE.md](CODE_STYLE.md) - Naming conventions
