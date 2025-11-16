#pragma once

#include <lvgl.h>
#include <string>
#include <vector>
#include "../interface/IComponent.hpp"

/**
 * @brief Pure UI widget for modal list overlay with selection
 *
 * Displays a centered modal overlay containing a scrollable list of items.
 * Supports visual selection highlighting via index.
 *
 * PURE UI - No logic, no callbacks, only setters/getters.
 *
 * Usage:
 *   ListOverlay overlay(parent);
 *   overlay.setTitle("Select Page");
 *   overlay.setItems({"Page 1", "Page 2", "Page 3"});
 *   overlay.setSelectedIndex(0);
 *   overlay.show();
 *
 *   // Later, from controller:
 *   int newIndex = overlay.getSelectedIndex() + 1;
 *   overlay.setSelectedIndex(newIndex);
 */
class ListOverlay : public UI::IComponent {
public:
    /**
     * @brief Construct list overlay
     * @param parent Parent LVGL object (typically screen)
     */
    explicit ListOverlay(lv_obj_t* parent);

    /**
     * @brief Destructor - cleans up LVGL objects
     */
    ~ListOverlay();

    /**
     * @brief Set overlay title (optional)
     * @param title Title text (empty to hide title)
     */
    void setTitle(const std::string& title);

    /**
     * @brief Set list items
     * @param items Vector of item labels
     *
     * Note: Recreates list if already shown. Call before show() if possible.
     */
    void setItems(const std::vector<std::string>& items);

    /**
     * @brief Set selected item index
     * @param index Item index (0-based), clamped to valid range
     *
     * Updates visual highlight and scrolls to make item visible.
     */
    void setSelectedIndex(int index);

    /**
     * @brief Show overlay (creates UI if not exists)
     */
    void show();

    /**
     * @brief Hide overlay (keeps objects, just hidden)
     */
    void hide();

    /**
     * @brief Check if overlay is currently visible
     * @return true if visible, false if hidden
     */
    bool isVisible() const;

    /**
     * @brief Get currently selected index
     * @return Selected index (0-based), -1 if no items
     */
    int getSelectedIndex() const;

    /**
     * @brief Get number of items in list
     * @return Item count
     */
    int getItemCount() const;

    /**
     * @brief Get button object at index (for advanced customization)
     *
     * Allows wrapper classes to add custom widgets to buttons (e.g., indicators).
     *
     * @param index Button index (0-based)
     * @return Button object or nullptr if invalid index
     */
    lv_obj_t* getButton(size_t index) const;

    /**
     * @brief Get underlying LVGL element (from IElement)
     * @return Overlay object (nullptr if not created)
     *
     * Implements IElement::getElement(). Returns the modal overlay container
     * which can be used for scoped bindings.
     */
    lv_obj_t* getElement() const override { return overlay_; }

private:
    void createOverlay();
    void createTitleLabel();
    void createList();
    void populateList();

    void updateHighlight();
    void scrollToSelected();

    void destroyList();
    void cleanup();

    lv_obj_t* parent_ = nullptr;

    lv_obj_t* overlay_ = nullptr;      // Modal background
    lv_obj_t* container_ = nullptr;    // Content container
    lv_obj_t* title_label_ = nullptr;  // Optional title
    lv_obj_t* list_ = nullptr;         // LVGL list widget

    std::vector<lv_obj_t*> buttons_;

    std::vector<lv_obj_t*> bullets_;

    std::vector<std::string> items_;
    std::string title_;
    int selected_index_ = 0;
    bool visible_ = false;
    bool ui_created_ = false;
};
