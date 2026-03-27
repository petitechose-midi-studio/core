#pragma once

/**
 * @file StepPropertyStrip.hpp
 * @brief Inline step-property selector strip for Sequencer view
 */

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/sequencer/SequencerState.hpp"

namespace core::ui {

struct StepPropertyStripProps {
    core::state::sequencer::StepProperty activeProperty =
        core::state::sequencer::StepProperty::NOTE;
    bool selecting = false;
    int selectedIndex = 0;
};

class StepPropertyStrip : public oc::ui::lvgl::IWidget {
public:
    explicit StepPropertyStrip(lv_obj_t* parent);
    ~StepPropertyStrip() override;

    StepPropertyStrip(const StepPropertyStrip&) = delete;
    StepPropertyStrip& operator=(const StepPropertyStrip&) = delete;

    void render(const StepPropertyStripProps& props);
    void setSelectorScopeVisible(bool visible);

    lv_obj_t* getElement() const override { return container_; }
    lv_obj_t* getSelectorScopeElement() const { return selector_scope_; }

private:
    static constexpr size_t PROPERTY_COUNT = 5;

    void createUI(lv_obj_t* parent);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* selection_cursor_ = nullptr;
    lv_obj_t* selector_scope_ = nullptr;
    std::array<lv_obj_t*, PROPERTY_COUNT> items_{};
    std::array<lv_obj_t*, PROPERTY_COUNT> icons_{};
};

}  // namespace core::ui
