cmake_minimum_required(VERSION 3.14)
project(c_gui_template LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(cui
    GIT_REPOSITORY https://github.com/mzgs/Better-C-UI.git
    GIT_TAG a3fe1409731b6aafc7b23c3eed6d2d42dfc40ec1)
FetchContent_MakeAvailable(cui)

add_executable(empty_gui MACOSX_BUNDLE main.c)
target_link_libraries(empty_gui PRIVATE cui::cui)
cui_app_target(empty_gui)
if(APPLE)
    set_target_properties(empty_gui PROPERTIES
        OUTPUT_NAME "C GUI Template"
        MACOSX_BUNDLE_BUNDLE_NAME "C GUI Template")
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME window_smoke COMMAND ${CMAKE_COMMAND} -E env
        "CUI_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/window-smoke.png"
        CUI_SCREENSHOT_FRAMES=1 $<TARGET_FILE:empty_gui>)
    set_tests_properties(window_smoke PROPERTIES
        TIMEOUT 15 PASS_REGULAR_EXPRESSION "cui: saved")
    if(APPLE)
        add_test(NAME run_script_smoke COMMAND ${CMAKE_COMMAND} -E env
            "CUI_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/run-smoke.png"
            CUI_SCREENSHOT_FRAMES=1 ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
        set_tests_properties(run_script_smoke PROPERTIES
            TIMEOUT 120 PASS_REGULAR_EXPRESSION "cui: saved" RUN_SERIAL TRUE)
    endif()
endif()
