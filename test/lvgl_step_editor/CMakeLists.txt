cmake_minimum_required(VERSION 3.29)
project(step_editor_test LANGUAGES C CXX)
set(LVGL_DIR "" CACHE PATH "Application LVGL checkout")
set(OC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../open-control" CACHE PATH "Open Control repositories")
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_DIR}" lvgl)
add_executable(step_editor test_main.cpp
    ../../src/context/standalone/OverlayPresentationRegistry.cpp
    "${OC_ROOT}/ui-lvgl/src/oc/ui/lvgl/RetainedSurfaceParkingLot.cpp"
    ../../src/ui/sequencer/SequencerStepEditOverlay.cpp
    ../../src/ui/sequencer/SequencerChordVoiceRail.cpp
    ../../../ui/src/ms/ui/widget/TextOverflow.cpp
    ../../src/ui/common/CompactMetricRow.cpp)
add_executable(track_navigation_strip track_navigation_strip.cpp
    ../../src/ui/common/TrackNavigationStrip.cpp
    ../../src/ui/common/AddSlotIcon.cpp)
add_executable(step_grid_widgets step_grid_widgets.cpp ../../src/ui/sequencer/StepGridWidgets.cpp)
add_executable(main_view_frame main_view_frame.cpp ../../src/ui/view/MainViewFrame.cpp
    ../../../ui/src/ms/ui/component/LayoutView.cpp
    "${OC_ROOT}/ui-lvgl/src/oc/ui/lvgl/RetainedSurfaceParkingLot.cpp")
add_executable(context_action_strip context_action_strip.cpp ../../src/ui/strip/ContextActionStrip.cpp
    "${OC_ROOT}/ui-lvgl/src/oc/ui/lvgl/PausableTimer.cpp"
    "${OC_ROOT}/framework/src/oc/time/Time.cpp")
add_executable(overlay_occlusion overlay_occlusion.cpp ../../../ui/src/ms/ui/ViewContainer.cpp)
add_executable(pattern_preset_preview pattern_preset_preview.cpp)
add_executable(cc_lane_grid cc_lane_grid.cpp ../../src/state/sequencer/SequencerCcLaneDomain.cpp
    ../../../ui/src/ms/ui/widget/TextOverflow.cpp
    "${OC_ROOT}/ui-lvgl/src/oc/ui/lvgl/StaticSurfaceInvalidation.cpp")
add_executable(cached_header_text cached_header_text.cpp
    ../../src/ui/common/TrackHeaderRow.cpp ../../src/ui/common/AddSlotIcon.cpp
    ../../src/ui/common/CompactMetricRow.cpp ../../src/ui/sequencer/SequencerCcLaneGrid.cpp
    ../../src/state/sequencer/SequencerCcLaneDomain.cpp
    ../../../ui/src/ms/ui/widget/TextOverflow.cpp
    "${OC_ROOT}/ui-lvgl/src/oc/ui/lvgl/StaticSurfaceInvalidation.cpp")
add_executable(property_selection_overlay property_selection_overlay.cpp
    ../../src/ui/sequencer/StepPropertySelectionOverlay.cpp
    ../../src/ui/sequencer/StepPropertyVisuals.cpp)
add_executable(macro_knob macro_knob.cpp ../../src/ui/widget/MacroKnobWidget.cpp
    "${OC_ROOT}/ui-lvgl-components/src/widget/Label.cpp"
    "${OC_ROOT}/ui-lvgl/src/oc/ui/lvgl/StaticSurfaceInvalidation.cpp")
add_executable(font_registry font_registry.cpp ../../../ui/src/ms/ui/font/CoreFonts.cpp)
foreach(target step_editor track_navigation_strip step_grid_widgets main_view_frame context_action_strip overlay_occlusion pattern_preset_preview cc_lane_grid cached_header_text property_selection_overlay macro_knob font_registry)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE ../../src ../../../ui/src ../../../device-support/src
        "${OC_ROOT}/ui-lvgl/src" "${OC_ROOT}/framework/src"
        "${OC_ROOT}/note/src" "${OC_ROOT}/ui-lvgl-components/include")
    target_link_libraries(${target} PRIVATE lvgl::lvgl)
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
endforeach()
enable_testing()
add_test(NAME step_editor COMMAND step_editor)
add_test(NAME track_navigation_strip COMMAND track_navigation_strip)
add_test(NAME step_grid_widgets COMMAND step_grid_widgets)
add_test(NAME main_view_frame COMMAND main_view_frame)
add_test(NAME context_action_strip COMMAND context_action_strip)
add_test(NAME overlay_occlusion COMMAND overlay_occlusion)
add_test(NAME pattern_preset_preview COMMAND pattern_preset_preview)
add_test(NAME cc_lane_grid COMMAND cc_lane_grid)
add_test(NAME cached_header_text COMMAND cached_header_text)
add_test(NAME property_selection_overlay COMMAND property_selection_overlay)
add_test(NAME macro_knob COMMAND macro_knob)
add_test(NAME font_registry COMMAND font_registry)
