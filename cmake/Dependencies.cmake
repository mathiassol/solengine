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

# ---- GLFW (windowing — Vulkan surface via glfwCreateWindowSurface) ----
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

# ---- Vulkan-Headers (official KhronosGroup headers, no SDK required) ----
FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.3.290
    GIT_SHALLOW    TRUE)

# ---- Volk (Vulkan meta-loader — dynamically loads vulkan-1.dll at runtime) ----
# No link-time dependency on the Vulkan SDK loader library.
set(VOLK_HEADERS_ONLY       OFF CACHE BOOL "" FORCE)
set(VOLK_PULL_IN_VULKAN     ON  CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_INSTALL_DIR "${FETCHCONTENT_BASE_DIR}/vulkanheaders-src" CACHE PATH "" FORCE)
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.3.290.0
    GIT_SHALLOW    TRUE)

# ---- Vulkan Memory Allocator (GPU heap management) ----
FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE)

# ---- glslang (GLSL → SPIR-V compiler, used at configure time) ----
set(GLSLANG_TESTS                  OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL         OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL                 OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES        ON  CACHE BOOL "" FORCE)
set(ENABLE_HLSL                    OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT                     OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG        14.3.0
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

# ---- Dear ImGui (compiled as a small static lib — GLFW + Vulkan backends) ----
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

# ---- stb (stb_image, stb_image_resize for texture loading) ----
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)

message(STATUS "Fetching dependencies (first run downloads a lot)...")
FetchContent_MakeAvailable(glm entt glfw VulkanHeaders volk vma glslang
                           JoltPhysics imgui cgltf json stb)

# ---- Vulkan-Headers: expose as an INTERFACE target ----
if(NOT TARGET Vulkan::Headers)
    add_library(VulkanHeaders_iface INTERFACE)
    target_include_directories(VulkanHeaders_iface INTERFACE
        ${VulkanHeaders_SOURCE_DIR}/include)
    add_library(Vulkan::Headers ALIAS VulkanHeaders_iface)
endif()

# ---- Volk: set up to use our Vulkan-Headers ----
# volk fetches its own headers unless we override VOLK_VULKAN_HEADERS_PATH
include_directories(${VulkanHeaders_SOURCE_DIR}/include)
if(TARGET volk)
    target_include_directories(volk PUBLIC
        ${VulkanHeaders_SOURCE_DIR}/include)
    if(WIN32)
        target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
    endif()
endif()

# ---- VMA: header-only INTERFACE ----
add_library(vma_iface INTERFACE)
target_include_directories(vma_iface INTERFACE ${vma_SOURCE_DIR}/include)
target_link_libraries(vma_iface INTERFACE volk)

# ---- Build ImGui as a static lib (GLFW + Vulkan backends) ----
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES   # use volk / dynamic dispatch
    VK_NO_PROTOTYPES)                 # match our build
target_link_libraries(imgui PUBLIC glfw volk)
target_include_directories(imgui PUBLIC ${VulkanHeaders_SOURCE_DIR}/include)

# ---- cgltf as INTERFACE; impl placed in one TU in engine ----
add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${cgltf_SOURCE_DIR})

# ---- stb as INTERFACE ----
add_library(stb_iface INTERFACE)
target_include_directories(stb_iface INTERFACE ${stb_SOURCE_DIR})

