# =============================================================================
#  Glass Obsidian -- a collapse-safe window + glass theme library for Dear ImGui
#
#  Targets
#    obsidian           static library (theme + widgets + window state machine)
#    obsidian_headless  verification harness: 13 invariants + PNG screenshots,
#                       needs nothing but a C++17 compiler (no GPU, no window
#                       system). Exit code = number of failed invariants.
#    obsidian_demo_dx11 Win32 + DirectX 11 demo app   (OBSIDIAN_WITH_BACKENDS)
#    obsidian_demo_gl3  SDL2 + OpenGL 3 demo app      (OBSIDIAN_WITH_BACKENDS)
#
#  The windowed demos need a FULL Dear ImGui tree (this repo vendors only the
#  backend-free core). Point OBSIDIAN_IMGUI_DIR at an imgui checkout that has
#  the backends/ folder, e.g. -DOBSIDIAN_IMGUI_DIR=$HOME/imgui
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(glass_obsidian_imgui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(OBSIDIAN_BUILD_TESTS   "Build the headless verification harness" ON)
option(OBSIDIAN_WITH_BACKENDS "Build windowed demo apps (needs backends + SDL2/GL or Win32/DX11)" OFF)
set(OBSIDIAN_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui" CACHE PATH
    "Dear ImGui source root (needs backends/ when OBSIDIAN_WITH_BACKENDS=ON)")

# -----------------------------------------------------------------------------
#  library (+ the vendored backend-free imgui core, so one target is enough)
# -----------------------------------------------------------------------------
set(IMGUI_CORE
    "${OBSIDIAN_IMGUI_DIR}/imgui.cpp"
    "${OBSIDIAN_IMGUI_DIR}/imgui_demo.cpp"
    "${OBSIDIAN_IMGUI_DIR}/imgui_draw.cpp"
    "${OBSIDIAN_IMGUI_DIR}/imgui_tables.cpp"
    "${OBSIDIAN_IMGUI_DIR}/imgui_widgets.cpp")

add_library(obsidian STATIC
    src/palette.cpp
    src/theme.cpp
    src/glass.cpp
    src/window.cpp
    ${IMGUI_CORE})

target_include_directories(obsidian
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE "${OBSIDIAN_IMGUI_DIR}")

if(MSVC)
    target_compile_options(obsidian PRIVATE /W4)
else()
    target_compile_options(obsidian PRIVATE -Wall -Wextra)
endif()

# -----------------------------------------------------------------------------
#  headless verification (default ON -- this is the proof the theme ships with)
# -----------------------------------------------------------------------------
if(OBSIDIAN_BUILD_TESTS)
    add_executable(obsidian_headless
        tests/headless_main.cpp
        app/demo_ui.cpp)
    target_link_libraries(obsidian_headless PRIVATE obsidian)
    target_include_directories(obsidian_headless PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/app"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools")
endif()

# -----------------------------------------------------------------------------
#  windowed demos (optional)
# -----------------------------------------------------------------------------
if(OBSIDIAN_WITH_BACKENDS)
    if(NOT EXISTS "${OBSIDIAN_IMGUI_DIR}/backends/imgui_impl_win32.cpp" AND
       NOT EXISTS "${OBSIDIAN_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp")
        message(FATAL_ERROR
            "OBSIDIAN_WITH_BACKENDS=ON but ${OBSIDIAN_IMGUI_DIR} has no backends/ "
            "folder. Point OBSIDIAN_IMGUI_DIR at a full Dear ImGui checkout.")
    endif()

    if(WIN32)
        add_executable(obsidian_demo_dx11
            app/main_win32_dx11.cpp
            app/demo_ui.cpp
            "${OBSIDIAN_IMGUI_DIR}/backends/imgui_impl_win32.cpp"
            "${OBSIDIAN_IMGUI_DIR}/backends/imgui_impl_dx11.cpp")
        target_link_libraries(obsidian_demo_dx11 PRIVATE obsidian d3d11 dxgi)
        set_target_properties(obsidian_demo_dx11 PROPERTIES WIN32_EXECUTABLE TRUE)  # wWinMain
        target_include_directories(obsidian_demo_dx11 PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/app"
            "${OBSIDIAN_IMGUI_DIR}" "${OBSIDIAN_IMGUI_DIR}/backends")
    else()
        find_package(SDL2 REQUIRED)
        find_package(OpenGL REQUIRED)
        add_executable(obsidian_demo_gl3
            app/main_sdl2_gl3.cpp
            app/demo_ui.cpp
            "${OBSIDIAN_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp"
            "${OBSIDIAN_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp")
        target_link_libraries(obsidian_demo_gl3 PRIVATE obsidian SDL2::SDL2 OpenGL::GL)
        target_include_directories(obsidian_demo_gl3 PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/app"
            "${OBSIDIAN_IMGUI_DIR}" "${OBSIDIAN_IMGUI_DIR}/backends")
    endif()
endif()

# -----------------------------------------------------------------------------
#  install
# -----------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS obsidian ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/obsidian DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
