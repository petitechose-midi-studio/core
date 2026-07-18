#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>
#include <oc/ui/lvgl/IWidget.hpp>

namespace core::ui::sequencer {

struct SequencerStructureWorkspaceItemProps {
    bool visible = false;
    bool focused = false;
    bool active = false;
    bool add = false;
    std::array<char, 12> label{};
};

struct SequencerStructureWorkspaceProps {
    static constexpr uint8_t ITEM_COUNT = 16;

    bool visible = false;
    std::array<char, 28> breadcrumb{};
    std::array<char, 24> context{};
    std::array<SequencerStructureWorkspaceItemProps, ITEM_COUNT> items{};
};

/** Retained, bounded Structure surface; render performs no allocation. */
class SequencerStructureWorkspace : public oc::ui::lvgl::IWidget {
public:
    explicit SequencerStructureWorkspace(lv_obj_t* parent);
    ~SequencerStructureWorkspace() override;

    SequencerStructureWorkspace(const SequencerStructureWorkspace&) = delete;
    SequencerStructureWorkspace& operator=(const SequencerStructureWorkspace&) = delete;

    void render(const SequencerStructureWorkspaceProps& props);
    lv_obj_t* getElement() const override { return container_; }

private:
    void createUi(lv_obj_t* parent);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* breadcrumb_ = nullptr;
    lv_obj_t* context_ = nullptr;
    lv_obj_t* grid_ = nullptr;
    std::array<lv_obj_t*, SequencerStructureWorkspaceProps::ITEM_COUNT> cells_{};
    std::array<lv_obj_t*, SequencerStructureWorkspaceProps::ITEM_COUNT> labels_{};
    std::array<lv_obj_t*, SequencerStructureWorkspaceProps::ITEM_COUNT> markers_{};
    bool visible_cache_ = false;
};

}  // namespace core::ui::sequencer
