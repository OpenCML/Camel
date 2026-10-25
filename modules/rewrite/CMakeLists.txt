include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME rewrite
  SOURCES
    fact.cpp
    checkreport.cpp
    module.cpp
    proof.cpp
    rule.cpp
    ${CAMEL_VERIFY_SOURCES}
  EXTRA_LIBS
    graph_cmo
  EXTRA_INCLUDES
    ${CMAKE_SOURCE_DIR}/modules
)