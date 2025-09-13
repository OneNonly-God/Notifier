cmake_minimum_required(VERSION 3.10)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(NotesApp)

set(CMAKE_CXX_STANDARD 17)

# ImGui source files
set(IMGUI_SRC
    resources/imgui/imgui.cpp
    resources/imgui/imgui_draw.cpp
    resources/imgui/imgui_tables.cpp
    resources/imgui/imgui_widgets.cpp
    resources/imgui/backends/imgui_impl_glfw.cpp
    resources/imgui/backends/imgui_impl_opengl3.cpp
)

add_executable(notifier main.cpp ${IMGUI_SRC})

target_include_directories(notifier PRIVATE ${CMAKE_SOURCE_DIR}/resources/imgui 
    ${CMAKE_SOURCE_DIR}/resources/imgui/backends)

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

target_link_libraries(notifier PRIVATE glfw OpenGL::GL)