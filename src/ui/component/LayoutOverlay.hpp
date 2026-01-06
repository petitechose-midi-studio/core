#pragma once

/**
 * @file LayoutOverlay.hpp
 * @brief Base layout overlay component
 */

#include <lvgl.h>
#include <oc/ui/lvgl/IComponent.hpp>

namespace core::ui {

/**
 * Base layout shell for modal overlays.
 *
 * Provides a pre-configured overlay structure with:
 * - Fullscreen semi-transparent background
 * - Flex column container with header/content/footer slots
 * - Auto-collapse: empty slots take no space
 * - Explicit show/hide control for each slot
 *
 * Usage:
 * @code
 * LayoutOverlay overlay(parent);
 * auto* title = new TrackTitleItem(overlay.header());
 * auto* list = new VirtualList(overlay.content());
 * auto* hints = new HintBar(overlay.footer());
 * overlay.show();
 * @endcode
 */
class LayoutOverlay : public oc::ui::lvgl::IComponent {
public:
    explicit LayoutOverlay(lv_obj_t* parent);
    ~LayoutOverlay() override;

    LayoutOverlay(const LayoutOverlay&) = delete;
    LayoutOverlay& operator=(const LayoutOverlay&) = delete;

    // Slot accessors - parent your widgets to these
    lv_obj_t* header() const { return header_; }
    lv_obj_t* content() const { return content_; }
    lv_obj_t* footer() const { return footer_; }

    // Slot visibility control
    void showHeader(bool show = true);
    void showFooter(bool show = true);

    // IComponent
    void show() override;
    void hide() override;
    bool isVisible() const override { return visible_; }
    lv_obj_t* getElement() const override { return overlay_; }

private:
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* header_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* footer_ = nullptr;
    bool visible_ = false;
};

}  // namespace core::ui
