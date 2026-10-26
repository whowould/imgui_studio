cmake_minimum_required(VERSION 3.20)
project(imgui_studio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${upper} ${CMAKE_SOURCE_DIR}/bin)
endforeach()

add_compile_definitions(IMGUI_DEFINE_MATH_OPERATORS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
add_compile_options(/utf-8 /permissive-)

set(IMGUI third_party/imgui)
add_library(imgui STATIC
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_win32.cpp ${IMGUI}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${IMGUI} ${IMGUI}/backends)

# The widget kit: compiled into the editor and copied next to every export.
add_library(kit STATIC kit/kit.cpp)
target_include_directories(kit PUBLIC kit)
target_link_libraries(kit PUBLIC imgui)
target_compile_options(kit PRIVATE /W4)

add_executable(imgui_studio WIN32
    src/main.cpp src/editor.cpp src/document.cpp src/json.cpp src/names.cpp src/codegen.cpp src/preview.cpp
    src/platform.cpp src/plugins.cpp)
target_link_libraries(imgui_studio PRIVATE kit d3d11 dxgi dwmapi comdlg32 ole32 shell32 windowscodecs)
target_compile_options(imgui_studio PRIVATE /W4)

# The live-link runtime ships with exports (kit_live/); built here only to keep it compiling.
add_library(kit_live_check STATIC src/live.cpp)
target_link_libraries(kit_live_check PRIVATE kit)
target_compile_options(kit_live_check PRIVATE /W4)

# An example widget plugin: bin/plugins/widgets.dll.
add_library(widgets SHARED examples/widget_plugin/widgets.cpp)
target_link_libraries(widgets PRIVATE kit)
target_compile_options(widgets PRIVATE /W4)
set_target_properties(widgets PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/plugins
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/plugins)

# Exports copy the kit and the live-link sources from next to the exe.
set(LIVE_SOURCES src/live.h src/live.cpp src/document.h src/document.cpp src/json.h src/json.cpp src/names.h src/names.cpp
    src/preview.h src/preview.cpp)
add_custom_command(TARGET imgui_studio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin/kit/live
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/kit/kit.h ${CMAKE_SOURCE_DIR}/kit/kit.cpp
            ${CMAKE_SOURCE_DIR}/kit/inter_font.h ${CMAKE_SOURCE_DIR}/kit/studio_plugin.h ${CMAKE_SOURCE_DIR}/bin/kit
    COMMAND ${CMAKE_COMMAND} -E copy ${LIVE_SOURCES} ${CMAKE_SOURCE_DIR}/bin/kit/live
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
