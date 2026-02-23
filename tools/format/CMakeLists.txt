# camel-format：格式化工具
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

add_executable(camel-format main.cpp fmt.cpp)
set_target_properties(camel-format PROPERTIES
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
)
target_compile_options(camel-format PRIVATE
    $<$<CONFIG:Release>:-O3> $<$<CONFIG:RelWithDebInfo>:-O2> $<$<CONFIG:RelWithDebInfo>:-g3>
    $<$<CONFIG:RelWithDebInfo>:-fno-PIE> $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-g3>
)
target_link_options(camel-format PRIVATE $<$<CONFIG:RelWithDebInfo>:-no-pie>)

target_libcamel_tool_options(camel-format)
target_link_libcamel(camel-format)
