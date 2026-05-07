add_library(demo SHARED src/demo.cpp)
target_link_libraries(demo PRIVATE engine_core glfw)
set_target_properties(demo PROPERTIES OUTPUT_NAME "demo" PREFIX "")
