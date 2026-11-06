cmake_minimum_required(VERSION 3.20)
# The mod version; mod/Byteboard/Scripts/main.lua carries the same one (tools/package.mjs checks).
project(Byteboard VERSION 0.1.0 LANGUAGES C CXX)

# In-game overlay for Byteboard, loaded into the game by the Byteboard Lua mod (mod/Byteboard).
#   Byteboard.dll          the game DLL; it exports nothing. It also extracts the game data from the
#                          game's pak (extract/) when the data is missing or the pak changed.
#   ByteboardPreview.dll   the same overlay plus src/preview_capture.cpp (the Byteboard_Preview
#                          export and the frame capture); only preview.exe loads it
#   preview.exe            renders the overlay outside the game into a BMP
#   byteboard-extract.exe  runs the data extraction alone (development and checks)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT, so the DLL needs no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Every target sets its exception model below; CMake's default /EHsc next to /EHa would only produce a
# command-line warning.
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
# preview.exe loads the preview DLL from its own folder, so every configuration writes straight into
# bin/. The generator expression keeps multi-configuration generators from appending a
# per-configuration subfolder. build.cmd copies Byteboard.dll into mod/Byteboard.
set(BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin/$<0:>)

# Compiler settings shared by Dear ImGui and the overlay.
add_library(overlay_flags INTERFACE)
target_compile_definitions(overlay_flags INTERFACE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    # ImVec2 + ImVec2 and friends; Dear ImGui's own sources define it too.
    IMGUI_DEFINE_MATH_OPERATORS
    RD_VERSION="${PROJECT_VERSION}")
# /EHa: catch (...) also catches access violations, and destructors run when a fault unwinds a frame.
target_compile_options(overlay_flags INTERFACE /utf-8 /W3 /EHa)

# Dear ImGui (git submodule), built once for both DLLs.
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC overlay_flags)

# Game data extraction (extract_core library and byteboard-extract.exe).
include(extract/extract.cmake)

# The overlay, compiled once and linked into both DLLs.
set(OVERLAY_SOURCES
    src/bridge.cpp
    src/bridge.h
    src/catalog.cpp
    src/catalog.h
    src/dllmain.cpp
    src/gamedata.cpp
    src/gamedata.h
    src/hooks.cpp
    src/hooks.h
    src/json.cpp
    src/json.h
    src/paths.cpp
    src/paths.h
    src/render.cpp
    src/render.h
    src/ui_catalog.cpp
    src/ui_printer.cpp
    src/ui_state.cpp
    src/ui_state.h
    src/ui_style.cpp
    src/ui_style.h
    src/ui_widgets.cpp
    src/ui_widgets.h
    src/ui_windows.h
    src/util.cpp
    src/util.h)
add_library(overlay_core OBJECT ${OVERLAY_SOURCES})
# "extract/extract.h" resolves from the overlay folder.
target_include_directories(overlay_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(overlay_core PUBLIC imgui extract_core d3d11 dxgi)

add_library(Byteboard SHARED)
target_link_libraries(Byteboard PRIVATE overlay_core)

add_library(ByteboardPreview SHARED src/preview_capture.cpp)
target_link_libraries(ByteboardPreview PRIVATE overlay_core)

add_executable(preview preview/main.cpp)
target_compile_definitions(preview PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(preview PRIVATE /utf-8 /W3 /EHsc)
target_link_libraries(preview PRIVATE d3d11)
# preview.exe loads the preview DLL at run time: it is built first, not linked.
add_dependencies(preview ByteboardPreview)

set_target_properties(Byteboard ByteboardPreview preview PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
