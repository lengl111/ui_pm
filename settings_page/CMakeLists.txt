cmake_minimum_required(VERSION 3.16)

project(settings_page LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SETTINGS_PAGE_LVGL_TARGET "lvgl" CACHE STRING
    "Existing LVGL target used by settings_page")
option(SETTINGS_PAGE_BUILD_DEMO "Build the SDL settings demo" OFF)
set(SETTINGS_PAGE_LVGL_SOURCE "" CACHE PATH
    "LVGL 9 source checkout used by the standalone demo")
set(SETTINGS_PAGE_SDL2_DIR "" CACHE PATH
    "Directory containing SDL2Config.cmake for the standalone demo")

if(SETTINGS_PAGE_BUILD_DEMO AND NOT TARGET ${SETTINGS_PAGE_LVGL_TARGET})
    if(NOT EXISTS "${SETTINGS_PAGE_LVGL_SOURCE}/lv_version.h")
        message(FATAL_ERROR
            "Set SETTINGS_PAGE_LVGL_SOURCE to an LVGL 9 source checkout")
    endif()
    if(NOT EXISTS "${SETTINGS_PAGE_SDL2_DIR}/sdl2-config.cmake")
        message(FATAL_ERROR
            "Set SETTINGS_PAGE_SDL2_DIR to the SDL2 CMake package directory")
    endif()

    set(SDL2_DIR "${SETTINGS_PAGE_SDL2_DIR}")
    find_package(SDL2 REQUIRED CONFIG COMPONENTS SDL2)
    set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/demo/lv_conf.h"
        CACHE FILEPATH "LVGL configuration" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        "${SETTINGS_PAGE_LVGL_SOURCE}"
        "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
    target_include_directories(${SETTINGS_PAGE_LVGL_TARGET}
        PUBLIC "${SETTINGS_PAGE_SDL2_DIR}/../../../include")
    target_link_libraries(${SETTINGS_PAGE_LVGL_TARGET} PUBLIC SDL2::SDL2)
    set_property(TARGET ${SETTINGS_PAGE_LVGL_TARGET} PROPERTY C_EXTENSIONS ON)
endif()

if(NOT TARGET ${SETTINGS_PAGE_LVGL_TARGET})
    message(FATAL_ERROR
        "settings_page expects an existing LVGL target. "
        "Add LVGL first or set SETTINGS_PAGE_LVGL_TARGET.")
endif()

add_library(settings_page STATIC
    src/settings_page.c)
add_library(settings_page::settings_page ALIAS settings_page)

target_include_directories(settings_page
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(settings_page PUBLIC ${SETTINGS_PAGE_LVGL_TARGET})

if(MSVC)
    target_compile_options(settings_page PRIVATE /W4)
else()
    target_compile_options(settings_page PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SETTINGS_PAGE_BUILD_DEMO)
    set(SETTINGS_PAGE_ASSET_STAGE
        "${CMAKE_CURRENT_BINARY_DIR}/settings_assets")
    file(MAKE_DIRECTORY
        "${SETTINGS_PAGE_ASSET_STAGE}/usr/gui/image")
    file(COPY
        "${CMAKE_CURRENT_SOURCE_DIR}/demo/assets/icon"
        DESTINATION
        "${SETTINGS_PAGE_ASSET_STAGE}/usr/gui/image")
    target_compile_definitions(${SETTINGS_PAGE_LVGL_TARGET} PUBLIC
        LV_USE_FS_STDIO=1
        "LV_FS_STDIO_LETTER='A'"
        "LV_FS_STDIO_PATH=\"${SETTINGS_PAGE_ASSET_STAGE}\""
        LV_USE_LODEPNG=1)
    add_executable(settings_page_demo
        demo/main.c
        demo/settings_demo_cn_20.c
        demo/settings_demo_cn_28.c
        demo/settings_demo_cn_24.c
        demo/SourceHanSansCN_Medium_24px_4bpp.c
        demo/SourceHanSansCN_Regular_16px_4bpp.c
        demo/settings_demo_cn_32.c
        demo/settings_demo_unigui_28.c
        demo/SourceHanSansCN_Regular_32px_4bpp.c
        demo/ui_font_MED36.c)
    target_link_libraries(settings_page_demo
        PRIVATE settings_page::settings_page SDL2::SDL2)
    if(MINGW)
        target_compile_options(settings_page_demo
            PRIVATE -Wall -Wextra -Werror -Wpedantic)
    endif()

    add_custom_command(TARGET settings_page_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SETTINGS_PAGE_SDL2_DIR}/../../../bin/SDL2.dll"
            "$<TARGET_FILE_DIR:settings_page_demo>/SDL2.dll")

    include(CTest)
    if(BUILD_TESTING)
        add_test(NAME settings_page_smoke COMMAND settings_page_demo --smoke)
        set_tests_properties(settings_page_smoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 20)
    endif()
endif()
