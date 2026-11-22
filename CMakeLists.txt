cmake_minimum_required(VERSION 3.24)
project(tapir LANGUAGES CXX RC)

set(APP_NAME tapir)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT MSVC)
  message(FATAL_ERROR "tapir requires MSVC from Developer PowerShell for VS")
endif()

add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc)
add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/GL>)
add_link_options($<$<CONFIG:Release>:/LTCG> $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)

configure_file(resources/app.rc ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)

add_executable(${APP_NAME} WIN32
  src/main.cpp
  src/app.cpp
  src/app.h
  src/config.cpp
  src/config.h
  src/hotkey.cpp
  src/hotkey.h
  src/window.cpp
  src/window.h
  src/util/placement.cpp
  src/util/placement.h
  ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)
target_include_directories(${APP_NAME} PRIVATE src)
target_link_libraries(${APP_NAME} PRIVATE user32 shcore)
target_link_options(${APP_NAME} PRIVATE /MANIFEST:NO)

option(TAPIR_BUILD_TESTS "Build tapir unit tests" ON)

if(TAPIR_BUILD_TESTS)
  enable_testing()
  add_executable(tapir_tests
    tests/doctest_main.test.cpp
    tests/smoke.test.cpp
    tests/hotkey.test.cpp
    tests/placement.test.cpp
    tests/config.test.cpp
    src/config.cpp
    src/hotkey.cpp
    src/util/placement.cpp
  )
  target_include_directories(tapir_tests PRIVATE src third_party/doctest)
  target_link_libraries(tapir_tests PRIVATE user32)
  add_test(NAME tapir_tests COMMAND tapir_tests)
endif()
