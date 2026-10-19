include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME nn
  SOURCES
    executor.cpp
    layers.cpp
    losses.cpp
    module.cpp
    optim.cpp
    state.cpp
    tensor_ops.cpp
    src/checkpoint.cpp
    src/checkpoint_io.cpp
  EXTRA_LIBS
    tensor_cmo
    nlohmann_json::nlohmann_json
)
file(READ "${CMAKE_SOURCE_DIR}/package.json" _checkpoint_package)
string(JSON _checkpoint_version GET "${_checkpoint_package}" version)
target_compile_definitions(nn_cmo PRIVATE "CAMEL_CHECKPOINT_VERSION=\"${_checkpoint_version}\"")
