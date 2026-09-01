cmake_minimum_required(VERSION 3.22)
project(csd_ui_editor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)

target_link_libraries(imgui PUBLIC glfw)

add_executable(annotation_editor
  src/main.cpp
  src/engines/cocos/cocos_csd_engine.cpp
  src/resources/asset_store.cpp
)
set_target_properties(annotation_editor PROPERTIES
  OUTPUT_NAME csd_editor
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/dist"
)

set(TINYXML2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyxml2")
if(NOT EXISTS "${TINYXML2_DIR}/tinyxml2.cpp")
  message(FATAL_ERROR "tinyxml2 was not found under ${TINYXML2_DIR}.")
endif()
target_sources(annotation_editor PRIVATE "${TINYXML2_DIR}/tinyxml2.cpp")

target_include_directories(annotation_editor PRIVATE
  src
  third_party/nlohmann
  third_party/stb
  "${TINYXML2_DIR}"
)

target_link_libraries(annotation_editor PRIVATE
  imgui
  glfw
)

if(MSVC)
  target_compile_definitions(annotation_editor PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(WIN32)
  target_link_libraries(annotation_editor PRIVATE opengl32)
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(annotation_editor PRIVATE OpenGL::GL)
endif()

if(MSVC)
  target_compile_options(annotation_editor PRIVATE /W4 /permissive-)
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT annotation_editor)
else()
  target_compile_options(annotation_editor PRIVATE -Wall -Wextra -Wpedantic)
endif()
