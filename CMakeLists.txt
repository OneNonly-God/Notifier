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
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)


add_executable(notifier main.cpp ${IMGUI_SRC})
target_include_directories(notifier PRIVATE ${CMAKE_SOURCE_DIR}/resources/imgui 
    ${CMAKE_SOURCE_DIR}/resources/imgui/backends ${GTK3_INCLUDE_DIRS})

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)


target_compile_options(notifier PRIVATE ${GTK3_CFLAGS_OTHER})
target_link_libraries(notifier PRIVATE glfw OpenGL::GL ${GTK3_LIBRARIES})
