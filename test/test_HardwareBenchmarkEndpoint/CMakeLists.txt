cmake_minimum_required(VERSION 3.29)
project(hardware_benchmark_endpoint_contract LANGUAGES CXX)
include(CTest)
get_filename_component(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(OPEN_CONTROL_DIR "${CORE_DIR}/../../open-control" ABSOLUTE)

# NotificationQueue's ABI depends on OC_ENABLE_STATS. Build the whole native
# dependency graph consistently, without changing the normal Core test suite.
add_compile_definitions(OC_ENABLE_STATS=1)
set(MS_CORE_BUILD_TESTS OFF CACHE BOOL "Build normal Core tests" FORCE)
add_subdirectory("${CORE_DIR}" "${CMAKE_BINARY_DIR}/core")
add_executable(test_HardwareBenchmarkEndpoint
    endpoint_test.cpp
    "${CORE_DIR}/src/validation/benchmark/HardwareBenchmarkEndpoint.cpp"
    "${CORE_DIR}/src/validation/benchmark/HardwareBenchmarkRun.cpp"
    "${CORE_DIR}/src/validation/benchmark/HardwareBenchmarkFixture.cpp"
    "${CORE_DIR}/src/validation/benchmark/HardwareBenchmarkMetrics.cpp"
    "${OPEN_CONTROL_DIR}/ui-lvgl/src/oc/ui/lvgl/LvglFrameProfiler.cpp")
target_compile_features(test_HardwareBenchmarkEndpoint PRIVATE cxx_std_17)
target_compile_definitions(test_HardwareBenchmarkEndpoint PRIVATE
    MS_HARDWARE_BENCHMARK=1 OC_ENABLE_LVGL_BENCHMARK=1
    MS_HARDWARE_BENCHMARK_BUILD_ID="native-endpoint-contract")
target_include_directories(test_HardwareBenchmarkEndpoint BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CORE_DIR}/test"
    "${OPEN_CONTROL_DIR}/ui-lvgl/src")
target_link_libraries(test_HardwareBenchmarkEndpoint PRIVATE midi_studio_core_native)
if(MSVC)
    target_compile_options(test_HardwareBenchmarkEndpoint PRIVATE /UNDEBUG /W4)
else()
    target_compile_options(test_HardwareBenchmarkEndpoint PRIVATE -UNDEBUG -Wall -Wextra)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME test_HardwareBenchmarkEndpoint
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_json.py"
            "$<TARGET_FILE:test_HardwareBenchmarkEndpoint>")
