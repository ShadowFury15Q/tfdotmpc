cmake_minimum_required(VERSION 3.20)
project(DotmNativeRuntime LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
add_library(dotm_guest_memory STATIC guest_memory.cpp)
target_include_directories(dotm_guest_memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(dotm_memory_test test_guest_memory.cpp)
target_link_libraries(dotm_memory_test PRIVATE dotm_guest_memory)
# Page-rounded guest allocations for future Xbox kernel memory services.
# Public test exercises only synthetic guest addresses, not game assets.
add_library(dotm_guest_virtual_allocator STATIC guest_virtual_allocator.cpp)
target_link_libraries(dotm_guest_virtual_allocator PUBLIC dotm_guest_memory)
add_executable(dotm_guest_virtual_allocator_test test_guest_virtual_allocator.cpp)
target_link_libraries(dotm_guest_virtual_allocator_test PRIVATE dotm_guest_virtual_allocator)
# Restricted Xbox 360 NT virtual memory ABI: PPC r3-r7, big-endian pointers.
add_library(dotm_kernel_guest_memory STATIC kernel_guest_memory.cpp)
target_link_libraries(dotm_kernel_guest_memory PUBLIC dotm_guest_virtual_allocator)
add_executable(dotm_kernel_ppc_import_test
  test_kernel_ppc_import.cpp kernel_ppc_import.cpp)
target_include_directories(dotm_kernel_ppc_import_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test_fixtures)
target_link_libraries(dotm_kernel_ppc_import_test PRIVATE dotm_kernel_guest_memory)
add_test(NAME xbox360_nt_virtual_memory_ppc_abi COMMAND dotm_kernel_ppc_import_test)
# Real Xbox kernel thread-local slots, including per-thread value isolation.
find_package(Threads REQUIRED)
add_library(dotm_kernel_tls STATIC kernel_tls.cpp)
add_executable(dotm_kernel_tls_ppc_test test_kernel_tls.cpp kernel_tls_ppc_import.cpp)
target_include_directories(dotm_kernel_tls_ppc_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/test_fixtures/private_link
  ${CMAKE_CURRENT_SOURCE_DIR}/test_fixtures)
target_link_libraries(dotm_kernel_tls_ppc_test PRIVATE dotm_kernel_tls Threads::Threads)
add_test(NAME xbox360_kernel_tls_ppc_abi COMMAND dotm_kernel_tls_ppc_test)
# Original PPC startup uses RTL critical sections before game engine setup.
add_library(dotm_kernel_critical_sections STATIC kernel_critical_sections.cpp)
target_link_libraries(dotm_kernel_critical_sections PUBLIC dotm_guest_memory Threads::Threads)
add_executable(dotm_kernel_critical_sections_ppc_test
  test_kernel_critical_sections.cpp kernel_critical_sections_ppc.cpp)
target_include_directories(dotm_kernel_critical_sections_ppc_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/test_fixtures/private_link
  ${CMAKE_CURRENT_SOURCE_DIR}/test_fixtures)
target_link_libraries(dotm_kernel_critical_sections_ppc_test PRIVATE
  dotm_kernel_critical_sections)
add_test(NAME xbox360_rtl_critical_section_ppc_abi COMMAND dotm_kernel_critical_sections_ppc_test)

add_library(dotm_recomp_bridge STATIC recomp_bridge.cpp)
target_link_libraries(dotm_recomp_bridge PUBLIC dotm_guest_memory)
add_executable(dotm_recomp_bridge_test test_recomp_bridge.cpp)
target_link_libraries(dotm_recomp_bridge_test PRIVATE dotm_recomp_bridge)
add_test(NAME recompiled_ppc_dispatch_and_guest_store COMMAND dotm_recomp_bridge_test)
# Real Xbox 360 nonvolatile register/LR ABI, exercised without proprietary PPC.
add_executable(dotm_nonvolatile_gpr_test test_ppc_nonvolatile_gpr.cpp)
add_test(NAME xbox360_nonvolatile_gprlr_abi COMMAND dotm_nonvolatile_gpr_test)
add_executable(dotm_compiled_adapter_test
  test_compiled_ppc_adapter.cpp compiled_ppc_adapter.cpp)
target_include_directories(dotm_compiled_adapter_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test_fixtures)
target_link_libraries(dotm_compiled_adapter_test PRIVATE dotm_recomp_bridge)
add_test(NAME xenonrecomp_real_mapping_abi_smoke COMMAND dotm_compiled_adapter_test)

add_test(NAME guest_memory_and_native_dispatch COMMAND dotm_memory_test)
add_test(NAME guest_virtual_allocation_reuse_and_bounds COMMAND dotm_guest_virtual_allocator_test)

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

# Platform-independent native Windows keyboard/mouse action-state engine.
add_library(dotm_native_input STATIC native_input.cpp)
target_include_directories(dotm_native_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(dotm_native_input_test test_native_input.cpp)
target_link_libraries(dotm_native_input_test PRIVATE dotm_native_input)
add_test(NAME native_keyboard_mouse_actions COMMAND dotm_native_input_test)

if(WIN32)
  add_library(dotm_win32_input STATIC win32_input.cpp)
  target_link_libraries(dotm_win32_input PUBLIC dotm_native_input user32)
  target_link_libraries(dotm_dx11_mesh_viewer PRIVATE dotm_win32_input)
endif()

# Xbox 360 XAM input ABI backed by native Win32 keyboard/mouse actions.
# Public CI tests use no original game code or assets.
add_library(dotm_xam_input STATIC xam_input.cpp xam_guest_input.cpp)
target_include_directories(dotm_xam_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dotm_xam_input PUBLIC dotm_native_input dotm_guest_memory)

add_executable(dotm_xam_input_test test_xam_input.cpp)
target_link_libraries(dotm_xam_input_test PRIVATE dotm_xam_input)
add_test(NAME xbox360_be_gamepad_from_pc_actions COMMAND dotm_xam_input_test)

# Public CI uses synthetic PPCContext. A private game build must compile
# xam_ppc_import.cpp against the actual generated ppc_recomp_shared.h.
add_executable(dotm_xam_ppc_import_test
  test_xam_ppc_import.cpp xam_ppc_import.cpp)
target_include_directories(dotm_xam_ppc_import_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test_fixtures)
target_link_libraries(dotm_xam_ppc_import_test PRIVATE dotm_xam_input)
add_test(NAME xenonrecomp_xam_input_original_import_abi
  COMMAND dotm_xam_ppc_import_test)

# PRIVATE link-only validation of the original XenonRecomp output.
# This target is intentionally absent from public CI and never calls _xstart.
# Point DOTM_PRIVATE_PPC_DIR at the locally generated, uncommitted ppc/ directory.
set(DOTM_PRIVATE_PPC_DIR "" CACHE PATH "Private XenonRecomp C++ directory (never commit it)")
if(DOTM_PRIVATE_PPC_DIR)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  # The original XenonRecomp output uses Clang builtins and SIMDe x86 headers.
  # Keep these dependencies private: public synthetic CI needs neither.
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Private XenonRecomp output requires a Clang C++ compiler")
  endif()
  find_path(DOTM_SIMDE_INCLUDE_DIR NAMES x86/avx.h PATH_SUFFIXES simde)
  if(NOT DOTM_SIMDE_INCLUDE_DIR)
    message(FATAL_ERROR "Private PPC requires SIMDe headers: set DOTM_SIMDE_INCLUDE_DIR to the directory containing x86/avx.h")
  endif()
  foreach(required_file ppc_recomp_shared.h ppc_context.h ppc_func_mapping.cpp)
    if(NOT EXISTS "${DOTM_PRIVATE_PPC_DIR}/${required_file}")
      message(FATAL_ERROR "Private XenonRecomp file missing: ${required_file}")
    endif()
  endforeach()
  # The supplied original output contains 697 units (indices 0..696).
  # Compile the exact contiguous XenonRecomp set. A wildcard count can
  # accidentally accept duplicate, stale or missing numbered units.
  set(DOTM_PRIVATE_PPC_UNITS "")
  foreach(unit_index RANGE 0 696)
    set(unit_path "${DOTM_PRIVATE_PPC_DIR}/ppc_recomp.${unit_index}.cpp")
    if(NOT EXISTS "${unit_path}")
      message(FATAL_ERROR "Private PPC translation unit missing: ppc_recomp.${unit_index}.cpp")
    endif()
    list(APPEND DOTM_PRIVATE_PPC_UNITS "${unit_path}")
  endforeach()
  set(DOTM_PRIVATE_STUBS "${CMAKE_CURRENT_BINARY_DIR}/private_link_stubs.cpp")
  add_custom_command(
    OUTPUT "${DOTM_PRIVATE_STUBS}"
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/generate_private_link_stubs.py"
            "${DOTM_PRIVATE_PPC_DIR}" "${DOTM_PRIVATE_STUBS}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generate_private_link_stubs.py"
            "${DOTM_PRIVATE_PPC_DIR}/ppc_recomp_shared.h"
            "${DOTM_PRIVATE_PPC_DIR}/ppc_func_mapping.cpp"
            ${DOTM_PRIVATE_PPC_UNITS}
    VERBATIM)
  add_executable(dotm_private_link_probe EXCLUDE_FROM_ALL
    private_link_probe_main.cpp compiled_ppc_adapter.cpp xam_ppc_import.cpp kernel_ppc_import.cpp kernel_tls_ppc_import.cpp kernel_critical_sections_ppc.cpp
    private_ppc_gpr_import.cpp private_ppc_fpr_import.cpp
    "${DOTM_PRIVATE_PPC_DIR}/ppc_func_mapping.cpp"
    ${DOTM_PRIVATE_PPC_UNITS} "${DOTM_PRIVATE_STUBS}")
  target_include_directories(dotm_private_link_probe
    PRIVATE "${DOTM_PRIVATE_PPC_DIR}" "${DOTM_SIMDE_INCLUDE_DIR}")
  # Older SIMDe releases omit the SSE MXCSR rounding mask constant.
  # Force-include a public compatibility shim; never modify generated files.
  if(MSVC)
    target_compile_options(dotm_private_link_probe PRIVATE
      "/FI${CMAKE_CURRENT_SOURCE_DIR}/private_ppc_simde_compat.h")
  else()
    target_compile_options(dotm_private_link_probe PRIVATE
      -include "${CMAKE_CURRENT_SOURCE_DIR}/private_ppc_simde_compat.h")
  endif()
  # The original shared header declares ~178K functions. Reparse it once per
  # private build instead of 697 times; no proprietary data enters public CI.
  target_precompile_headers(dotm_private_link_probe PRIVATE
    "${DOTM_PRIVATE_PPC_DIR}/ppc_recomp_shared.h")
  target_link_libraries(dotm_private_link_probe
    PRIVATE dotm_recomp_bridge dotm_xam_input dotm_kernel_guest_memory dotm_kernel_tls dotm_kernel_critical_sections)
  if(MSVC)
    target_compile_options(dotm_private_link_probe PRIVATE /bigobj)
  endif()
endif()

# PUBLIC multi-object link smoke test. This uses a tiny synthetic PPC mapping
# and runs the SAME private-link stub generator as the actual private build.
# Unlike syntax-only checks, it must resolve symbols at the Windows linker.
# It never links or runs proprietary DOTM code.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(DOTM_SYNTHETIC_PPC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test_fixtures/private_link")
set(DOTM_SYNTHETIC_STUBS "${CMAKE_CURRENT_BINARY_DIR}/synthetic_private_link_stubs.cpp")
add_custom_command(
  OUTPUT "${DOTM_SYNTHETIC_STUBS}"
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/generate_private_link_stubs.py"
          "${DOTM_SYNTHETIC_PPC_DIR}" "${DOTM_SYNTHETIC_STUBS}"
          --minimum-units 1
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generate_private_link_stubs.py"
          "${DOTM_SYNTHETIC_PPC_DIR}/ppc_recomp_shared.h"
          "${DOTM_SYNTHETIC_PPC_DIR}/ppc_func_mapping.cpp"
          "${DOTM_SYNTHETIC_PPC_DIR}/ppc_recomp.0.cpp"
  VERBATIM)
add_executable(dotm_synthetic_private_link_test
  test_synthetic_private_link.cpp
  compiled_ppc_adapter.cpp
  xam_ppc_import.cpp kernel_ppc_import.cpp kernel_tls_ppc_import.cpp kernel_critical_sections_ppc.cpp
  private_ppc_gpr_import.cpp private_ppc_fpr_import.cpp
  "${DOTM_SYNTHETIC_PPC_DIR}/ppc_func_mapping.cpp"
  "${DOTM_SYNTHETIC_PPC_DIR}/ppc_recomp.0.cpp"
  "${DOTM_SYNTHETIC_STUBS}")
target_include_directories(dotm_synthetic_private_link_test PRIVATE
  "${DOTM_SYNTHETIC_PPC_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_fixtures")
target_link_libraries(dotm_synthetic_private_link_test
  PRIVATE dotm_recomp_bridge dotm_xam_input dotm_guest_virtual_allocator dotm_kernel_guest_memory dotm_kernel_tls dotm_kernel_critical_sections)
# Run endian-precise Xbox 360 FPR ABI tests without proprietary PPC units.
add_executable(dotm_ppc_nonvolatile_fpr_test test_ppc_nonvolatile_fpr.cpp)
add_test(NAME xbox360_original_ppc_fpr_save_restore COMMAND dotm_ppc_nonvolatile_fpr_test)

add_test(NAME synthetic_full_ppc_link_and_xam_import
  COMMAND dotm_synthetic_private_link_test)

# Exercise the same private boot probe against synthetic PPC units. This
# tests stack setup and host bindings without distributing or running game code.
add_executable(dotm_synthetic_private_boot_probe
  private_link_probe_main.cpp
  compiled_ppc_adapter.cpp
  xam_ppc_import.cpp kernel_ppc_import.cpp kernel_tls_ppc_import.cpp kernel_critical_sections_ppc.cpp
  private_ppc_gpr_import.cpp private_ppc_fpr_import.cpp
  "${DOTM_SYNTHETIC_PPC_DIR}/ppc_func_mapping.cpp"
  "${DOTM_SYNTHETIC_PPC_DIR}/ppc_recomp.0.cpp"
  "${DOTM_SYNTHETIC_STUBS}")
target_compile_definitions(dotm_synthetic_private_boot_probe
  PRIVATE DOTM_SYNTHETIC_PRIVATE_BOOT=1)
target_include_directories(dotm_synthetic_private_boot_probe PRIVATE
  "${DOTM_SYNTHETIC_PPC_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_fixtures")
target_link_libraries(dotm_synthetic_private_boot_probe PRIVATE
  dotm_recomp_bridge dotm_xam_input dotm_guest_virtual_allocator
  dotm_kernel_guest_memory dotm_kernel_tls dotm_kernel_critical_sections)
add_test(NAME synthetic_private_boot_link_probe
  COMMAND dotm_synthetic_private_boot_probe)
add_test(NAME synthetic_private_boot_executes_entry
  COMMAND dotm_synthetic_private_boot_probe --synthetic-entry)
