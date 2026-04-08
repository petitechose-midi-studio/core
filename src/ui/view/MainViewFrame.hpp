#pragma once

#include <memory>

#include <lvgl.h>

#include <ms/ui/component/LayoutView.hpp>

namespace core::ui {

class MainViewFrame {
public:
    explicit MainViewFrame(lv_obj_t* parent);
    ~MainViewFrame() = default;

    MainViewFrame(const MainViewFrame&) = delete;
    MainViewFrame& operator=(const MainViewFrame&) = delete;

    lv_obj_t* container() const { return container_; }
    lv_obj_t* header() const { return header_; }
    lv_obj_t* body() const { return body_; }
    lv_obj_t* interactionRow() const { return interaction_row_; }
    lv_obj_t* centerColumn() const { return center_column_; }

    void createInteractionRow();
    void createCenterColumn();

private:
    std::unique_ptr<ms::ui::LayoutView> layout_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* header_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* interaction_row_ = nullptr;
    lv_obj_t* center_column_ = nullptr;
};

}  // namespace core::ui
