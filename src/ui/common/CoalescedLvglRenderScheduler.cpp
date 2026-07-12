#include "ui/common/CoalescedLvglRenderScheduler.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

namespace core::ui {

FLASHMEM CoalescedLvglRenderScheduler::CoalescedLvglRenderScheduler(
    const char* debugLabel,
    DrainCallback callback,
    void* context,
    uint32_t periodMs,
    DrainReadyCallback drainReady
)
#if OC_ENABLE_STATS
    : diagnostic_label_(debugLabel ? debugLabel : "ui.render-scheduler")
    , callback_(callback)
#else
    : callback_(callback)
#endif
    , drain_ready_(drainReady)
    , context_(context)
    , timer_(periodMs, &CoalescedLvglRenderScheduler::onTimer, this) {
#if !OC_ENABLE_STATS
    (void)debugLabel;
#endif
}

FLASHMEM CoalescedLvglRenderScheduler::~CoalescedLvglRenderScheduler() = default;

void CoalescedLvglRenderScheduler::request(uint32_t flags, bool ready) {
    if (flags == 0 || !callback_) return;

    pending_flags_ |= flags;
    resumePending(ready);
}

void CoalescedLvglRenderScheduler::resumePending(bool ready) {
    if (pending_flags_ == 0 || !callback_ || !canDrain()) {
        timer_.pause();
        return;
    }
    timer_.resume(ready);
}

void CoalescedLvglRenderScheduler::pause() {
    timer_.pause();
}

void CoalescedLvglRenderScheduler::cancel() {
    pending_flags_ = 0;
    timer_.pause();
}

void CoalescedLvglRenderScheduler::onTimer(lv_timer_t* timer) {
    auto* self = static_cast<CoalescedLvglRenderScheduler*>(lv_timer_get_user_data(timer));
    if (self) self->drain();
}

bool CoalescedLvglRenderScheduler::canDrain() const {
    return drain_ready_ == nullptr || drain_ready_(context_);
}

void CoalescedLvglRenderScheduler::drain() {
    timer_.pause();

    if (!canDrain()) return;

    const uint32_t flags = pending_flags_;
    pending_flags_ = 0;
    if (flags == 0 || !callback_) return;

    OC_PERF_SCOPE(perfDrain, diagnostic_label_);
    OC_PERF_UNITS(perfDrain, flags, 0U);
    callback_(context_, flags);

    resumePending();
}

}  // namespace core::ui
