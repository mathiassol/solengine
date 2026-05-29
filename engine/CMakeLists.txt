# ---- engine_core: shared library (DLL mode) or static library (monolithic mode) ----
if(SOL_MONOLITHIC)
    add_library(engine_core STATIC
        src/engine.cpp
        src/host.cpp
        src/platform/window.cpp
        src/render/mesh.cpp
        src/render/texture.cpp
        src/render/vk/vk_context.cpp
        src/render/vk/vk_swapchain.cpp
        src/render/vk/vk_buffer.cpp
        src/render/vk/vk_image.cpp
        src/render/vk/vk_pipeline.cpp
        src/render/vk/vk_desc.cpp
        src/render/vk/vk_renderer.cpp
        src/physics/physics.cpp
        src/asset/gltf_loader.cpp
        src/asset/model_loader.cpp
        src/asset/fbx_loader.cpp
        src/asset/blend_loader.cpp
        src/asset/stb_impl.cpp
        src/ui/imgui_layer.cpp
        src/ui/ImGuizmo.cpp
        src/log.cpp
        src/scene/scene.cpp
        src/scene/node_factory.cpp
        src/scene/node_handle.cpp
        src/scene/physics_nodes.cpp
        src/scene/scene_manager.cpp
        src/scene/fly_cam_controller.cpp
        src/scene/actor.cpp
        src/scene/lua_component.cpp
        src/script/script_engine.cpp
        src/project/project.cpp
        src/input/input_manager.cpp
        src/ecs_world.cpp
        src/reflect.cpp
        src/world/world_partition.cpp
        src/perf/profiler.cpp
        src/audio/audio_engine.cpp
        src/scene/audio_nodes.cpp)
else()
    add_library(engine_core SHARED
        src/engine.cpp
        src/host.cpp
        src/platform/window.cpp
        src/render/mesh.cpp
        src/render/texture.cpp
        src/render/vk/vk_context.cpp
        src/render/vk/vk_swapchain.cpp
        src/render/vk/vk_buffer.cpp
        src/render/vk/vk_image.cpp
        src/render/vk/vk_pipeline.cpp
        src/render/vk/vk_desc.cpp
        src/render/vk/vk_renderer.cpp
        src/physics/physics.cpp
        src/asset/gltf_loader.cpp
        src/asset/model_loader.cpp
        src/asset/fbx_loader.cpp
        src/asset/blend_loader.cpp
        src/asset/stb_impl.cpp
        src/ui/imgui_layer.cpp
        src/ui/ImGuizmo.cpp
        src/log.cpp
        src/scene/scene.cpp
        src/scene/node_factory.cpp
        src/scene/node_handle.cpp
        src/scene/physics_nodes.cpp
        src/scene/scene_manager.cpp
        src/scene/fly_cam_controller.cpp
        src/scene/actor.cpp
        src/scene/lua_component.cpp
        src/script/script_engine.cpp
        src/project/project.cpp
        src/input/input_manager.cpp
        src/ecs_world.cpp
        src/reflect.cpp
        src/world/world_partition.cpp
        src/perf/profiler.cpp
        src/audio/audio_engine.cpp
        src/scene/audio_nodes.cpp)
endif()

target_include_directories(engine_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_BINARY_DIR}/generated
            ${luabridge3_SOURCE_DIR}/Source)  # for embedded SPIR-V headers

target_link_libraries(engine_core
    PUBLIC
        glm::glm
        EnTT::EnTT
        imgui
    PRIVATE
        glfw
        volk
        vma_iface
        Jolt
        cgltf
        nlohmann_json::nlohmann_json
        stb_iface
        ufbx_iface
        lua54
        miniaudio_iface)

# Platform-specific Vulkan surface extension
if(WIN32)
    target_compile_definitions(engine_core PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    target_compile_definitions(engine_core PRIVATE VK_USE_PLATFORM_METAL_EXT)
elseif(UNIX)
    target_compile_definitions(engine_core PRIVATE VK_USE_PLATFORM_XLIB_KHR)
endif()
target_compile_definitions(engine_core PRIVATE VK_NO_PROTOTYPES)

# Fix parallel PDB write contention on MSVC
if(MSVC)
    # Replace /Zi (shared PDB, causes C1041 in parallel builds) with /Z7 (embed debug info in .obj)
    foreach(flag_var CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        string(REPLACE "/Zi" "/Z7" ${flag_var} "${${flag_var}}")
        string(REPLACE "/ZI" "/Z7" ${flag_var} "${${flag_var}}")
    endforeach()
    target_compile_options(engine_core PRIVATE /FS)
endif()

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

# ---- SPIR-V shader compilation via glslang_validator -------------------------
# Locate glslang_validator built by FetchContent
set(GLSLANG_VALIDATOR "$<TARGET_FILE:glslang-standalone>")

set(SOL_SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders)
set(SOL_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders)
file(MAKE_DIRECTORY ${SOL_SHADER_OUT})

function(sol_compile_shader STAGE GLSL_FILE OUT_HEADER_VAR)
    get_filename_component(FNAME ${GLSL_FILE} NAME)
    string(REPLACE "." "_" VARNAME "${FNAME}")
    set(SPV_FILE  "${SOL_SHADER_OUT}/${FNAME}.spv")
    set(HDR_FILE  "${SOL_SHADER_OUT}/${FNAME}.h")

    # Map stage name to glslang flag
    if(STAGE STREQUAL "vert")
        set(STAGE_FLAG "-S vert")
    elseif(STAGE STREQUAL "frag")
        set(STAGE_FLAG "-S frag")
    else()
        set(STAGE_FLAG "-S comp")
    endif()

    # Compile GLSL → SPIR-V
    add_custom_command(
        OUTPUT  ${SPV_FILE}
        COMMAND ${GLSLANG_VALIDATOR}
                -V --target-env vulkan1.2
                -S ${STAGE}
                -o ${SPV_FILE}
                ${GLSL_FILE}
        DEPENDS ${GLSL_FILE} glslang-standalone
        COMMENT "Compiling ${FNAME} → SPIR-V"
        VERBATIM)

    # Convert SPIR-V binary → C header (uint32_t[])
    add_custom_command(
        OUTPUT  ${HDR_FILE}
        COMMAND ${CMAKE_COMMAND}
                -DSPV_FILE=${SPV_FILE}
                -DHDR_FILE=${HDR_FILE}
                -DVARNAME=${VARNAME}
                -P ${CMAKE_SOURCE_DIR}/cmake/SpvToHeader.cmake
        DEPENDS ${SPV_FILE}
        COMMENT "Embedding ${FNAME}.spv as C header"
        VERBATIM)

    set(${OUT_HEADER_VAR} ${HDR_FILE} PARENT_SCOPE)
endfunction()

sol_compile_shader(vert ${SOL_SHADER_SRC}/shadow.vert.glsl       SOL_H_shad_vert)
sol_compile_shader(frag ${SOL_SHADER_SRC}/shadow.frag.glsl       SOL_H_shad_frag)
sol_compile_shader(frag ${SOL_SHADER_SRC}/shadow_vsm.frag.glsl  SOL_H_shad_vsm_frag)
sol_compile_shader(vert ${SOL_SHADER_SRC}/sky.vert.glsl          SOL_H_sky_vert)
sol_compile_shader(frag ${SOL_SHADER_SRC}/sky.frag.glsl          SOL_H_sky_frag)
sol_compile_shader(vert ${SOL_SHADER_SRC}/fullscreen.vert.glsl   SOL_H_fs_vert)
sol_compile_shader(frag ${SOL_SHADER_SRC}/bloom_bright.frag.glsl SOL_H_bloom_b)
sol_compile_shader(frag ${SOL_SHADER_SRC}/bloom_blur.frag.glsl   SOL_H_bloom_blur)
sol_compile_shader(frag ${SOL_SHADER_SRC}/tonemap.frag.glsl      SOL_H_tonemap)
sol_compile_shader(frag ${SOL_SHADER_SRC}/ssao.frag.glsl          SOL_H_ssao_frag)
sol_compile_shader(frag ${SOL_SHADER_SRC}/ssao_blur.frag.glsl     SOL_H_ssao_blur_frag)
sol_compile_shader(frag ${SOL_SHADER_SRC}/ibl_sky_capture.frag.glsl  SOL_H_ibl_sky)
sol_compile_shader(frag ${SOL_SHADER_SRC}/ibl_irradiance.frag.glsl   SOL_H_ibl_irr)
sol_compile_shader(frag ${SOL_SHADER_SRC}/ibl_prefilter.frag.glsl    SOL_H_ibl_pref)
sol_compile_shader(frag ${SOL_SHADER_SRC}/ibl_brdf_lut.frag.glsl     SOL_H_ibl_brdf)
sol_compile_shader(frag ${SOL_SHADER_SRC}/equirect_to_cube.frag.glsl SOL_H_equirect)
sol_compile_shader(frag ${SOL_SHADER_SRC}/taa.frag.glsl              SOL_H_taa_frag)
sol_compile_shader(frag ${SOL_SHADER_SRC}/ssr.frag.glsl          SOL_H_ssr_frag)
sol_compile_shader(frag ${SOL_SHADER_SRC}/ssr_temporal.frag.glsl SOL_H_ssr_temporal_frag)
sol_compile_shader(vert ${SOL_SHADER_SRC}/depth_pre.vert.glsl       SOL_H_depth_pre_vert)
sol_compile_shader(frag ${SOL_SHADER_SRC}/depth_pre.frag.glsl       SOL_H_depth_pre_frag)
sol_compile_shader(vert ${SOL_SHADER_SRC}/forward_plus.vert.glsl    SOL_H_fwd_plus_vert)
sol_compile_shader(frag ${SOL_SHADER_SRC}/forward_plus.frag.glsl    SOL_H_fwd_plus_frag)

target_sources(engine_core PRIVATE
    ${SOL_H_shad_vert} ${SOL_H_shad_frag} ${SOL_H_shad_vsm_frag}
    ${SOL_H_sky_vert}  ${SOL_H_sky_frag}
    ${SOL_H_fs_vert}
    ${SOL_H_bloom_b}   ${SOL_H_bloom_blur}
    ${SOL_H_tonemap}
    ${SOL_H_ssao_frag} ${SOL_H_ssao_blur_frag}
    ${SOL_H_ibl_sky}   ${SOL_H_ibl_irr}
    ${SOL_H_ibl_pref}  ${SOL_H_ibl_brdf}  ${SOL_H_equirect}
    ${SOL_H_taa_frag}
    ${SOL_H_ssr_frag}        ${SOL_H_ssr_temporal_frag}
    ${SOL_H_depth_pre_vert} ${SOL_H_depth_pre_frag}
    ${SOL_H_fwd_plus_vert}  ${SOL_H_fwd_plus_frag})
