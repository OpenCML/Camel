include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME tiles
  SOURCES src/tree_transfer.cpp src/resident_storage.cpp src/module.cpp src/graph_values.cpp src/graph_regions.cpp src/placement.cpp src/residency.cpp src/region_extract.cpp src/region_outline.cpp src/region_test_pass.cpp src/region_pass.cpp src/compiler.cpp src/compiled_scope.cpp src/row_kernel.cpp src/row_body.cpp src/codegen.cpp src/codegen_cache.cpp src/scalar_expr.cpp src/index_expr.cpp src/access.cpp src/kernel_ir.cpp src/serial_execute.cpp src/matmul_kernel.cpp src/serial_kernel.cpp src/pointwise_kernel.cpp src/reduction_kernel.cpp src/regions.cpp src/scalar_kernel.cpp src/loop_kernel.cpp src/parallel_kernel.cpp src/cuda.cpp src/mixed_gemm.cpp src/cuda_backend.cpp src/cuda_pool.cpp src/capability.cpp src/cuda_resources.cpp src/cuda_submission.cpp src/schedule.cpp src/device.cpp src/census.cpp
  EXTRA_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include
  EXTRA_LIBS tensor_cmo nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS}
)

# Native contract tests build with the standard build, without needing a CUDA device.
#
# The sources are an OBJECT library and the executable links from it, so the check target's
# undefined symbols are visible to the libcamel export-list generation before libcamel links:
# tiles-plan-check is the target that imported OperatorTable::key and caught the gap.
# kernel_ir_check.cpp exercises the boundary decision itself, so that decision and the placement
# query it reads are compiled into this target; the rest of what they need comes from libcamel.
add_library(tiles-plan-check_objects OBJECT src/resident_storage.cpp src/residency.cpp src/placement.cpp ../../test/cases/modules/tiles/plan_check.cpp ../../test/cases/modules/tiles/kernel_ir_check.cpp ../../test/cases/modules/tiles/reduction_domain_check.cpp ../../test/cases/modules/tiles/core_views_check.cpp src/row_kernel.cpp src/row_body.cpp src/codegen.cpp src/codegen_cache.cpp src/schedule.cpp src/scalar_expr.cpp src/index_expr.cpp src/access.cpp src/kernel_ir.cpp src/serial_execute.cpp src/matmul_kernel.cpp src/serial_kernel.cpp src/pointwise_kernel.cpp src/reduction_kernel.cpp src/regions.cpp src/scalar_kernel.cpp src/loop_kernel.cpp src/parallel_kernel.cpp)
target_link_libraries(tiles-plan-check_objects PRIVATE nlohmann_json::nlohmann_json)
add_executable(tiles-plan-check $<TARGET_OBJECTS:tiles-plan-check_objects>)
camel_register_export_scan_target(tiles-plan-check_objects)
target_compile_features(tiles-plan-check_objects PRIVATE cxx_std_23)
target_include_directories(tiles-plan-check_objects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(tiles-plan-check PRIVATE libcamel fmt::fmt)

# The module and its source inspection API also build without the CUDA SDK.
# Load the driver and NVRTC lazily: importing tiles never requires a GPU.
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
  add_library(tiles-async-check_objects OBJECT ../../test/cases/modules/tiles/async_submission_check.cpp ../../test/cases/modules/tiles/async_submission_lifetime.cpp ../../test/cases/modules/tiles/memory_pool_check.cpp ../../test/cases/modules/tiles/memory_pool_failures.cpp src/resident_storage.cpp src/cuda.cpp src/mixed_gemm.cpp src/cuda_backend.cpp src/cuda_pool.cpp src/cuda_resources.cpp src/cuda_submission.cpp src/capability.cpp src/row_kernel.cpp src/row_body.cpp src/codegen.cpp src/codegen_cache.cpp src/schedule.cpp src/scalar_expr.cpp src/index_expr.cpp src/access.cpp src/kernel_ir.cpp src/matmul_kernel.cpp src/serial_kernel.cpp src/pointwise_kernel.cpp src/reduction_kernel.cpp src/regions.cpp src/scalar_kernel.cpp src/loop_kernel.cpp src/parallel_kernel.cpp)
  add_executable(tiles-async-check $<TARGET_OBJECTS:tiles-async-check_objects>)
  camel_register_export_scan_target(tiles-async-check_objects)
  target_compile_features(tiles-async-check_objects PRIVATE cxx_std_23)
  target_include_directories(tiles-async-check_objects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party)
  target_link_libraries(tiles-async-check_objects PRIVATE nlohmann_json::nlohmann_json)
  target_link_libraries(tiles-async-check PRIVATE libcamel fmt::fmt nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})
  # The lease test builds the module's device code without libcamel; `--devices` is read from
  # the core's selection, so that one translation unit comes along (it depends on nothing else).
  # kernel expression construction names index expressions, so index_expr.cpp is one of the files
  # this target cannot leave out.
  add_executable(tiles-module-lease ../../test/cases/modules/tiles/module_lease.cpp ../../test/cases/modules/tiles/scalar_device.cpp src/scalar_expr.cpp src/index_expr.cpp src/cuda_backend.cpp src/cuda_pool.cpp src/capability.cpp src/cuda_resources.cpp ${CMAKE_SOURCE_DIR}/src/execute/devices.cpp)
  target_compile_features(tiles-module-lease PRIVATE cxx_std_23)
  target_include_directories(tiles-module-lease PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include)
  target_link_libraries(tiles-module-lease PRIVATE ${CMAKE_DL_LIBS})
  foreach(target tiles_cmo_objects tiles-module-lease tiles-async-check_objects)
  target_include_directories(${target} PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
  target_compile_definitions(${target} PRIVATE
    CAMEL_TILES_CUDA=1
    CAMEL_TILES_CUDA_BIN="${CUDAToolkit_BIN_DIR}"
    CAMEL_TILES_CUDA_LIB="${CUDAToolkit_LIBRARY_DIR}")
  endforeach()
  message(STATUS "tiles: CUDA ${CUDAToolkit_VERSION} headers; driver/NVRTC loaded at runtime")
else()
  message(STATUS "tiles: source inspection only (CUDA Toolkit not found)")
endif()
