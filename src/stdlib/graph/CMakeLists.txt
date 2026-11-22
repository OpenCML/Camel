include(${PROJECT_SOURCE_DIR}/cmake/camel_module.cmake)
add_camel_module(NAME graph SOURCES module.cpp handle.cpp nodekind.cpp regionkind.cpp read.cpp txn.cpp facts.cpp probe.cpp)
