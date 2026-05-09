# Third-party dependencies via FetchContent.
# First configure pulls everything; subsequent configures are cached.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---- GLM (header-only math) ----
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

# ---- EnTT (ECS, header-only) ----
FetchContent_Declare(entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.2
    GIT_SHALLOW    TRUE)

# ---- GLFW (windowing) ----
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

# ---- bgfx (rendering) via bgfx.cmake ----
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS    ON  CACHE BOOL "" FORCE)
set(BGFX_INSTALL        OFF CACHE BOOL "" FORCE)
set(BX_AMALGAMATED      ON  CACHE BOOL "" FORCE)
set(BGFX_AMALGAMATED    ON  CACHE BOOL "" FORCE)
FetchContent_Declare(bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG        v1.143.9243-536
    GIT_SHALLOW    TRUE)

# ---- Jolt Physics ----
set(TARGET_HELLO_WORLD               OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST          OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES                   OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS                OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER                    OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS              OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Build)

# ---- Dear ImGui (compiled as a small static lib) ----
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.0
    GIT_SHALLOW    TRUE)

# ---- cgltf (single-header GLB/GLTF loader) ----
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.14
    GIT_SHALLOW    TRUE)

# ---- nlohmann/json (single-header JSON, used for scene files) ----
set(JSON_BuildTests     OFF CACHE BOOL "" FORCE)
set(JSON_Install        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)

message(STATUS "Fetching dependencies (first run downloads a lot)...")
FetchContent_MakeAvailable(glm entt glfw bgfx JoltPhysics imgui cgltf json)

# ---- Build ImGui as a static lib (core + GLFW backend) ----
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

# ---- cgltf as INTERFACE; impl placed in one TU in engine ----
add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${cgltf_SOURCE_DIR})
