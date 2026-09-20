cmake_minimum_required(VERSION 3.16)

project(photo_gallery LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(PHOTO_GALLERY_LVGL_TARGET "lvgl" CACHE STRING
    "Existing LVGL target used by photo_gallery")
option(PHOTO_GALLERY_BUILD_DEMO "Build the SDL photo gallery demo" OFF)
set(PHOTO_GALLERY_LVGL_SOURCE "" CACHE PATH
    "LVGL 9 source checkout used by the standalone demo")
set(PHOTO_GALLERY_SDL2_DIR "" CACHE PATH
    "Directory containing SDL2Config.cmake for the standalone demo")

if(PHOTO_GALLERY_BUILD_DEMO AND NOT TARGET ${PHOTO_GALLERY_LVGL_TARGET})
    if(NOT EXISTS "${PHOTO_GALLERY_LVGL_SOURCE}/lv_version.h")
        message(FATAL_ERROR
            "Set PHOTO_GALLERY_LVGL_SOURCE to an LVGL 9 source checkout")
    endif()
    if(NOT EXISTS "${PHOTO_GALLERY_SDL2_DIR}/sdl2-config.cmake")
        message(FATAL_ERROR
            "Set PHOTO_GALLERY_SDL2_DIR to the SDL2 CMake package directory")
    endif()

    set(SDL2_DIR "${PHOTO_GALLERY_SDL2_DIR}")
    find_package(SDL2 REQUIRED CONFIG COMPONENTS SDL2)
    set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/demo/lv_conf.h"
        CACHE FILEPATH "LVGL configuration" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        "${PHOTO_GALLERY_LVGL_SOURCE}"
        "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
    target_include_directories(${PHOTO_GALLERY_LVGL_TARGET}
        PUBLIC "${PHOTO_GALLERY_SDL2_DIR}/../../../include")
    target_link_libraries(${PHOTO_GALLERY_LVGL_TARGET} PUBLIC SDL2::SDL2)
    set_property(TARGET ${PHOTO_GALLERY_LVGL_TARGET} PROPERTY C_EXTENSIONS ON)
endif()

if(NOT TARGET ${PHOTO_GALLERY_LVGL_TARGET})
    message(FATAL_ERROR
        "photo_gallery expects an existing LVGL target. "
        "Add LVGL first or set PHOTO_GALLERY_LVGL_TARGET.")
endif()

add_library(photo_gallery STATIC
    src/photo_gallery.c)
add_library(photo_gallery::photo_gallery ALIAS photo_gallery)

target_include_directories(photo_gallery
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(photo_gallery PUBLIC ${PHOTO_GALLERY_LVGL_TARGET})

if(MSVC)
    target_compile_options(photo_gallery PRIVATE /W4)
else()
    target_compile_options(photo_gallery PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(PHOTO_GALLERY_BUILD_DEMO)
    add_executable(photo_gallery_demo
        demo/main.c)
    target_link_libraries(photo_gallery_demo
        PRIVATE photo_gallery::photo_gallery SDL2::SDL2)
    if(MINGW)
        target_compile_options(photo_gallery_demo
            PRIVATE -Wall -Wextra -Werror -Wpedantic)
    endif()

    add_custom_command(TARGET photo_gallery_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PHOTO_GALLERY_SDL2_DIR}/../../../bin/SDL2.dll"
            "$<TARGET_FILE_DIR:photo_gallery_demo>/SDL2.dll")

    include(CTest)
    if(BUILD_TESTING)
        add_test(NAME photo_gallery_smoke
            COMMAND photo_gallery_demo --smoke)
        set_tests_properties(photo_gallery_smoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 20)
    endif()
endif()
