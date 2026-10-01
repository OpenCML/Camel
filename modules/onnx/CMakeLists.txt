include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)

# Capability data (see capability.h) is embedded as bytes; editing the JSON re-runs this step.
set(_onnx_capabilities ${CMAKE_CURRENT_SOURCE_DIR}/capabilities/onnxruntime-cpu.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_onnx_capabilities})
file(READ ${_onnx_capabilities} _onnx_capabilities_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _onnx_capabilities_bytes "${_onnx_capabilities_hex}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated/onnxruntime_cpu_capabilities.inc
  "${_onnx_capabilities_bytes}")
add_camel_module(
  NAME onnx
  SOURCES
    capability.cpp
    emitter.cpp
    exporter.cpp
    lowering.cpp
    lowering_grad.cpp
    module.cpp
    proto/onnx_writer.cpp
  EXTRA_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
  EXTRA_LIBS
    tensor_cmo
    nlohmann_json::nlohmann_json
)
