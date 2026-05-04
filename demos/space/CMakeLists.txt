add_library(space_demo SHARED space.cpp)
target_link_libraries(space_demo PRIVATE engine_core)
set_target_properties(space_demo PROPERTIES OUTPUT_NAME "space_demo" PREFIX "")
