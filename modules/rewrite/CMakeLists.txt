include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME rewrite
  SOURCES
    analysis.cpp
    module.cpp
    rule.cpp
    run.cpp
  EXTRA_LIBS
    graph_cmo
)