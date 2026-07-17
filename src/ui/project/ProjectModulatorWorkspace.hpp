#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <lvgl.h>
#include <ms/ui/widget/CurvePreviewWidget.hpp>
#include <oc/ui/lvgl/PausableTimer.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/modulation/ProjectControlState.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

namespace core::ui::project {

struct ProjectModulatorWorkspaceProps {
    bool visible = false;
    const core::state::modulation::ProjectControlState* control = nullptr;
    const core::state::modulation::ModulatorSourceState* source = nullptr;
    bool options = false;
    uint8_t selectedIndex = 0U;
    uint8_t telemetryRevision = 0U;
};

/**
 * Retained source-first Modulator editor.
 *
 * This widget owns presentation state only. Its fixed caches and the shared
 * curve geometry live with the PSRAM-allocated owner; render() creates no
 * LVGL object and performs no heap allocation.
 */
class ProjectModulatorWorkspace final {
public:
    explicit ProjectModulatorWorkspace(lv_obj_t* parent);
    ~ProjectModulatorWorkspace();

    ProjectModulatorWorkspace(const ProjectModulatorWorkspace&) = delete;
    ProjectModulatorWorkspace& operator=(
        const ProjectModulatorWorkspace&
    ) = delete;

    [[nodiscard]] bool valid() const;
    [[nodiscard]] lv_obj_t* getElement() const { return root_; }
    void render(const ProjectModulatorWorkspaceProps& props);

private:
    static constexpr uint8_t CARD_CAPACITY = 5U;
    static constexpr uint32_t EDIT_FEEDBACK_MS = 900U;
    static constexpr uint32_t EDIT_FEEDBACK_POLL_MS = 50U;

    struct CardWidgets {
        lv_obj_t* root = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* label = nullptr;
        lv_obj_t* value = nullptr;
        std::array<char, 8> iconText{};
        std::array<char, 32> labelText{};
        std::array<char, 32> valueText{};
    };

    struct CurveSampleContext {
        const core::state::modulation::ProjectControlState* control = nullptr;
        const core::state::modulation::ModulatorSourceState* source = nullptr;
        uint16_t previousValue = 0U;
        bool hasPrevious = false;
    };

    void createUi(lv_obj_t* parent);
    void createCard(uint8_t index);
    void renderHeader(
        const core::state::modulation::ModulatorSourceState& source
    );
    void renderCards(const ProjectModulatorWorkspaceProps& props);
    void renderCurve(const ProjectModulatorWorkspaceProps& props);
    void showEditFeedback(
        const ProjectModulatorWorkspaceProps& props,
        bool sourceChanged
    );
    void hideEditFeedback();
    static void onEditFeedbackTimeout(lv_timer_t* timer);
    static bool sampleCurve(
        void* context,
        uint16_t positionQ16,
        ms::ui::CurvePreviewSample& out
    );

    lv_obj_t* root_ = nullptr;
    lv_obj_t* source_icon_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* state_icon_ = nullptr;
    lv_obj_t* state_text_ = nullptr;
    core::app::ExtmemUniquePtr<ms::ui::CurvePreviewWidget> curve_preview_;
    std::array<CardWidgets, CARD_CAPACITY> cards_{};
    lv_obj_t* edit_feedback_ = nullptr;
    lv_obj_t* edit_feedback_key_ = nullptr;
    lv_obj_t* edit_feedback_value_ = nullptr;
    std::optional<oc::ui::lvgl::PausableTimer> edit_feedback_timer_;

    CurveSampleContext curve_sample_context_{};
    core::state::modulation::ModulatorSourceState rendered_source_{};
    std::array<char, 24> titleText_{};
    std::array<char, 16> stateText_{};
    std::array<char, 32> editFeedbackKeyText_{};
    std::array<char, 32> editFeedbackValueText_{};
    core::state::modulation::ModulatorId rendered_source_id_{};
    uint32_t edit_feedback_deadline_ms_ = 0U;
    uint8_t rendered_selected_index_ = UINT8_MAX;
    bool rendered_options_ = false;
    bool has_rendered_source_ = false;
    bool visible_ = false;
};

static_assert(
    sizeof(ProjectModulatorWorkspace) <= 1280U,
    "Project Modulator workspace exceeds its retained PSRAM owner budget"
);

}  // namespace core::ui::project
