# camel-cpp: Camel -> C++ 转译 + 自动编译，产物输出到 tmp/
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

add_executable(camel-cpp main.cpp)

set_target_properties(camel-cpp PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(camel-cpp PRIVATE
    $<$<CONFIG:Release>:-O3> $<$<CONFIG:RelWithDebInfo>:-O2> $<$<CONFIG:RelWithDebInfo>:-g3>
    $<$<CONFIG:RelWithDebInfo>:-fno-PIE> $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-g3>
)
target_link_options(camel-cpp PRIVATE $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>)

target_libcamel_tool_options(camel-cpp)
target_link_libcamel_with_json(camel-cpp)
