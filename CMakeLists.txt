cmake_minimum_required(VERSION 3.24)

# CMake routes RC through its cmcldeps helper for Ninja on Windows. When that
# helper is missing (for example in the MSYS2 CMake package, which ships no
# cmcldeps.exe) the helper path collapses to an empty string and the generated
# rc.exe command line starts with a stray "RC" token, failing with
# "fatal error RC1107: invalid usage". app.rc has no includes worth scanning,
# so build it with plain rc.exe instead.
set(CMAKE_NINJA_CMCLDEPS_RC 0)

project(tapir LANGUAGES CXX RC)

set(APP_NAME tapir)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT MSVC)
  message(FATAL_ERROR "tapir requires MSVC from Developer PowerShell for VS")
endif()

add_compile_definitions(NOMINMAX)
add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc)
add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/GL>
                    $<$<CONFIG:Release>:/Zi>)
add_link_options($<$<CONFIG:Release>:/LTCG> $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)

configure_file(resources/app.rc ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)

add_executable(${APP_NAME} WIN32
  src/main.cpp
  src/anim.cpp
  src/anim.h
  src/backdrop.cpp
  src/backdrop.h
  src/app.cpp
  src/app.h
  src/clipboard.cpp
  src/clipboard.h
  src/config.cpp
  src/config.h
  src/config_writer.cpp
  src/config_writer.h
  src/diagnostics.cpp
  src/diagnostics.h
  src/config_file.cpp
  src/config_file.h
  src/fuzzy.cpp
  src/fuzzy.h
  src/gfx.cpp
  src/gfx.h
  src/history_codec.cpp
  src/history_codec.h
  src/util/history_store.cpp
  src/util/history_store.h
  src/hotkey.cpp
  src/hotkey.h
  src/hotkey_grammar.h
  src/icon_cache.h
  src/icons.h
  src/index.cpp
  src/index.h
  src/index_cache.cpp
  src/index_cache.h
  src/input.cpp
  src/input.h
  src/launches.cpp
  src/launches.h
  src/paste_flow.cpp
  src/paste_flow.h
  src/popup_logic.cpp
  src/popup_logic.h
  src/render.cpp
  src/render.h
  src/settings_ui.cpp
  src/settings_ui.h
  src/tray.cpp
  src/tray.h
  src/window.cpp
  src/window.h
  src/util/config_path.cpp
  src/util/config_path.h
  src/util/details_layout.cpp
  src/util/details_layout.h
  src/util/monitor_policy.cpp
  src/util/monitor_policy.h
  src/util/placement.cpp
  src/util/placement.h
  src/util/startup.cpp
  src/util/startup.h
  ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)
target_include_directories(${APP_NAME} PRIVATE src third_party)
target_link_libraries(${APP_NAME} PRIVATE user32 shell32 ole32 oleaut32 shlwapi propsys shcore crypt32 advapi32 d3d11 dxgi d2d1 dwrite dwmapi dcomp gdi32 psapi dxguid windowsapp)
target_link_options(${APP_NAME} PRIVATE /MANIFEST:NO)

option(TAPIR_BUILD_TESTS "Build tapir unit tests" ON)

if(TAPIR_BUILD_TESTS)
  enable_testing()
  add_executable(tapir_tests
    tests/doctest_main.test.cpp
    tests/smoke.test.cpp
    tests/diagnostics.test.cpp
    tests/config_file.test.cpp
    tests/config_writer.test.cpp
    tests/clipboard.test.cpp
    tests/history_codec.test.cpp
    tests/hotkey.test.cpp
    tests/placement.test.cpp
    tests/config.test.cpp
    tests/input.test.cpp
    tests/anim.test.cpp
    tests/text_layout_cache.test.cpp
    tests/themes.test.cpp
    src/util/text_layout_cache.h
    tests/fuzzy.test.cpp
    tests/icon_cache.test.cpp
    tests/index_cache.test.cpp
    tests/launches.test.cpp
    tests/popup_logic.test.cpp
    tests/paste_flow.test.cpp
    tests/details_layout.test.cpp
    tests/monitor_policy.test.cpp
    tests/startup.test.cpp
    src/anim.cpp
    src/clipboard.cpp
    src/config.cpp
    src/config_file.cpp
    src/config_writer.cpp
    src/config_writer.h
    src/diagnostics.cpp
    src/fuzzy.cpp
    src/history_codec.cpp
    src/hotkey.cpp
    src/index_cache.cpp
    src/index_cache.h
    src/input.cpp
    src/launches.cpp
    src/paste_flow.cpp
    src/popup_logic.cpp
    src/popup_logic.h
    src/util/config_path.cpp
    src/util/details_layout.cpp
    src/util/monitor_policy.cpp
    src/util/placement.cpp
    src/util/startup.cpp
  )
  target_include_directories(tapir_tests PRIVATE src third_party/doctest third_party)
  target_link_libraries(tapir_tests PRIVATE user32 advapi32)
  add_test(NAME tapir_tests COMMAND tapir_tests)
endif()
