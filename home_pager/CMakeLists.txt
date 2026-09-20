cmake_minimum_required(VERSION 3.16)

project(home_pager LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(HOME_PAGER_LVGL_TARGET "lvgl" CACHE STRING
    "Existing LVGL target used by home_pager")
option(HOME_PAGER_BUILD_DEMO "Build the SDL Home pager demo" OFF)
set(HOME_PAGER_LVGL_SOURCE "" CACHE PATH
    "LVGL 9 source checkout used by the standalone demo")
set(HOME_PAGER_SDL2_DIR "" CACHE PATH
    "Directory containing SDL2Config.cmake for the standalone demo")
set(HOME_PAGER_UNIGUI_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../ref/unigui" CACHE PATH
    "unigui source tree used only for demo fonts")

if(HOME_PAGER_BUILD_DEMO AND NOT TARGET ${HOME_PAGER_LVGL_TARGET})
    if(NOT EXISTS "${HOME_PAGER_LVGL_SOURCE}/lv_version.h")
        message(FATAL_ERROR
            "Set HOME_PAGER_LVGL_SOURCE to an LVGL 9 source checkout")
    endif()
    if(NOT EXISTS "${HOME_PAGER_SDL2_DIR}/sdl2-config.cmake")
        message(FATAL_ERROR
            "Set HOME_PAGER_SDL2_DIR to the SDL2 CMake package directory")
    endif()

    set(SDL2_DIR "${HOME_PAGER_SDL2_DIR}")
    find_package(SDL2 REQUIRED CONFIG COMPONENTS SDL2)
    set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/demo/lv_conf.h"
        CACHE FILEPATH "LVGL configuration" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        "${HOME_PAGER_LVGL_SOURCE}"
        "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
    target_include_directories(${HOME_PAGER_LVGL_TARGET}
        PUBLIC "${HOME_PAGER_SDL2_DIR}/../../../include")
    target_link_libraries(${HOME_PAGER_LVGL_TARGET} PUBLIC SDL2::SDL2)
    set_property(TARGET ${HOME_PAGER_LVGL_TARGET} PROPERTY C_EXTENSIONS ON)
endif()

if(NOT TARGET ${HOME_PAGER_LVGL_TARGET})
    message(FATAL_ERROR
        "home_pager expects an existing LVGL target. "
        "Add LVGL first or set HOME_PAGER_LVGL_TARGET.")
endif()

add_library(home_pager STATIC
    src/home_pager.c)
add_library(home_pager::home_pager ALIAS home_pager)

target_include_directories(home_pager
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(home_pager PUBLIC ${HOME_PAGER_LVGL_TARGET})

if(MSVC)
    target_compile_options(home_pager PRIVATE /W4)
else()
    target_compile_options(home_pager PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(HOME_PAGER_BUILD_DEMO)
    if(NOT EXISTS
       "${HOME_PAGER_UNIGUI_SOURCE}/fonts/SourceHanSansCN_Medium_24px_4bpp.c")
        message(FATAL_ERROR
            "HOME_PAGER_UNIGUI_SOURCE does not contain the expected fonts")
    endif()

    add_executable(home_pager_demo
        demo/main.c
        "${HOME_PAGER_UNIGUI_SOURCE}/fonts/SourceHanSansCN_Medium_28px_4bpp.c"
        "${HOME_PAGER_UNIGUI_SOURCE}/fonts/SourceHanSansCN_Regular_24px_4bpp.c")
    target_link_libraries(home_pager_demo
        PRIVATE home_pager::home_pager SDL2::SDL2)
    if(MINGW)
        target_compile_options(home_pager_demo
            PRIVATE -Wall -Wextra -Werror -Wpedantic)
    endif()

    add_custom_command(TARGET home_pager_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HOME_PAGER_SDL2_DIR}/../../../bin/SDL2.dll"
            "$<TARGET_FILE_DIR:home_pager_demo>/SDL2.dll")

    include(CTest)
    if(BUILD_TESTING)
        add_test(NAME home_pager_smoke COMMAND home_pager_demo --smoke)
        set_tests_properties(home_pager_smoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 20)
    endif()
endif()
