# ---- engine_core: shared library that games link against ----
add_library(engine_core SHARED
    src/engine.cpp
    src/platform/window.cpp
    src/render/renderer.cpp
    src/physics/physics.cpp
    src/asset/gltf_loader.cpp
    src/ui/imgui_layer.cpp
    src/log.cpp
)

target_include_directories(engine_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(engine_core
    PUBLIC
        glm::glm
        EnTT::EnTT
        imgui
    PRIVATE
        glfw
        bgfx
        bx
        bimg
        Jolt
        cgltf)

target_compile_definitions(engine_core
    PRIVATE SOL_ENGINE_BUILD=1
    PUBLIC  $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

set_target_properties(engine_core PROPERTIES
    OUTPUT_NAME "sol_engine"
    WINDOWS_EXPORT_ALL_SYMBOLS ON)

# ---- engine.exe: launcher/runtime that loads game DLLs ----
add_executable(engine src/main.cpp)
target_link_libraries(engine PRIVATE engine_core)
set_target_properties(engine PROPERTIES OUTPUT_NAME "sol")
