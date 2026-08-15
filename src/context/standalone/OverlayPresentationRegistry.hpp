#pragma once

#include <array>
#include <cstddef>

#include <lvgl.h>

#include <oc/type/Ids.hpp>
#include <oc/ui/lvgl/RetainedSurfaceParkingLot.hpp>

#include "app/OverlayTypes.hpp"

namespace oc::context {
template <typename T>
class OverlayManager;
}

namespace core::context::standalone {

/**
 * Keeps inactive retained overlay trees outside the active LVGL screen.
 *
 * Overlay state and input authority remain owned by OverlayManager. This
 * registry only projects that lifecycle onto LVGL parents, preserving one
 * presentation path for normal, replaced, and stacked overlays.
 */
class OverlayPresentationRegistry {
public:
    explicit OverlayPresentationRegistry(lv_obj_t* activeParent);
    ~OverlayPresentationRegistry();

    OverlayPresentationRegistry(const OverlayPresentationRegistry&) = delete;
    OverlayPresentationRegistry& operator=(const OverlayPresentationRegistry&) = delete;

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] bool registerOverlay(core::ui::OverlayType type, lv_obj_t* root);
    void setPresented(core::ui::OverlayType type, bool presented);

    static void onPresentationChanged(void* context,
                                      core::ui::OverlayType type,
                                      bool presented);

private:
    struct Entry {
        lv_obj_t* root = nullptr;
        lv_obj_t* activeParent = nullptr;
        bool presented = false;
    };

    [[nodiscard]] lv_obj_t* activeParentFor(lv_obj_t* root) const;
    [[nodiscard]] bool rootPresentedElsewhere(
        lv_obj_t* root,
        core::ui::OverlayType except
    ) const;

    static constexpr size_t ENTRY_COUNT =
        static_cast<size_t>(core::ui::OverlayType::COUNT);

    oc::ui::lvgl::RetainedSurfaceParkingLot parking_{};
    lv_obj_t* parking_host_ = nullptr;
    lv_obj_t* default_active_parent_ = nullptr;
    std::array<Entry, ENTRY_COUNT> entries_{};
    bool valid_ = false;
};

[[nodiscard]] bool registerOverlaySurface(
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    OverlayPresentationRegistry& presentations,
    core::ui::OverlayType type,
    lv_obj_t* root,
    oc::type::ButtonID latchButton = 0,
    oc::type::ScopeID authorityScope = 0
);

}  // namespace core::context::standalone
