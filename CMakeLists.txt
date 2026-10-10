cmake_minimum_required(VERSION 3.16)
project(caro_temple C)

if(MSVC)
  # MSVC has no C99 switch; its C11 mode accepts all the C99 we use.
  set(CMAKE_C_STANDARD 11)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
else()
  set(CMAKE_C_STANDARD 99)
endif()
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(CARO_BUILD_GUI "Build the raylib GUI version" ON)
option(CARO_BUILD_TUI "Build the zero-dependency terminal version" ON)

if(MSVC)
  set(CARO_WARN /W3)
else()
  set(CARO_WARN -Wall -Wextra -Wno-unused-parameter)
endif()

# ---- core (shared, no dependencies) ----
file(GLOB CORE_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/core/*.c)
add_library(caro_core STATIC ${CORE_SRC})
target_include_directories(caro_core PUBLIC ${CMAKE_SOURCE_DIR}/core)
target_compile_options(caro_core PRIVATE ${CARO_WARN})
if(NOT WIN32)
  target_link_libraries(caro_core PUBLIC m)
endif()

# ---- tests ----
enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/test_*.c)
foreach(t ${TEST_SRC})
  get_filename_component(name ${t} NAME_WE)
  add_executable(${name} ${t})
  target_link_libraries(${name} caro_core)
  target_compile_options(${name} PRIVATE ${CARO_WARN})
  add_test(NAME ${name} COMMAND ${name})
endforeach()

# ---- terminal version (no libraries) ----
if(CARO_BUILD_TUI)
  file(GLOB TUI_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tui/*.c)
  add_executable(caro_tui ${TUI_SRC})
  target_link_libraries(caro_tui caro_core)
  target_compile_options(caro_tui PRIVATE ${CARO_WARN})
  if(NOT WIN32)
    target_compile_definitions(caro_tui PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
  endif()
endif()

# ---- GUI version (raylib) ----
if(CARO_BUILD_GUI)
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
  # Tip: -DFETCHCONTENT_SOURCE_DIR_RAYLIB=<path to raylib checkout> to avoid downloading.
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(raylib)

  file(GLOB GUI_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/gui/*.c)
  add_executable(caro ${GUI_SRC})
  find_package(Threads REQUIRED)
  target_link_libraries(caro caro_core raylib Threads::Threads)
  target_compile_options(caro PRIVATE ${CARO_WARN})
  if(APPLE)
    target_link_libraries(caro "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
  endif()
  if(WIN32 AND NOT MSVC)
    target_link_options(caro PRIVATE -mwindows)
  endif()
  add_custom_command(TARGET caro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:caro>/assets)
endif()
