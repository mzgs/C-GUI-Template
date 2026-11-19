cmake_minimum_required(VERSION 3.14)
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET AND "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment version" FORCE)
endif()
project(c_gui_template VERSION 0.1.3 LANGUAGES C)

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

add_library(app_common INTERFACE)
target_compile_definitions(app_common INTERFACE
    "APP_NAME=\"${APP_NAME}\""
    "APP_VERSION=\"${PROJECT_VERSION}\"")
if(MSVC)
    target_compile_options(app_common INTERFACE /utf-8 /W4)
else()
    target_compile_options(app_common INTERFACE -Wall -Wextra)
endif()

if(APPLE)
    enable_language(OBJC)
    add_library(app_platform STATIC src/platform/platform_macos.m)
    set_source_files_properties(src/platform/platform_macos.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(app_platform PRIVATE "-framework Cocoa")
elseif(WIN32)
    add_library(app_platform STATIC src/platform/platform_win32.c)
    target_link_libraries(app_platform PRIVATE advapi32 user32)
else()
    add_library(app_platform STATIC src/platform/platform_x11.c)
    target_link_libraries(app_platform PRIVATE cui::cui)
endif()
target_include_directories(app_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/platform")
target_link_libraries(app_platform PRIVATE app_common)

add_library(app_settings STATIC src/settings/settings.c)
target_include_directories(app_settings PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/settings")
target_link_libraries(app_settings PUBLIC app_platform cui::cui PRIVATE app_common yyjson)
if(WIN32)
    target_link_libraries(app_settings PRIVATE uuid shell32 ole32)
endif()

add_library(app_chrome STATIC
    src/chrome/app_chrome.c
    src/chrome/settings_dialog.c
    src/chrome/command_palette.c)
target_include_directories(app_chrome PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/chrome")
target_link_libraries(app_chrome PUBLIC app_settings PRIVATE app_common)
if(APPLE)
    target_sources(app_chrome PRIVATE src/platform/app_menu_macos.m)
    set_source_files_properties(src/platform/app_menu_macos.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(app_chrome PRIVATE "-framework Cocoa")
endif()

add_executable(empty_gui MACOSX_BUNDLE main.c)
target_link_libraries(empty_gui PRIVATE app_chrome app_common cui::cui)
cui_app_target(empty_gui)
install(TARGETS empty_gui
    BUNDLE DESTINATION . COMPONENT app
    RUNTIME DESTINATION . COMPONENT app)
if(APPLE)
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

    if(APPLE)
        add_test(NAME run_script_smoke COMMAND ${CMAKE_COMMAND} -E env
            "BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "BUILD_CONFIG=$<CONFIG>"
            "CUI_SCREENSHOT=${CMAKE_CURRENT_BINARY_DIR}/run-smoke.png"
            CUI_SCREENSHOT_FRAMES=1 ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
        set_tests_properties(run_script_smoke PROPERTIES
            TIMEOUT 120 PASS_REGULAR_EXPRESSION "cui: saved" RUN_SERIAL TRUE)
    endif()
endif()
