# Camel Debugger: interactive debugger (lldb-style), links libcamel
# Native DAP/REPL/HTTP backend; the graphical frontend is maintained in OpenCML-Ext.
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)
find_package(httplib REQUIRED)

camel_add_tool_objects(camel-debugger_objects
    main.cpp
    server.cpp
    src/state.cpp
    src/compile.cpp
    src/gir_json.cpp
    src/process.cpp
    src/spawn.cpp
    src/run.cpp
    src/worker.cpp
    src/repl.cpp
    src/repl_debug.cpp
    src/command/dispatcher.cpp
    src/command/commands.cpp
    src/core/debug_core.cpp
    src/core/explain.cpp
    src/core/memory.cpp
    src/core/pipeline.cpp
    src/core/frames.cpp
    src/core/program_index.cpp
    src/core/values.cpp
    src/dap/adapter.cpp
)
add_executable(camel-debugger $<TARGET_OBJECTS:camel-debugger_objects>)
target_include_directories(camel-debugger_objects PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/modules/rewrite
)
target_link_libraries(camel-debugger_objects PRIVATE httplib::httplib nlohmann_json::nlohmann_json)
set_target_properties(camel-debugger PROPERTIES RUNTIME_OUTPUT_NAME camel-db)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/build_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/build_config.h
    @ONLY
)

target_link_options(camel-debugger PRIVATE $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>)

target_link_libcamel(camel-debugger)
target_link_libraries(camel-debugger PRIVATE httplib::httplib nlohmann_json::nlohmann_json)
# On macOS cpp-httplib resolves host names through CFHost (CFNetwork), which the Conan
# package does not link for us.
if(APPLE)
    target_link_libraries(camel-debugger PRIVATE "-framework CoreFoundation" "-framework CFNetwork")
endif()
