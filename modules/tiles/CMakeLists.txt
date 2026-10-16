include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME tiles
  SOURCES src/module.cpp src/compiler.cpp src/row_kernel.cpp src/row_body.cpp src/codegen.cpp src/codegen_cache.cpp src/scalar_expr.cpp src/index_expr.cpp src/access.cpp src/kernel_ir.cpp src/matmul_kernel.cpp src/serial_kernel.cpp src/pointwise_kernel.cpp src/reduction_kernel.cpp src/scalar_kernel.cpp src/parallel_kernel.cpp src/cuda.cpp src/cuda_backend.cpp src/cuda_resources.cpp src/schedule.cpp src/device.cpp
  EXTRA_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include
  EXTRA_LIBS tensor_cmo nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS}
)

# Native contract tests build with the standard build, without needing a CUDA device.
add_executable(tiles-plan-check ../../test/cases/modules/tiles/plan_check.cpp ../../test/cases/modules/tiles/kernel_ir_check.cpp src/row_kernel.cpp src/row_body.cpp src/codegen.cpp src/codegen_cache.cpp src/schedule.cpp src/scalar_expr.cpp src/index_expr.cpp src/access.cpp src/kernel_ir.cpp src/matmul_kernel.cpp src/serial_kernel.cpp src/pointwise_kernel.cpp src/reduction_kernel.cpp src/scalar_kernel.cpp src/parallel_kernel.cpp)
target_compile_features(tiles-plan-check PRIVATE cxx_std_23)
target_include_directories(tiles-plan-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(tiles-plan-check PRIVATE libcamel fmt::fmt)

# The module and its source inspection API also build without the CUDA SDK.
# Load the driver and NVRTC lazily: importing tiles never requires a GPU.
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
  add_executable(tiles-module-lease ../../test/cases/modules/tiles/module_lease.cpp ../../test/cases/modules/tiles/scalar_device.cpp src/scalar_expr.cpp src/cuda_backend.cpp src/cuda_resources.cpp)
  target_compile_features(tiles-module-lease PRIVATE cxx_std_23)
  target_include_directories(tiles-module-lease PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(tiles-module-lease PRIVATE ${CMAKE_DL_LIBS})
  foreach(target tiles_cmo tiles-module-lease)
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
