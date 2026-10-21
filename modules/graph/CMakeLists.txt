include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(NAME graph SOURCES module.cpp handle.cpp nodekind.cpp regionkind.cpp read.cpp txn.cpp facts.cpp)
