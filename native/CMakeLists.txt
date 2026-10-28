cmake_minimum_required(VERSION 3.20)
project(DotmNativeRuntime LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(dotm_guest_memory STATIC guest_memory.cpp)
target_include_directories(dotm_guest_memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(dotm_memory_test test_guest_memory.cpp)
target_link_libraries(dotm_memory_test PRIVATE dotm_guest_memory)
enable_testing()
add_test(NAME guest_memory_and_native_dispatch COMMAND dotm_memory_test)

add_executable(dotm_pe_probe boot_probe.cpp)
target_link_libraries(dotm_pe_probe PRIVATE dotm_guest_memory)

include(FetchContent)
FetchContent_Declare(lzokay
    GIT_REPOSITORY https://github.com/AxioDL/lzokay.git
    GIT_TAG db2df1fcbebc2ed06c10f727f72567d40f06a2be
    GIT_SHALLOW FALSE
    SOURCE_SUBDIR __dotm_skip_upstream_build)
FetchContent_MakeAvailable(lzokay)
add_library(lzokay STATIC "${lzokay_SOURCE_DIR}/lzokay.cpp")
target_include_directories(lzokay PUBLIC "${lzokay_SOURCE_DIR}")
add_library(dotm_ue3_package STATIC ue3_package.cpp ue3_properties.cpp ue3_texture.cpp ue3_mip.cpp ue3_dds.cpp ue3_mesh.cpp ue3_lod.cpp)
target_include_directories(dotm_ue3_package PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dotm_ue3_package PRIVATE lzokay)
add_executable(dotm_ue3_package_test test_ue3_package.cpp)
target_link_libraries(dotm_ue3_package_test PRIVATE dotm_ue3_package lzokay)
add_test(NAME ue3_be_lzo_and_imports COMMAND dotm_ue3_package_test)
add_executable(dotm_ue3_probe ue3_package_probe.cpp)
target_link_libraries(dotm_ue3_probe PRIVATE dotm_ue3_package)

add_executable(dotm_ue3_properties_test test_ue3_properties.cpp)
target_link_libraries(dotm_ue3_properties_test PRIVATE dotm_ue3_package lzokay)
add_test(NAME ue3_tagged_property_stream COMMAND dotm_ue3_properties_test)

add_executable(dotm_dds_export dds_export_main.cpp)
target_link_libraries(dotm_dds_export PRIVATE dotm_ue3_package)
add_executable(dotm_ue3_dds_test test_ue3_dds.cpp)
target_link_libraries(dotm_ue3_dds_test PRIVATE dotm_ue3_package)
add_test(NAME ue3_xenos_bc_to_dds COMMAND dotm_ue3_dds_test)

add_executable(dotm_mesh_probe mesh_probe.cpp)
target_link_libraries(dotm_mesh_probe PRIVATE dotm_ue3_package)
add_executable(dotm_ue3_mesh_test test_ue3_mesh.cpp)
target_link_libraries(dotm_ue3_mesh_test PRIVATE dotm_ue3_package)
add_test(NAME ue3_kdop_collision_and_skeletal_bounds COMMAND dotm_ue3_mesh_test)

add_executable(dotm_static_obj_export static_obj_export_main.cpp)
target_link_libraries(dotm_static_obj_export PRIVATE dotm_ue3_package)

if(WIN32)
  add_executable(dotm_dx11_mesh_viewer WIN32 dx11_mesh_viewer.cpp)
  target_link_libraries(dotm_dx11_mesh_viewer PRIVATE dotm_ue3_package d3d11 d3dcompiler dxgi user32 shell32)
endif()
