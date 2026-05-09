# ---- engine_core: shared library (DLL mode) or static library (monolithic mode) ----
if(SOL_MONOLITHIC)
    add_library(engine_core STATIC
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
        src/project/project.cpp)
else()
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
        src/project/project.cpp)
endif()

target_include_directories(engine_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# stb_image.h lives in bgfx's bimg 3rdparty (already on disk, no extra download)
target_include_directories(engine_core PRIVATE
    ${bgfx_SOURCE_DIR}/bimg/3rdparty/stb)

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

if(SOL_MONOLITHIC)
    target_compile_definitions(engine_core
        PUBLIC SOL_STATIC_BUILD=1 SOL_ENGINE_BUILD=1
        PUBLIC $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

    set_target_properties(engine_core PROPERTIES OUTPUT_NAME "sol_engine_static")
else()
    target_compile_definitions(engine_core
        PRIVATE SOL_ENGINE_BUILD=1
        PUBLIC  $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

    set_target_properties(engine_core PROPERTIES
        OUTPUT_NAME "sol_engine"
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# ---- Default shaders: compiled to embeddable C headers via bgfx's shaderc.
# Patch bgfxToolUtils.cmake: fix malformed $<IF:$<CONFIG:Debug>:0,3> expression
# (should use commas, not colon, as separator in $<IF>)
set(_bgfx_tool_file "${bgfx_SOURCE_DIR}/cmake/bgfxToolUtils.cmake")
if(EXISTS "${_bgfx_tool_file}")
    file(READ "${_bgfx_tool_file}" _bgfx_tool_content)
    string(REPLACE
        "$<IF:$<CONFIG:Debug>:0,3>"
        "$<IF:$<CONFIG:Debug>,0,3>"
        _bgfx_tool_content "${_bgfx_tool_content}")
    file(WRITE "${_bgfx_tool_file}" "${_bgfx_tool_content}")
endif()
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

# Sky shaders (same profile as PBR — 150 minimum)
set(SOL_SKY_PROFILES s_5_0 s_6_0 150 300_es spirv)

bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS     ${SOL_SHADER_DIR}/vs_sky.sc
    VARYING_DEF ${SOL_SHADER_DIR}/varying_sky.def.sc
    OUTPUT_DIR  ${SOL_SHADER_OUT}
    OUT_FILES_VAR SOL_SKY_VS_OUT
    AS_HEADERS
    PROFILES ${SOL_SKY_PROFILES})

bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS     ${SOL_SHADER_DIR}/fs_sky.sc
    VARYING_DEF ${SOL_SHADER_DIR}/varying_sky.def.sc
    OUTPUT_DIR  ${SOL_SHADER_OUT}
    OUT_FILES_VAR SOL_SKY_FS_OUT
    AS_HEADERS
    PROFILES ${SOL_SKY_PROFILES})

# Fullscreen / bloom / post shaders (simple, profile 120 OK)
set(SOL_FS_PROFILES s_5_0 s_6_0 120 100_es spirv)

bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS     ${SOL_SHADER_DIR}/vs_fullscreen.sc
    VARYING_DEF ${SOL_SHADER_DIR}/varying_fullscreen.def.sc
    OUTPUT_DIR  ${SOL_SHADER_OUT}
    OUT_FILES_VAR SOL_FS_VS_OUT
    AS_HEADERS
    PROFILES ${SOL_FS_PROFILES})

bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS     ${SOL_SHADER_DIR}/fs_bloom_bright.sc
    VARYING_DEF ${SOL_SHADER_DIR}/varying_fullscreen.def.sc
    OUTPUT_DIR  ${SOL_SHADER_OUT}
    OUT_FILES_VAR SOL_BLOOM_BRIGHT_OUT
    AS_HEADERS
    PROFILES ${SOL_FS_PROFILES})

bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS     ${SOL_SHADER_DIR}/fs_bloom_blur.sc
    VARYING_DEF ${SOL_SHADER_DIR}/varying_fullscreen.def.sc
    OUTPUT_DIR  ${SOL_SHADER_OUT}
    OUT_FILES_VAR SOL_BLOOM_BLUR_OUT
    AS_HEADERS
    PROFILES ${SOL_FS_PROFILES})

bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS     ${SOL_SHADER_DIR}/fs_post.sc
    VARYING_DEF ${SOL_SHADER_DIR}/varying_fullscreen.def.sc
    OUTPUT_DIR  ${SOL_SHADER_OUT}
    OUT_FILES_VAR SOL_POST_OUT
    AS_HEADERS
    PROFILES ${SOL_FS_PROFILES})

target_sources(engine_core PRIVATE
    ${SOL_SHADOW_VS_OUT} ${SOL_SHADOW_FS_OUT}
    ${SOL_PBR_VS_OUT}    ${SOL_PBR_FS_OUT}
    ${SOL_SKY_VS_OUT}    ${SOL_SKY_FS_OUT}
    ${SOL_FS_VS_OUT}
    ${SOL_BLOOM_BRIGHT_OUT} ${SOL_BLOOM_BLUR_OUT} ${SOL_POST_OUT})
target_include_directories(engine_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

# ---- engine.exe: launcher/runtime that loads game DLLs ----
add_executable(engine src/main.cpp)
target_link_libraries(engine PRIVATE engine_core)
set_target_properties(engine PROPERTIES OUTPUT_NAME "sol")
