#pragma once

/**
 * @file OverlaySet.hpp
 * @brief Overlay set type definition
 */

#include <memory>
#include <tuple>

#include <lvgl.h>

#include <oc/ui/lvgl/IView.hpp>

namespace core::ui {

/**
 * @brief Type-safe container for overlays
 *
 * Unlike ViewSet, multiple overlays can be visible simultaneously.
 * Each overlay manages its own visibility independently.
 *
 * Usage:
 * @code
 * OverlaySet<ConfirmDialog, Toast, ContextMenu> overlays_;
 *
 * void initialize() {
 *     overlays_.create<ConfirmDialog>(screen);
 *     overlays_.create<Toast>(screen);
 * }
 *
 * void showConfirm() {
 *     overlays_.show<ConfirmDialog>();
 * }
 *
 * void notify(const char* msg) {
 *     overlays_.get<Toast>().setMessage(msg);
 *     overlays_.show<Toast>();
 * }
 * @endcode
 */
template <typename... Overlays>
class OverlaySet {
public:
    OverlaySet() = default;
    ~OverlaySet() = default;

    // Non-copyable, movable
    OverlaySet(const OverlaySet&) = delete;
    OverlaySet& operator=(const OverlaySet&) = delete;
    OverlaySet(OverlaySet&&) = default;
    OverlaySet& operator=(OverlaySet&&) = default;

    /**
     * @brief Create an overlay of type O
     * @return Reference to the created overlay
     */
    template <typename O, typename... Args>
    O& create(lv_obj_t* screen, Args&&... args) {
        auto& ptr = std::get<std::unique_ptr<O>>(overlays_);
        ptr = std::make_unique<O>(screen, std::forward<Args>(args)...);
        // Overlays start hidden
        lv_obj_add_flag(ptr->getElement(), LV_OBJ_FLAG_HIDDEN);
        return *ptr;
    }

    /**
     * @brief Get an overlay by type
     */
    template <typename O>
    O& get() {
        return *std::get<std::unique_ptr<O>>(overlays_);
    }

    /**
     * @brief Get an overlay by type (const)
     */
    template <typename O>
    const O& get() const {
        return *std::get<std::unique_ptr<O>>(overlays_);
    }

    /**
     * @brief Check if an overlay exists
     */
    template <typename O>
    bool has() const {
        return std::get<std::unique_ptr<O>>(overlays_) != nullptr;
    }

    /**
     * @brief Show an overlay (make visible + activate)
     */
    template <typename O>
    void show() {
        auto& ptr = std::get<std::unique_ptr<O>>(overlays_);
        if (!ptr) return;
        lv_obj_clear_flag(ptr->getElement(), LV_OBJ_FLAG_HIDDEN);
        ptr->onActivate();
    }

    /**
     * @brief Hide an overlay (deactivate + make invisible)
     */
    template <typename O>
    void hide() {
        auto& ptr = std::get<std::unique_ptr<O>>(overlays_);
        if (!ptr) return;
        ptr->onDeactivate();
        lv_obj_add_flag(ptr->getElement(), LV_OBJ_FLAG_HIDDEN);
    }

    /**
     * @brief Check if an overlay is visible
     */
    template <typename O>
    bool isVisible() const {
        auto& ptr = std::get<std::unique_ptr<O>>(overlays_);
        if (!ptr) return false;
        return !lv_obj_has_flag(ptr->getElement(), LV_OBJ_FLAG_HIDDEN);
    }

    /**
     * @brief Hide all overlays
     */
    void hideAll() {
        hideAllImpl(std::index_sequence_for<Overlays...>{});
    }

private:
    std::tuple<std::unique_ptr<Overlays>...> overlays_;

    template <std::size_t... Is>
    void hideAllImpl(std::index_sequence<Is...>) {
        (hideByIndex<Is>(), ...);
    }

    template <std::size_t I>
    void hideByIndex() {
        auto& ptr = std::get<I>(overlays_);
        if (ptr) {
            ptr->onDeactivate();
            lv_obj_add_flag(ptr->getElement(), LV_OBJ_FLAG_HIDDEN);
        }
    }
};

}  // namespace core::ui
