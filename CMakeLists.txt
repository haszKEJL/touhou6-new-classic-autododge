cmake_minimum_required(VERSION 3.20)
project(TouhouAssist VERSION 1.7.0 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project requires Windows x64.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_executable(dodge_assist native/main.cpp)
add_executable(scarlet_launcher native/injector.cpp)
add_library(scarlet_assist SHARED native/main.cpp native/overlay.cpp
    vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp vendor/imgui/imgui_tables.cpp vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_win32.cpp vendor/imgui/backends/imgui_impl_dx11.cpp
    vendor/minhook/src/buffer.c vendor/minhook/src/hook.c vendor/minhook/src/trampoline.c vendor/minhook/src/hde/hde64.c)
target_compile_definitions(scarlet_assist PRIVATE TH_ASSIST_DLL)
target_include_directories(scarlet_assist PRIVATE vendor/imgui vendor/imgui/backends vendor/minhook/include)
set_target_properties(scarlet_assist PROPERTIES PREFIX "")
target_link_libraries(scarlet_assist PRIVATE d3d11 dxgi d3dcompiler dwmapi gdi32)
foreach(target dodge_assist scarlet_launcher scarlet_assist)
    target_link_libraries(${target} PRIVATE bcrypt user32)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    elseif(MINGW)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endforeach()
enable_testing()
add_test(NAME behavior COMMAND dodge_assist --self-test)

add_executable(overlay_layout_test native/overlay_layout_test.cpp
    vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp vendor/imgui/imgui_tables.cpp vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_win32.cpp vendor/imgui/backends/imgui_impl_dx11.cpp
    vendor/minhook/src/buffer.c vendor/minhook/src/hook.c vendor/minhook/src/trampoline.c vendor/minhook/src/hde/hde64.c)
target_include_directories(overlay_layout_test PRIVATE vendor/imgui vendor/imgui/backends vendor/minhook/include)
target_link_libraries(overlay_layout_test PRIVATE user32 d3d11 dxgi d3dcompiler dwmapi gdi32)
if(MSVC)
    target_compile_options(overlay_layout_test PRIVATE /W4 /utf-8)
elseif(MINGW)
    target_link_options(overlay_layout_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_test(NAME panel_layout COMMAND overlay_layout_test)

