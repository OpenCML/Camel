include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME tiles
  SOURCES src/module.cpp src/compiler.cpp src/codegen.cpp src/cuda.cpp src/schedule.cpp
  EXTRA_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include
  EXTRA_LIBS tensor_cmo ${CMAKE_DL_LIBS}
)

# Native contract tests build with the standard build, without needing a CUDA device.
add_executable(tiles-plan-check ../../test/cases/modules/tiles/plan_check.cpp src/schedule.cpp)
target_compile_features(tiles-plan-check PRIVATE cxx_std_23)
target_include_directories(tiles-plan-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(tiles-plan-check PRIVATE libcamel fmt::fmt)

# The module and its source inspection API also build without the CUDA SDK.
# Load the driver and NVRTC lazily: importing tiles never requires a GPU.
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
  target_include_directories(tiles_cmo PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
  target_compile_definitions(tiles_cmo PRIVATE
    CAMEL_TILES_CUDA=1
    CAMEL_TILES_CUDA_BIN="${CUDAToolkit_BIN_DIR}"
    CAMEL_TILES_CUDA_LIB="${CUDAToolkit_LIBRARY_DIR}")
  message(STATUS "tiles: CUDA ${CUDAToolkit_VERSION} headers; driver/NVRTC loaded at runtime")
else()
  message(STATUS "tiles: source inspection only (CUDA Toolkit not found)")
endif()
