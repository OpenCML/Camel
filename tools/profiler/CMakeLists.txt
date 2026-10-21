# camel-pr：性能分析
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

camel_add_tool_objects(camel-pr_objects main.cpp)
add_executable(camel-pr $<TARGET_OBJECTS:camel-pr_objects>)
target_link_libraries(camel-pr_objects PRIVATE nlohmann_json::nlohmann_json)
target_link_options(camel-pr PRIVATE $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>)
target_link_libcamel_with_json(camel-pr)
