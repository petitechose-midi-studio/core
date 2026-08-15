#include "context/standalone/OverlayPresentationRegistry.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/ui/lvgl/Scope.hpp>

namespace core::context::standalone {

FLASHMEM OverlayPresentationRegistry::OverlayPresentationRegistry(lv_obj_t* activeParent)
    : default_active_parent_(activeParent) {
    valid_ = activeParent != nullptr && parking_.initialize();
    if (valid_) parking_host_ = parking_.createHost();
    valid_ = valid_ && parking_host_ != nullptr;
}

FLASHMEM OverlayPresentationRegistry::~OverlayPresentationRegistry() = default;

FLASHMEM lv_obj_t* OverlayPresentationRegistry::activeParentFor(
    lv_obj_t* root
) const {
    for (const auto& entry : entries_) {
        if (entry.root == root && entry.activeParent &&
            entry.activeParent != parking_host_) {
            return entry.activeParent;
        }
    }
    return nullptr;
}

FLASHMEM bool OverlayPresentationRegistry::rootPresentedElsewhere(
    lv_obj_t* root,
    core::ui::OverlayType except
) const {
    for (size_t index = 0; index < entries_.size(); ++index) {
        if (index == static_cast<size_t>(except)) continue;
        const auto& entry = entries_[index];
        if (entry.root == root && entry.presented) return true;
    }
    return false;
}

FLASHMEM bool OverlayPresentationRegistry::registerOverlay(core::ui::OverlayType type,
                                                           lv_obj_t* root) {
    const size_t index = static_cast<size_t>(type);
    if (!valid_ || type == core::ui::OverlayType::NONE ||
        index >= entries_.size() || !root) {
        return false;
    }

    Entry& entry = entries_[index];
    if (entry.root != nullptr && entry.root != root) return false;
    entry.root = root;
    entry.activeParent = activeParentFor(root);
    if (!entry.activeParent) entry.activeParent = lv_obj_get_parent(root);
    if (entry.activeParent == parking_host_) {
        entry.activeParent = default_active_parent_;
    }
    if (!entry.activeParent) entry.activeParent = default_active_parent_;
    if (!entry.activeParent) {
        entry = {};
        return false;
    }

    entry.presented = false;
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    oc::ui::lvgl::RetainedSurfaceParkingLot::park(entry.root, parking_host_);
    return lv_obj_get_parent(root) == parking_host_;
}

FLASHMEM void OverlayPresentationRegistry::setPresented(core::ui::OverlayType type,
                                                        bool presented) {
    const size_t index = static_cast<size_t>(type);
    if (type == core::ui::OverlayType::NONE || index >= entries_.size()) return;

    Entry& entry = entries_[index];
    if (!entry.root) return;

    entry.presented = presented;

    if (presented) {
        oc::ui::lvgl::RetainedSurfaceParkingLot::attach(entry.root, entry.activeParent);
        lv_obj_move_foreground(entry.root);
        lv_obj_clear_flag(entry.root, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (!rootPresentedElsewhere(entry.root, type)) {
        lv_obj_add_flag(entry.root, LV_OBJ_FLAG_HIDDEN);
        oc::ui::lvgl::RetainedSurfaceParkingLot::park(entry.root, parking_host_);
    }
}

FLASHMEM void OverlayPresentationRegistry::onPresentationChanged(
    void* context,
    core::ui::OverlayType type,
    bool presented
) {
    auto* registry = static_cast<OverlayPresentationRegistry*>(context);
    if (registry) registry->setPresented(type, presented);
}

FLASHMEM bool registerOverlaySurface(
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    OverlayPresentationRegistry& presentations,
    core::ui::OverlayType type,
    lv_obj_t* root,
    oc::type::ButtonID latchButton,
    oc::type::ScopeID authorityScope
) {
    if (!root) return false;
    const auto scope = authorityScope != 0
        ? authorityScope
        : oc::ui::lvgl::scopeID(root);
    if (scope == 0 || !presentations.registerOverlay(type, root)) return false;
    overlays.registerCleanup(type, scope, latchButton);
    return true;
}

}  // namespace core::context::standalone
