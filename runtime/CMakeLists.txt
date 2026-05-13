add_executable(sol_runtime src/main.cpp)
target_link_libraries(sol_runtime PRIVATE engine_core)
set_target_properties(sol_runtime PROPERTIES OUTPUT_NAME "sol")
