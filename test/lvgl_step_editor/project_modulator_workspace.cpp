#include <array>
#include <cassert>
#include <cstdio>
#include <tuple>
#include <lvgl.h>
#include <ms/ui/widget/CurvePreviewWidget.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include "ui/font/StandaloneFonts.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

CoreFonts fonts;
StandaloneFonts standalone_fonts;

// Observe requests made by the real widget after render() has returned.
// Keep its actual LVGL geometry, size callbacks and marker timer in the test.
namespace ms::ui {
class ObservedCurvePreviewWidget : public CurvePreviewWidget {
public:
    using CurvePreviewWidget::CurvePreviewWidget;
    static inline ObservedCurvePreviewWidget* current = nullptr;
    unsigned markerCalls = 0;
    std::tuple<bool, uint16_t, uint16_t> lastMarker{};
    void render(const CurvePreviewWidgetProps& props) {
        current = this;
        original = props;
        auto observed = props;
        observed.markerProvider = [](void* context, CurvePreviewMarker& out) {
            auto& self = *static_cast<ObservedCurvePreviewWidget*>(context);
            ++self.markerCalls;
            const bool ok = self.original.markerProvider &&
                self.original.markerProvider(self.original.markerContext, out);
            self.lastMarker = {out.visible, out.positionQ16, out.valueQ16};
            return ok;
        };
        observed.markerContext = this;
        CurvePreviewWidget::render(observed);
    }
private:
    CurvePreviewWidgetProps original{};
};
}

#define CurvePreviewWidget ObservedCurvePreviewWidget
#include "../../src/ui/project/ProjectModulatorWorkspace.cpp"
#undef CurvePreviewWidget

int main() {
    lv_init();
    auto* font = const_cast<lv_font_t*>(LV_FONT_DEFAULT);
    fonts = {font, font, font, font, font, font, font, font, font, font};
    standalone_fonts = {font, font, font};
    auto* display = lv_display_create(320, 240);
    std::array<uint16_t, 320 * 240> pixels{};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels.data(), nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    lv_obj_set_flex_flow(lv_screen_active(), LV_FLEX_FLOW_COLUMN);
    {
        using namespace core::state::modulation;
        ProjectControlState control;
        ModulatorLfoDraft draft{};
        draft.name = "Borrow";
        draft.parameters.shape = ModulatorLfoShape::SQUARE;
        const auto created = createLfoModulator(control.authored().modulation, draft);
        assert(created.changed());
        assert(compileProjectControlRuntimePlan(control.authored(), {}, control.plan).compiled());
        resetProjectControlRuntimeState(control.runtime, {});
        assert(synchronizeProjectControlRuntimeState(control.runtime, control.plan, {}) == ProjectControlRuntimeStatus::OK);
        ProjectControlRuntimeFrame frame{};
        assert(evaluateProjectControlRuntimeFrame(control.plan, control.authored().curves, {}, {},
            nullptr, 0, control.runtime, frame).evaluated());
        core::ui::project::ProjectModulatorWorkspace workspace(lv_screen_active());
        assert(workspace.valid());
        const auto render = [&] {
            workspace.render({.visible = true, .control = &control,
                .source = findProjectModulator(control.authored().modulation, created.sourceId)});
            lv_obj_update_layout(workspace.getElement());
            lv_refr_now(display);
        };
        render();
        auto* widget = ms::ui::ObservedCurvePreviewWidget::current;
        const auto resize = [&](int width) {
            const auto callsBefore = widget->markerCalls;
            lv_obj_set_width(widget->getElement(), width);
            lv_obj_update_layout(widget->getElement());
            lv_tick_inc(20);
            lv_timer_handler();
            lv_refr_now(display);
            assert(widget->markerCalls > callsBefore);
            assert(std::get<0>(widget->lastMarker));
            return widget->lastMarker;
        };
        const auto expected = resize(270);
        resize(280);
        for (unsigned cycle = 0; cycle < 20; ++cycle) {
            auto candidate = core::app::makeExtmemUniqueCopy(control.authored());
            assert(control.tryPublishAuthored(candidate));
            // The old live source is still addressable here, but no longer belongs
            // to the published domain. Poison it to detect a stale borrow reliably.
            auto* retired = findProjectModulator(candidate->modulation, created.sourceId);
            assert(retired);
            retired->parameters.lfo.phaseQ15 = 16384;
            assert(resize(270) == expected);
            candidate.reset();
            resize(280);
            assert(resize(270) == expected);
            render();
            resize(280);
        }
        assert(widget->markerCalls > 20);
        workspace.render({.visible = false});
        lv_tick_inc(20);
        lv_timer_handler();
        render();
        resize(280);
        assert(resize(270) == expected);
        std::printf("PASS: 20 publications, retired-source poisoning, release, deferred resize/timers and reopen; %u marker calls\n", widget->markerCalls);
    }
    lv_deinit();
}
