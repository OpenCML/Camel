# CLI shell：链接 libcamel，提供 camel 命令
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

set(CLI_SRC main.cpp config.cpp opencmlrc.cpp timeit.cpp check.cpp module_failures.cpp)
camel_add_tool_objects(camel_objects ${CLI_SRC})
add_executable(camel $<TARGET_OBJECTS:camel_objects>)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/build_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/build_config.h
    @ONLY
)

target_link_options(camel PRIVATE $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>)

target_include_directories(camel_objects PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(camel_objects PRIVATE nlohmann_json::nlohmann_json)
target_link_libcamel_with_json(camel)
