# 基于 libcamel 的独立工具：camel-cli / format / codegen / profiler / lang-server / debugger
# 均链接 libcamel，Windows 用 DELAYLOAD，Linux/macOS 用 RPATH
include(${CMAKE_CURRENT_SOURCE_DIR}/libcamel_tool.cmake)

add_subdirectory(camel-cli)
add_subdirectory(lang-server)
add_subdirectory(debugger)
add_subdirectory(format)
add_subdirectory(codegen)
add_subdirectory(profiler)
