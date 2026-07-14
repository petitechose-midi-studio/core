#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>
#include <oc/ui/lvgl/IWidget.hpp>

#include "ui/macro/MacroEditorPreviewModel.hpp"

namespace core::ui {

struct MacroEditorOverlayProps {
    bool visible = false;
    const char* title = "";
    const char* meta = "";
    const char* destination = "";
    const char* automation = "";
    const char* modulation = "";
    int selectedDomain = 0;
    const MacroEditorPreviewModel* preview = nullptr;
    uint32_t previewRevision = 0;
    uint32_t dataRevision = 0;
};

/** Semantic Macro editor: three direct domains plus Base/Mod/Out preview. */
class MacroEditorOverlay : public oc::ui::lvgl::IWidget {
public:
    explicit MacroEditorOverlay(lv_obj_t* parent);
    ~MacroEditorOverlay() override;

    MacroEditorOverlay(const MacroEditorOverlay&) = delete;
    MacroEditorOverlay& operator=(const MacroEditorOverlay&) = delete;

    void render(const MacroEditorOverlayProps& props);
    lv_obj_t* getElement() const override { return root_; }

private:
    struct TabWidgets {
        lv_obj_t* root = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* label = nullptr;
        lv_obj_t* value = nullptr;
        lv_obj_t* state = nullptr;
        std::array<char, 24> valueText{};
    };

    void createUi(lv_obj_t* parent);
    void createTab(size_t index,
                   const char* icon,
                   const char* label,
                   uint32_t color);
    void renderTab(size_t index,
                   const char* value,
                   bool selected,
                   bool stored,
                   bool playback,
                   uint32_t color);
    void renderGraph(const MacroEditorOverlayProps& props, bool previewChanged);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* meta_ = nullptr;
    std::array<TabWidgets, 3> tabs_{};
    lv_obj_t* graph_ = nullptr;
    lv_obj_t* automation_line_ = nullptr;
    lv_obj_t* base_line_ = nullptr;
    lv_obj_t* modulated_line_ = nullptr;
    std::array<lv_point_precise_t, MACRO_EDITOR_PREVIEW_SAMPLE_COUNT>
        automation_points_{};
    std::array<lv_point_precise_t, MACRO_EDITOR_PREVIEW_SAMPLE_COUNT>
        base_points_{};
    std::array<lv_point_precise_t, MACRO_EDITOR_PREVIEW_SAMPLE_COUNT>
        out_points_{};
    lv_obj_t* clipping_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    std::array<char, 24> titleText_{};
    std::array<char, 24> metaText_{};
    uint32_t renderedRevision_ = UINT32_MAX;
    uint32_t renderedPreviewRevision_ = UINT32_MAX;
    bool visible_ = false;
};

}  // namespace core::ui
