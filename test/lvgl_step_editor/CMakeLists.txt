cmake_minimum_required(VERSION 3.29)
project(step_editor_test LANGUAGES C CXX)
set(LVGL_DIR "" CACHE PATH "Application LVGL checkout")
set(OC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../open-control" CACHE PATH "Open Control repositories")
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_DIR}" lvgl)
add_executable(step_editor test_main.cpp
    ../../src/ui/sequencer/SequencerStepEditOverlay.cpp
    ../../src/ui/sequencer/SequencerChordVoiceRail.cpp
    ../../src/ui/common/CompactMetricRow.cpp)
target_compile_features(step_editor PRIVATE cxx_std_20)
target_include_directories(step_editor PRIVATE ../../src ../../../ui/src ../../../device-support/src
    "${OC_ROOT}/ui-lvgl/src" "${OC_ROOT}/framework/src"
    "${OC_ROOT}/note/src" "${OC_ROOT}/ui-lvgl-components/include")
target_link_libraries(step_editor PRIVATE lvgl::lvgl)
if(MSVC)
    target_compile_options(step_editor PRIVATE /UNDEBUG)
else()
    target_compile_options(step_editor PRIVATE -UNDEBUG)
endif()
enable_testing()
add_test(NAME step_editor COMMAND step_editor)
