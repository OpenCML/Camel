# camel-format：格式化工具
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

camel_add_tool_objects(camel-format_objects main.cpp fmt.cpp)
add_executable(camel-format $<TARGET_OBJECTS:camel-format_objects>)
target_link_options(camel-format PRIVATE $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>)
target_link_libcamel(camel-format)
