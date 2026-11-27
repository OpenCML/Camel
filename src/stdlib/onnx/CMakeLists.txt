include(${PROJECT_SOURCE_DIR}/cmake/camel_module.cmake)

# Capability data (see capability.h) is embedded as bytes; editing the JSON re-runs this step.
set(_onnx_capabilities ${CMAKE_CURRENT_SOURCE_DIR}/capabilities/onnxruntime-cpu.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_onnx_capabilities})
file(READ ${_onnx_capabilities} _onnx_capabilities_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _onnx_capabilities_bytes "${_onnx_capabilities_hex}")
# file(GENERATE) leaves the timestamp of an unchanged output alone; file(WRITE) does not. The old
# form stamped this header on every configure and recompiled capability.cpp (16 s) even when the
# JSON had not moved. capability.cpp is the only unit that includes it.
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/onnxruntime_cpu_capabilities.inc
  CONTENT "${_onnx_capabilities_bytes}")
add_camel_module(
  NAME onnx
  SOURCES
    capability.cpp
    emitter.cpp
    exporter.cpp
    lowering.cpp
    lowering_grad.cpp
    module.cpp
    passes/export.cpp
    proto/onnx_writer.cpp
  EXTRA_INCLUDES
    ${CMAKE_CURRENT_BINARY_DIR}/generated
  LINK_MODULES
    tensor
    nn
  EXTRA_LIBS
    nlohmann_json::nlohmann_json
)
