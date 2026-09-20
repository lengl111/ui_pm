cmake_minimum_required(VERSION 3.16)

project(quick_settings LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(QUICK_SETTINGS_LVGL_TARGET "lvgl" CACHE STRING
    "Existing LVGL target used by quick_settings")
option(QUICK_SETTINGS_BUILD_DEMO "Build the SDL quick settings demo" OFF)
set(QUICK_SETTINGS_LVGL_SOURCE "" CACHE PATH
    "LVGL 9 source checkout used by the standalone demo")
set(QUICK_SETTINGS_SDL2_DIR "" CACHE PATH
    "Directory containing SDL2Config.cmake for the standalone demo")

if(QUICK_SETTINGS_BUILD_DEMO AND NOT TARGET ${QUICK_SETTINGS_LVGL_TARGET})
    if(NOT EXISTS "${QUICK_SETTINGS_LVGL_SOURCE}/lv_version.h")
        message(FATAL_ERROR
            "Set QUICK_SETTINGS_LVGL_SOURCE to an LVGL 9 source checkout")
    endif()
    if(NOT EXISTS "${QUICK_SETTINGS_SDL2_DIR}/sdl2-config.cmake")
        message(FATAL_ERROR
            "Set QUICK_SETTINGS_SDL2_DIR to the SDL2 CMake package directory")
    endif()

    set(SDL2_DIR "${QUICK_SETTINGS_SDL2_DIR}")
    find_package(SDL2 REQUIRED CONFIG COMPONENTS SDL2)
    set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/demo/lv_conf.h"
        CACHE FILEPATH "LVGL configuration" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        "${QUICK_SETTINGS_LVGL_SOURCE}"
        "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
    target_include_directories(${QUICK_SETTINGS_LVGL_TARGET}
        PUBLIC "${QUICK_SETTINGS_SDL2_DIR}/../../../include")
    target_link_libraries(${QUICK_SETTINGS_LVGL_TARGET} PUBLIC SDL2::SDL2)
    set_property(TARGET ${QUICK_SETTINGS_LVGL_TARGET} PROPERTY C_EXTENSIONS ON)
endif()

if(NOT TARGET ${QUICK_SETTINGS_LVGL_TARGET})
    message(FATAL_ERROR
        "quick_settings expects an existing LVGL target. "
        "Add LVGL first or set QUICK_SETTINGS_LVGL_TARGET.")
endif()

add_library(quick_settings STATIC
    src/quick_settings.c)
add_library(quick_settings::quick_settings ALIAS quick_settings)

target_include_directories(quick_settings
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(quick_settings PUBLIC ${QUICK_SETTINGS_LVGL_TARGET})

if(MSVC)
    target_compile_options(quick_settings PRIVATE /W4)
else()
    target_compile_options(quick_settings PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(QUICK_SETTINGS_BUILD_DEMO)
    add_executable(quick_settings_demo
        demo/main.c)
    target_link_libraries(quick_settings_demo
        PRIVATE quick_settings::quick_settings SDL2::SDL2)
    if(MINGW)
        target_compile_options(quick_settings_demo
            PRIVATE -Wall -Wextra -Werror -Wpedantic)
    endif()

    add_custom_command(TARGET quick_settings_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QUICK_SETTINGS_SDL2_DIR}/../../../bin/SDL2.dll"
            "$<TARGET_FILE_DIR:quick_settings_demo>/SDL2.dll")

    include(CTest)
    if(BUILD_TESTING)
        add_test(NAME quick_settings_smoke
            COMMAND quick_settings_demo --smoke)
        set_tests_properties(quick_settings_smoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 20)
    endif()
endif()
