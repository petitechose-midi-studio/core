function(ms_core_add_project_file_tool)
    if(NOT DEFINED MS_CORE_PROJECT_FILE_CORE_SOURCES)
        message(FATAL_ERROR "ms_core_add_project_file_tool requires cmake/MsCoreSources.cmake")
    endif()

    add_library(ms_core_project_file_open_control_native STATIC
        ${MS_CORE_PROJECT_FILE_OPEN_CONTROL_SOURCES})
    target_include_directories(ms_core_project_file_open_control_native
        PUBLIC
            "${MS_CORE_OC_FRAMEWORK_DIR}/src"
            "${MS_CORE_OC_NOTE_DIR}/src")
    target_compile_features(ms_core_project_file_open_control_native PUBLIC cxx_std_20)

    add_library(ms_core_project_file_native STATIC
        ${MS_CORE_PROJECT_FILE_CORE_SOURCES})
    target_compile_definitions(ms_core_project_file_native PUBLIC OC_LOG)
    target_include_directories(ms_core_project_file_native
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(ms_core_project_file_native
        PUBLIC
            ms_core_project_file_open_control_native)

    add_executable(ms-core-file-tool
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/ms_core_file_tool/main.cpp")
    target_link_libraries(ms-core-file-tool PRIVATE ms_core_project_file_native)
    target_include_directories(ms-core-file-tool
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
endfunction()
