add_library(voxel_demo SHARED voxel.cpp)
target_link_libraries(voxel_demo PRIVATE engine_core)
set_target_properties(voxel_demo PROPERTIES OUTPUT_NAME "voxel_demo" PREFIX "")
