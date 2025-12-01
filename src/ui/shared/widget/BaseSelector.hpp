#pragma once

#include "../interface/ISelector.hpp"
#include "ListOverlay.hpp"
#include <memory>

/**
 * Base implementation for list selectors.
 * Handles ListOverlay, navigation, and optional footer.
 * Subclasses implement item-specific rendering.
 */
class BaseSelector : public ISelector {
public:
    explicit BaseSelector(lv_obj_t *parent);
    ~BaseSelector() override;

    void setTitle(const std::string &title) override;
    void setSelectedIndex(int index) override;
    int getSelectedIndex() const override;
    int getItemCount() const override;

    void show() override;
    void hide() override;
    bool isVisible() const override;
    lv_obj_t *getElement() const override;

protected:
    ListOverlay &overlay() { return overlay_; }
    const ListOverlay &overlay() const { return overlay_; }

    lv_obj_t *parent_ = nullptr;
    ListOverlay overlay_;
};
