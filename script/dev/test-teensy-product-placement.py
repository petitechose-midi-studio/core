#!/usr/bin/env python3

from teensy_product_placement import product_placement_violations


def main() -> int:
    valid = """
1610613000 220 W oc::state::Signal<bool, 4u>::subscribe(std::function<void (bool const&)>)
1610613300 32 t std::_Function_handler<void ()>::_M_manager(std::_Any_data&, std::_Any_data const&, std::_Manager_operation)
280504 8 t __lv_binfont_create_from_buffer_veneer
281312 8 t __lv_draw_sw_box_shadow_veneer
1610613500 1472 T lv_binfont_create
1610615000 2748 T lv_draw_sw_box_shadow
1610618000 416 T FatFormatter::makeFat32()
34348 324 T core::handler::MacroValueHandler::handleValueChange(unsigned char, float)
24016 648 T core::handler::MacroAutomationPlaybackService::update(unsigned long)
54180 596 T core::sequencer::RealtimeMidiQueue::pushBatchImpl_(void)
54856 1888 T core::sequencer::SequencerCcLaneRuntime::buildMusicalTickFrame(void)
97048 752 T core::ui::MacroView::processRenderFlags(unsigned long)
86548 702 T core::ui::StepGrid::renderTile(void)
539099136 153600 B Buffer::lvgl
"""
    assert product_placement_violations(valid) == ()

    invalid = valid.replace(
        "1610613000 220 W oc::state::Signal<bool, 4u>::subscribe",
        "24000 220 W oc::state::Signal<bool, 4u>::subscribe",
    ).replace(
        "34348 324 T core::handler::MacroValueHandler::handleValueChange",
        "1610620000 324 T core::handler::MacroValueHandler::handleValueChange",
    ).replace(
        "539099136 153600 B Buffer::lvgl",
        "539099136 230400 B Buffer::lvgl",
    )
    violations = product_placement_violations(invalid)
    assert "Signal subscription setup must execute from Flash" in violations
    assert any("MacroValueHandler" in item for item in violations)
    assert "LVGL draw buffer must be one 320x240 RGB565 frame in RAM2" in violations

    print("Teensy product placement parser: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
