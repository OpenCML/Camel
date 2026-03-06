# CLI shell：链接 libcamel，提供 camel 命令
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

set(CLI_SRC main.cpp config.cpp)
add_executable(camel ${CLI_SRC})

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/build_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/build_config.h
    @ONLY
)

set_target_properties(camel PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(camel PRIVATE
    $<$<CONFIG:Release>:-O3> $<$<CONFIG:RelWithDebInfo>:-O2> $<$<CONFIG:RelWithDebInfo>:-g3>
    $<$<CONFIG:RelWithDebInfo>:-fno-PIE> $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-g3>
)
target_link_options(camel PRIVATE $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>)

target_libcamel_tool_options(camel)
target_include_directories(camel PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libcamel_with_json(camel)
