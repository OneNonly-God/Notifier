cmake_minimum_required(VERSION 3.10)
project(Notifier)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Find dependencies ---
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
find_package(glfw3 REQUIRED)

# --- ImGui source files ---
set(IMGUI_SRC
    resources/imgui/imgui.cpp
    resources/imgui/imgui_draw.cpp
    resources/imgui/imgui_tables.cpp
    resources/imgui/imgui_widgets.cpp
    resources/imgui/backends/imgui_impl_glfw.cpp
    resources/imgui/backends/imgui_impl_opengl3.cpp
)

# --- Executable ---
add_executable(${PROJECT_NAME}
    main.cpp
    ${CMAKE_SOURCE_DIR}/resources/icon.h
    ${IMGUI_SRC}
)

# --- Include directories ---
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/resources/imgui
    ${CMAKE_SOURCE_DIR}/resources/imgui/backends
    ${CMAKE_SOURCE_DIR}/resources/
    ${GTK3_INCLUDE_DIRS}
)

# --- Compiler flags from pkg-config ---
target_compile_options(${PROJECT_NAME} PRIVATE ${GTK3_CFLAGS_OTHER})

# --- Link libraries ---
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    OpenGL::GL
    ${GTK3_LIBRARIES}
)