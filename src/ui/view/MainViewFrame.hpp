#pragma once

#include <lvgl.h>

#include <ms/ui/component/LayoutView.hpp>

#include "app/ExtmemAllocator.hpp"

namespace core::ui {

/**
 * Shared LVGL frame layout for retained standalone main views.
 *
 * The frame owns the common header/body/interaction containers. View
 * classes populate those containers with domain-specific widgets. Its root is
 * intentionally opaque so LVGL can use it as a full-view coverage boundary.
 */
class MainViewFrame {
public:
    explicit MainViewFrame(lv_obj_t* parent);
    ~MainViewFrame() = default;

    MainViewFrame(const MainViewFrame&) = delete;
    MainViewFrame& operator=(const MainViewFrame&) = delete;

    [[nodiscard]] bool valid() const { return valid_; }
    lv_obj_t* container() const { return container_; }
    lv_obj_t* header() const { return header_; }
    lv_obj_t* body() const { return body_; }
    lv_obj_t* interactionRow() const { return interaction_row_; }
    lv_obj_t* centerColumn() const { return center_column_; }

    void createInteractionRow();
    void createCenterColumn();

private:
    core::app::ExtmemUniquePtr<ms::ui::LayoutView> layout_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* header_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* interaction_row_ = nullptr;
    lv_obj_t* center_column_ = nullptr;
    bool valid_ = false;
};

}  // namespace core::ui
