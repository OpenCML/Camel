# Camel Debugger: interactive debugger (lldb-style), links libcamel
# camel-db 提供 serve（API）；Web UI 由 tools/debugger/serve_ui.py 单独提供
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)
find_package(httplib REQUIRED)

add_executable(camel-debugger main.cpp debugger_server.cpp)
target_include_directories(camel-debugger PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(camel-debugger PROPERTIES
    RUNTIME_OUTPUT_NAME camel-db
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/build_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/build_config.h
    @ONLY
)

target_compile_options(camel-debugger PRIVATE
    $<$<CONFIG:Release>:-O3> $<$<CONFIG:RelWithDebInfo>:-O2> $<$<CONFIG:RelWithDebInfo>:-g3>
    $<$<CONFIG:RelWithDebInfo>:-fno-PIE> $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-g3>
)
target_link_options(camel-debugger PRIVATE $<$<CONFIG:RelWithDebInfo>:-no-pie>)

target_libcamel_tool_options(camel-debugger)
target_include_directories(camel-debugger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libcamel(camel-debugger)
target_link_libraries(camel-debugger PRIVATE httplib::httplib nlohmann_json::nlohmann_json)
