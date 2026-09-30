include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME onnx
  SOURCES
    emitter.cpp
    exporter.cpp
    lowering.cpp
    lowering_grad.cpp
    module.cpp
    proto/onnx_writer.cpp
  EXTRA_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
  EXTRA_LIBS
    tensor_cmo
)
