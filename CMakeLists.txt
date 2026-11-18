cmake_minimum_required(VERSION 3.14)
project(c_gui_template VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(APP_NAME "C GUI Template" CACHE STRING "App name used for the window and settings folder")
set(APP_BUNDLE_ID "com.example.c-gui-template" CACHE STRING "macOS bundle identifier (CFBundleIdentifier)")

include(FetchContent)
FetchContent_Declare(cui
    GIT_REPOSITORY https://github.com/mzgs/Better-C-UI.git
    GIT_TAG a3fe1409731b6aafc7b23c3eed6d2d42dfc40ec1)
FetchContent_Declare(yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG 6447536015f3d600f3d65323b10976103b337ca7)
FetchContent_MakeAvailable(cui yyjson)

add_compile_definitions("APP_NAME=\"${APP_NAME}\"" "APP_VERSION=\"${PROJECT_VERSION}\"")
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(app_settings STATIC settings.c)
target_link_libraries(app_settings PRIVATE cui::cui yyjson)
if(WIN32)
    target_link_libraries(app_settings PRIVATE uuid shell32 ole32)
endif()

add_executable(empty_gui MACOSX_BUNDLE main.c)
target_link_libraries(empty_gui PRIVATE app_settings cui::cui)
cui_app_target(empty_gui)
if(APPLE)
    enable_language(OBJC)
    target_sources(app_settings PRIVATE macos_settings.m)
    target_link_libraries(app_settings PRIVATE "-framework Foundation")
    target_sources(empty_gui PRIVATE macos_menu.m)
    set_source_files_properties(macos_menu.m macos_settings.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_target_properties(empty_gui PROPERTIES
        OUTPUT_NAME "${APP_NAME}"
        MACOSX_BUNDLE_BUNDLE_NAME "${APP_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_ID}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    add_custom_target(run COMMAND $<TARGET_FILE:empty_gui> DEPENDS empty_gui
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" USES_TERMINAL VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME window_smoke COMMAND ${CMAKE_COMMAND} -E env
        "CUI_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/window-smoke.png"
        CUI_SCREENSHOT_FRAMES=1 $<TARGET_FILE:empty_gui>)
    set_tests_properties(window_smoke PROPERTIES
        TIMEOUT 15 PASS_REGULAR_EXPRESSION "cui: saved")
    add_executable(settings_test tests/settings_test.c)
    target_link_libraries(settings_test PRIVATE app_settings cui::cui)
    if(APPLE)
        target_sources(settings_test PRIVATE macos_menu.m)
    endif()
    add_test(NAME settings COMMAND settings_test "${CMAKE_CURRENT_BINARY_DIR}/settings-test")
    set_tests_properties(settings PROPERTIES TIMEOUT 15)
    if(APPLE)
        add_executable(macos_menu_test tests/macos_menu_test.m macos_menu.m)
        set_source_files_properties(tests/macos_menu_test.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(macos_menu_test PRIVATE app_settings cui::cui)
        add_test(NAME macos_menu COMMAND macos_menu_test)
        set_tests_properties(macos_menu PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
        add_test(NAME run_script_smoke COMMAND ${CMAKE_COMMAND} -E env
            "BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "BUILD_CONFIG=$<CONFIG>"
            "CUI_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/run-smoke.png"
            CUI_SCREENSHOT_FRAMES=1 ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
        set_tests_properties(run_script_smoke PROPERTIES
            TIMEOUT 120 PASS_REGULAR_EXPRESSION "cui: saved" RUN_SERIAL TRUE)
    endif()
endif()
