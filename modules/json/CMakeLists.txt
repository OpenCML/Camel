include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(NAME json EXTRA_LIBS nlohmann_json::nlohmann_json)
