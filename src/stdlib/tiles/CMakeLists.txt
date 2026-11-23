include(${PROJECT_SOURCE_DIR}/cmake/camel_module.cmake)
add_camel_module(
  NAME tiles
  SOURCES src/tree_transfer.cpp src/resident_storage.cpp src/module.cpp src/graph_values.cpp src/graph_regions.cpp src/placement.cpp src/residency.cpp src/region_extract.cpp src/region_outline.cpp src/region_test_pass.cpp src/region_pass.cpp src/compiler.cpp src/compiled_scope.cpp src/row_kernel.cpp src/row_body.cpp src/codegen.cpp src/codegen_cache.cpp src/scalar_expr.cpp src/index_expr.cpp src/access.cpp src/kernel_ir.cpp src/serial_execute.cpp src/matmul_kernel.cpp src/serial_kernel.cpp src/pointwise_kernel.cpp src/reduction_kernel.cpp src/regions.cpp src/scalar_kernel.cpp src/loop_kernel.cpp src/parallel_kernel.cpp src/cuda.cpp src/mixed_gemm.cpp src/cuda_backend.cpp src/cuda_pool.cpp src/capability.cpp src/cuda_resources.cpp src/cuda_submission.cpp src/schedule.cpp src/device.cpp src/census.cpp
  EXTRA_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include
  LINK_MODULES tensor
  EXTRA_LIBS nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS}
)

# Native contract tests build with the standard build, without needing a CUDA device.
#
# The sources are an OBJECT library and the executable links from it, so the check target's
# undefined symbols are visible to the libcamel export-list generation before libcamel links:
# tiles-plan-check is the target that imported OperatorTable::key and caught the gap.
# kernel_ir_check.cpp exercises the boundary decision itself, so that decision and the placement
# query it reads are compiled into this target; the rest of what they need comes from libcamel.
add_library(tiles-plan-check_objects OBJECT src/resident_storage.cpp src/residency.cpp src/placement.cpp ../../../test/cases/modules/tiles/plan_check.cpp ../../../test/cases/modules/tiles/kernel_ir_check.cpp ../../../test/cases/modules/tiles/reduction_domain_check.cpp ../../../test/cases/modules/tiles/core_views_check.cpp src/row_kernel.cpp src/row_body.cpp src/codegen.cpp src/codegen_cache.cpp src/schedule.cpp src/scalar_expr.cpp src/index_expr.cpp src/access.cpp src/kernel_ir.cpp src/serial_execute.cpp src/matmul_kernel.cpp src/serial_kernel.cpp src/pointwise_kernel.cpp src/reduction_kernel.cpp src/regions.cpp src/scalar_kernel.cpp src/loop_kernel.cpp src/parallel_kernel.cpp)
target_link_libraries(tiles-plan-check_objects PRIVATE nlohmann_json::nlohmann_json)
add_executable(tiles-plan-check $<TARGET_OBJECTS:tiles-plan-check_objects>)
camel_register_export_scan_target(tiles-plan-check_objects)
target_compile_features(tiles-plan-check_objects PRIVATE cxx_std_23)
target_include_directories(tiles-plan-check_objects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CAMEL_LAYOUT_TRANSITIONAL_ROOTS} ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(tiles-plan-check PRIVATE libcamel fmt::fmt)

# The check executables the plans run, written down by the build itself.
#
# A test that runs one of these used to spell its directory: `build/modules/tiles/<config>` while
# the module lived in `modules/`, `build/lib/tiles/<config>` while it lived in `lib/`. A target's
# binary directory follows its source directory, so the layout move to `src/stdlib/tiles/` moved the
# executables with it and left the three launchers reading a directory nothing writes any more. The
# entries that run them then failed on "no such file" -- a red that reads like a check refusing to
# hold and is not one: the check body never started, so it was never green and never red either.
#
# `$<TARGET_FILE:...>` is the generator's own answer to "where is this target", so the launchers ask
# the install for the file (scripts/collect-out.js copies every path listed here into
# `out/<tag>/bin/`) instead of spelling a build directory, and the next move costs them nothing.
set(_camel_test_executables "$<TARGET_FILE:tiles-plan-check>\n")
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/camel-test-executables-tiles-$<CONFIG>.txt"
    CONTENT "${_camel_test_executables}")

# The module and its source inspection API also build without the CUDA SDK.
# Load the driver and NVRTC lazily: importing tiles never requires a GPU.
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
  add_library(tiles-async-check_objects OBJECT ../../../test/cases/modules/tiles/async_submission_check.cpp ../../../test/cases/modules/tiles/async_submission_lifetime.cpp ../../../test/cases/modules/tiles/memory_pool_check.cpp ../../../test/cases/modules/tiles/memory_pool_failures.cpp src/resident_storage.cpp src/cuda.cpp src/mixed_gemm.cpp src/cuda_backend.cpp src/cuda_pool.cpp src/cuda_resources.cpp src/cuda_submission.cpp src/capability.cpp src/row_kernel.cpp src/row_body.cpp src/codegen.cpp src/codegen_cache.cpp src/schedule.cpp src/scalar_expr.cpp src/index_expr.cpp src/access.cpp src/kernel_ir.cpp src/matmul_kernel.cpp src/serial_kernel.cpp src/pointwise_kernel.cpp src/reduction_kernel.cpp src/regions.cpp src/scalar_kernel.cpp src/loop_kernel.cpp src/parallel_kernel.cpp)
  add_executable(tiles-async-check $<TARGET_OBJECTS:tiles-async-check_objects>)
  camel_register_export_scan_target(tiles-async-check_objects)
  target_compile_features(tiles-async-check_objects PRIVATE cxx_std_23)
  target_include_directories(tiles-async-check_objects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CAMEL_LAYOUT_TRANSITIONAL_ROOTS} ${CMAKE_SOURCE_DIR}/third_party)
  target_link_libraries(tiles-async-check_objects PRIVATE nlohmann_json::nlohmann_json)
  target_link_libraries(tiles-async-check PRIVATE libcamel fmt::fmt nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})
  # The lease test builds the module's device code without libcamel; `--devices` is read from
  # the core's selection, so that one translation unit comes along (it depends on nothing else).
  # kernel expression construction names index expressions, so index_expr.cpp is one of the files
  # this target cannot leave out.
  add_executable(tiles-module-lease ../../../test/cases/modules/tiles/module_lease.cpp ../../../test/cases/modules/tiles/scalar_device.cpp src/scalar_expr.cpp src/index_expr.cpp src/cuda_backend.cpp src/cuda_pool.cpp src/capability.cpp src/cuda_resources.cpp ${CMAKE_SOURCE_DIR}/src/stdlib/std/exec/devices.cpp)
  target_compile_features(tiles-module-lease PRIVATE cxx_std_23)
  target_include_directories(tiles-module-lease PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CAMEL_LAYOUT_TRANSITIONAL_ROOTS})
  target_link_libraries(tiles-module-lease PRIVATE ${CMAKE_DL_LIBS})
  foreach(target tiles_cmo_objects tiles-module-lease tiles-async-check_objects)
  target_include_directories(${target} PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
  target_compile_definitions(${target} PRIVATE
    CAMEL_TILES_CUDA=1
    CAMEL_TILES_CUDA_BIN="${CUDAToolkit_BIN_DIR}"
    CAMEL_TILES_CUDA_LIB="${CUDAToolkit_LIBRARY_DIR}")
  endforeach()
  # The two checks above are built only where the toolkit is: their list is generated here, so a
  # build without CUDA answers with one manifest and not with a file missing from a plan.
  set(_camel_cuda_test_executables "")
  foreach(_camel_test_target tiles-async-check tiles-module-lease)
      string(APPEND _camel_cuda_test_executables "$<TARGET_FILE:${_camel_test_target}>\n")
  endforeach()
  file(GENERATE
      OUTPUT "${CMAKE_BINARY_DIR}/camel-test-executables-tiles-cuda-$<CONFIG>.txt"
      CONTENT "${_camel_cuda_test_executables}")
  message(STATUS "tiles: CUDA ${CUDAToolkit_VERSION} headers; driver/NVRTC loaded at runtime")
else()
  message(STATUS "tiles: source inspection only (CUDA Toolkit not found)")
endif()
