cmake_minimum_required(VERSION 3.16)

project(paged_list LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(PAGED_LIST_LVGL_TARGET "lvgl" CACHE STRING
    "Existing LVGL target used by paged_list")
option(PAGED_LIST_BUILD_TESTS "Build paged_list smoke tests" OFF)
option(PAGED_LIST_BUILD_DEMO "Build the SDL paged-list demo" OFF)
set(PAGED_LIST_LVGL_SOURCE "" CACHE PATH
    "LVGL 9 source checkout used by standalone targets")
set(PAGED_LIST_SDL2_DIR "" CACHE PATH
    "Directory containing SDL2Config.cmake for the standalone demo")

if((PAGED_LIST_BUILD_TESTS OR PAGED_LIST_BUILD_DEMO) AND
   NOT TARGET ${PAGED_LIST_LVGL_TARGET})
    if(NOT EXISTS "${PAGED_LIST_LVGL_SOURCE}/lv_version.h")
        message(FATAL_ERROR
            "Set PAGED_LIST_LVGL_SOURCE when building standalone targets")
    endif()
    if(PAGED_LIST_BUILD_DEMO)
        if(NOT EXISTS "${PAGED_LIST_SDL2_DIR}/sdl2-config.cmake")
            message(FATAL_ERROR
                "Set PAGED_LIST_SDL2_DIR when building the standalone demo")
        endif()
        set(SDL2_DIR "${PAGED_LIST_SDL2_DIR}")
        find_package(SDL2 REQUIRED CONFIG COMPONENTS SDL2)
        set(PAGED_LIST_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/demo/lv_conf.h")
    else()
        set(PAGED_LIST_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests/lv_conf.h")
    endif()
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
    set(LV_BUILD_CONF_PATH "${PAGED_LIST_CONF_PATH}"
        CACHE FILEPATH "LVGL configuration used by paged_list tests" FORCE)
    add_subdirectory(
        "${PAGED_LIST_LVGL_SOURCE}"
        "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
    if(PAGED_LIST_BUILD_DEMO)
        target_include_directories(${PAGED_LIST_LVGL_TARGET}
            PUBLIC "${PAGED_LIST_SDL2_DIR}/../../../include")
        target_link_libraries(${PAGED_LIST_LVGL_TARGET} PUBLIC SDL2::SDL2)
        set_property(TARGET ${PAGED_LIST_LVGL_TARGET} PROPERTY C_EXTENSIONS ON)
    endif()
endif()

if(PAGED_LIST_BUILD_DEMO)
    add_executable(paged_list_demo demo/main.c)
    target_link_libraries(paged_list_demo
        PRIVATE paged_list::paged_list SDL2::SDL2)
    if(MINGW)
        target_compile_options(paged_list_demo
            PRIVATE -Wall -Wextra -Werror -Wpedantic)
    endif()
    add_custom_command(TARGET paged_list_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PAGED_LIST_SDL2_DIR}/../../../bin/SDL2.dll"
            "$<TARGET_FILE_DIR:paged_list_demo>/SDL2.dll")
endif()

if(NOT TARGET ${PAGED_LIST_LVGL_TARGET})
    message(FATAL_ERROR
        "paged_list expects an existing LVGL target. Add LVGL first or set "
        "PAGED_LIST_LVGL_TARGET.")
endif()

add_library(paged_list STATIC
    src/paged_list.c)
add_library(paged_list::paged_list ALIAS paged_list)

target_include_directories(paged_list
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(paged_list PUBLIC ${PAGED_LIST_LVGL_TARGET})

if(MSVC)
    target_compile_options(paged_list PRIVATE /W4)
else()
    target_compile_options(paged_list PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(PAGED_LIST_BUILD_TESTS)
    include(CTest)
    add_executable(paged_list_smoke tests/paged_list_smoke.c)
    target_link_libraries(paged_list_smoke PRIVATE paged_list::paged_list)
    if(MSVC)
        target_compile_options(paged_list_smoke PRIVATE /W4 /WX)
    else()
        target_compile_options(paged_list_smoke PRIVATE -Wall -Wextra -Werror -Wpedantic)
    endif()
    if(BUILD_TESTING)
        add_test(NAME paged_list_smoke COMMAND paged_list_smoke)
    endif()
endif()
