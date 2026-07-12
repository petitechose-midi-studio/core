#pragma once

#include <cstdint>

#include <oc/Config.hpp>
#include <oc/ui/lvgl/PausableTimer.hpp>

namespace core::ui {

constexpr const char* renderSchedulerDebugLabel(const char* label) {
#if OC_ENABLE_STATS
    return label;
#else
    (void)label;
    return nullptr;
#endif
}

/**
 * Frame-paces retained LVGL projections and coalesces related state changes.
 *
 * Signal callbacks only request render flags. The projection runs from LVGL's
 * timer phase at most once per period, keeping widget mutation out of the
 * reactive state flush and collapsing one logical transaction into one render.
 * Flags are consumed before the drain callback, so requests made reentrantly by
 * LVGL callbacks are retained for the next pass. An optional readiness gate
 * keeps pending work paused while a retained view is hidden or covered.
 */
class CoalescedLvglRenderScheduler {
public:
    using DrainCallback = void (*)(void* context, uint32_t flags);
    using DrainReadyCallback = bool (*)(void* context);

    static constexpr uint32_t DEFAULT_PERIOD_MS = 16;

    CoalescedLvglRenderScheduler(const char* debugLabel,
                                 DrainCallback callback,
                                 void* context,
                                 uint32_t periodMs = DEFAULT_PERIOD_MS,
                                 DrainReadyCallback drainReady = nullptr);
    ~CoalescedLvglRenderScheduler();

    CoalescedLvglRenderScheduler(const CoalescedLvglRenderScheduler&) = delete;
    CoalescedLvglRenderScheduler& operator=(const CoalescedLvglRenderScheduler&) = delete;

    [[nodiscard]] bool valid() const { return callback_ != nullptr && timer_.valid(); }
    [[nodiscard]] bool pending() const { return pending_flags_ != 0; }
    void request(uint32_t flags, bool ready = false);
    void resumePending(bool ready = false);
    void pause();
    void cancel();

private:
    static void onTimer(lv_timer_t* timer);
    bool canDrain() const;
    void drain();

#if OC_ENABLE_STATS
    const char* diagnostic_label_ = "ui.render-scheduler";
#endif
    DrainCallback callback_ = nullptr;
    DrainReadyCallback drain_ready_ = nullptr;
    void* context_ = nullptr;
    uint32_t pending_flags_ = 0;
    oc::ui::lvgl::PausableTimer timer_;
};

}  // namespace core::ui
