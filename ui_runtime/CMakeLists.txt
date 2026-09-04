cmake_minimum_required(VERSION 3.16)

project(ui_runtime C)

option(UI_RUNTIME_BUILD_TESTS "Build ui_runtime independent tests" OFF)
set(UI_RUNTIME_LVGL_TARGET "" CACHE STRING
    "Existing LVGL CMake target used by ui_runtime")

if(UI_RUNTIME_BUILD_TESTS)
    add_library(ui_runtime_fake_lvgl STATIC
        tests/fake_lvgl/fake_lvgl.c
    )
    target_include_directories(ui_runtime_fake_lvgl PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fake_lvgl
    )
endif()

add_library(ui_runtime STATIC
    src/ui_overlay.c
    src/ui_root_host.c
    src/ui_runtime.c
)
add_library(ui_runtime::ui_runtime ALIAS ui_runtime)

target_compile_features(ui_runtime PUBLIC c_std_11)
target_include_directories(ui_runtime
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(UI_RUNTIME_BUILD_TESTS)
    target_link_libraries(ui_runtime PUBLIC ui_runtime_fake_lvgl)
elseif(UI_RUNTIME_LVGL_TARGET AND TARGET ${UI_RUNTIME_LVGL_TARGET})
    target_link_libraries(ui_runtime PUBLIC ${UI_RUNTIME_LVGL_TARGET})
elseif(TARGET lvgl::lvgl)
    target_link_libraries(ui_runtime PUBLIC lvgl::lvgl)
elseif(TARGET lvgl)
    target_link_libraries(ui_runtime PUBLIC lvgl)
else()
    message(FATAL_ERROR
        "Provide UI_RUNTIME_LVGL_TARGET, lvgl, or lvgl::lvgl when tests are OFF")
endif()

if(UI_RUNTIME_BUILD_TESTS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ui_runtime PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(UI_RUNTIME_BUILD_TESTS)
    enable_testing()
    add_executable(ui_runtime_tests
        tests/test_ui_runtime.c
    )
    target_link_libraries(ui_runtime_tests PRIVATE ui_runtime)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ui_runtime_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    add_test(NAME ui_runtime_tests COMMAND ui_runtime_tests)
endif()
