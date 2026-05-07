# ---- engine_core: shared library that games link against ----
add_library(engine_core SHARED
    src/engine.cpp
    src/platform/window.cpp
    src/render/renderer.cpp
    src/render/mesh.cpp
    src/render/texture.cpp
    src/physics/physics.cpp
    src/asset/gltf_loader.cpp
    src/ui/imgui_layer.cpp
    src/log.cpp
    src/scene/scene.cpp
    src/scene/physics_nodes.cpp
    src/scene/scene_manager.cpp
    src/project/project.cpp
)

target_include_directories(engine_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(engine_core
    PUBLIC
        glm::glm
        EnTT::EnTT
        imgui          # PUBLIC so game DLLs can link their own static copy;
                       # they must call ImGui::SetCurrentContext(e->imgui_context())
    PRIVATE
        glfw
        bgfx
        bx
        bimg
        Jolt
        cgltf
        nlohmann_json::nlohmann_json)

target_compile_definitions(engine_core
    PRIVATE SOL_ENGINE_BUILD=1
    PUBLIC  $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

set_target_properties(engine_core PROPERTIES
    OUTPUT_NAME "sol_engine"
    WINDOWS_EXPORT_ALL_SYMBOLS ON)

# ---- Default shaders: compiled to embeddable C headers via bgfx's shaderc.
include(${bgfx_SOURCE_DIR}/cmake/bgfxToolUtils.cmake)
set(BGFX_SHADER_INCLUDE_PATH ${bgfx_SOURCE_DIR}/bgfx/src)

set(SOL_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders)
set(SOL_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders)

# Shadow shaders (minimal — no tangent/normal required)
set(SOL_SHADOW_PROFILES s_5_0 s_6_0 120 100_es spirv)

bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS     ${SOL_SHADER_DIR}/vs_shadow.sc
    VARYING_DEF ${SOL_SHADER_DIR}/varying_shadow.def.sc
    OUTPUT_DIR  ${SOL_SHADER_OUT}
    OUT_FILES_VAR SOL_SHADOW_VS_OUT
    AS_HEADERS
    PROFILES ${SOL_SHADOW_PROFILES})

bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS     ${SOL_SHADER_DIR}/fs_shadow.sc
    VARYING_DEF ${SOL_SHADER_DIR}/varying_shadow.def.sc
    OUTPUT_DIR  ${SOL_SHADER_OUT}
    OUT_FILES_VAR SOL_SHADOW_FS_OUT
    AS_HEADERS
    PROFILES ${SOL_SHADOW_PROFILES})

# PBR shaders (full lighting — uses tangents, shadow sampler, etc.)
set(SOL_PBR_PROFILES s_5_0 s_6_0 150 300_es spirv)

bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS     ${SOL_SHADER_DIR}/vs_pbr.sc
    VARYING_DEF ${SOL_SHADER_DIR}/varying_pbr.def.sc
    OUTPUT_DIR  ${SOL_SHADER_OUT}
    OUT_FILES_VAR SOL_PBR_VS_OUT
    AS_HEADERS
    PROFILES ${SOL_PBR_PROFILES})

bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS     ${SOL_SHADER_DIR}/fs_pbr.sc
    VARYING_DEF ${SOL_SHADER_DIR}/varying_pbr.def.sc
    OUTPUT_DIR  ${SOL_SHADER_OUT}
    OUT_FILES_VAR SOL_PBR_FS_OUT
    AS_HEADERS
    PROFILES ${SOL_PBR_PROFILES})

target_sources(engine_core PRIVATE
    ${SOL_SHADOW_VS_OUT} ${SOL_SHADOW_FS_OUT}
    ${SOL_PBR_VS_OUT}    ${SOL_PBR_FS_OUT})
target_include_directories(engine_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

# ---- engine.exe: launcher/runtime that loads game DLLs ----
add_executable(engine src/main.cpp)
target_link_libraries(engine PRIVATE engine_core)
set_target_properties(engine PROPERTIES OUTPUT_NAME "sol")
