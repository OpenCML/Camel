include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME rewrite
  SOURCES
    module.cpp
    rule.cpp
  EXTRA_LIBS
    graph_cmo
)