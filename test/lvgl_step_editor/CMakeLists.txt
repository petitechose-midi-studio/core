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
    ../../src/ui/common/CompactMetricRow.cpp)
add_executable(track_navigation_strip track_navigation_strip.cpp
    ../../src/ui/common/TrackNavigationStrip.cpp
    ../../src/ui/common/AddSlotIcon.cpp)
add_executable(step_grid_widgets step_grid_widgets.cpp ../../src/ui/sequencer/StepGridWidgets.cpp)
foreach(target step_editor track_navigation_strip step_grid_widgets)
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
