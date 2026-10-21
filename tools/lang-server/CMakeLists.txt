# camel-ls：LSP 语言服务器
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

camel_add_tool_objects(camel-ls_objects main.cpp server.cpp workspace.cpp reading.cpp calls.cpp protocol.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../format/fmt.cpp)
add_executable(camel-ls $<TARGET_OBJECTS:camel-ls_objects>)
target_include_directories(camel-ls_objects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../format)
target_link_libraries(camel-ls_objects PRIVATE nlohmann_json::nlohmann_json)
target_link_options(camel-ls PRIVATE $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>)
target_link_libcamel_with_json(camel-ls)
