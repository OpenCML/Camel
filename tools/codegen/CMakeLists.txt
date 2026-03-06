# camel-codegen：从 AST 生成代码
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

add_executable(camel-codegen main.cpp)
set_target_properties(camel-codegen PROPERTIES
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
)
target_compile_options(camel-codegen PRIVATE
    $<$<CONFIG:Release>:-O3> $<$<CONFIG:RelWithDebInfo>:-O2> $<$<CONFIG:RelWithDebInfo>:-g3>
    $<$<CONFIG:RelWithDebInfo>:-fno-PIE> $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-g3>
)
target_link_options(camel-codegen PRIVATE $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>)

target_libcamel_tool_options(camel-codegen)
target_link_libcamel(camel-codegen)
