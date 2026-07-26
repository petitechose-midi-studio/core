#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/project/ProjectTrackEditorState.hpp"

namespace core::ui::project {

/** Semantic, allocation-free input for the retained Track Editor surface. */
struct ProjectTrackEditorOverlayProps {
    bool visible = false;
    const char* title = "";
    const char* route = "";
    const char* delay = "";
    const char* structureHint = "";
    uint32_t trackColor = 0U;
    core::state::project::ProjectTrackEditorProperty selectedProperty =
        core::state::project::ProjectTrackEditorProperty::CHANNEL;
    bool muted = false;
    bool soloed = false;
    bool trackEnabled = false;
};

/**
 * Retained 320x240 Track Editor surface.
 *
 * The view uses one custom-drawn surface rather than a row of LVGL children.
 * Rendering only copies bounded strings and invalidates when semantic props
 * change, so steady frames perform neither allocation nor layout work.
 */
class ProjectTrackEditorOverlay final : public oc::ui::lvgl::IWidget {
public:
    explicit ProjectTrackEditorOverlay(lv_obj_t* parent);
    ~ProjectTrackEditorOverlay() override;

    ProjectTrackEditorOverlay(const ProjectTrackEditorOverlay&) = delete;
    ProjectTrackEditorOverlay& operator=(const ProjectTrackEditorOverlay&) = delete;

    void render(const ProjectTrackEditorOverlayProps& props);

    lv_obj_t* getElement() const override { return root_; }

private:
    struct RenderCache {
        std::array<char, 16> title{};
        std::array<char, 24> route{};
        std::array<char, 16> delay{};
        std::array<char, 24> structureHint{};
        uint32_t trackColor = 0U;
        core::state::project::ProjectTrackEditorProperty selectedProperty =
            core::state::project::ProjectTrackEditorProperty::CHANNEL;
        bool muted = false;
        bool soloed = false;
        bool trackEnabled = false;

        friend bool operator==(const RenderCache& left, const RenderCache& right) {
            return left.title == right.title && left.route == right.route &&
                left.delay == right.delay &&
                left.structureHint == right.structureHint &&
                left.trackColor == right.trackColor &&
                left.selectedProperty == right.selectedProperty &&
                left.muted == right.muted && left.soloed == right.soloed &&
                left.trackEnabled == right.trackEnabled;
        }
    };

    void createUi(lv_obj_t* parent);
    void draw(lv_layer_t* layer) const;
    static void onDraw(lv_event_t* event);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* surface_ = nullptr;
    RenderCache cache_{};
    bool visible_ = false;
    bool rendered_ = false;
};

}  // namespace core::ui::project
