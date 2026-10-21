# camel-codegen：从 AST 生成代码
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

camel_add_tool_objects(camel-codegen_objects main.cpp)
add_executable(camel-codegen $<TARGET_OBJECTS:camel-codegen_objects>)
target_link_options(camel-codegen PRIVATE $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>)
target_link_libcamel(camel-codegen)
