# 基于 libcamel 的独立工具：camel-cli / format / codegen / profiler / lang-server / debugger
# 均链接 libcamel，Windows 用 DELAYLOAD，Linux/macOS 用 RPATH
#
# 这里是随 Camel 发布的七个产品；`cmake-parsecheck` 这种只给开发与测试用的工具在 test/tools/
# 下，由顶层 CMakeLists.txt 直接 add_subdirectory（2026-11-22 搬家）。
include(${CMAKE_CURRENT_SOURCE_DIR}/libcamel_tool.cmake)

add_subdirectory(camel-cli)
add_subdirectory(camel-cpp)
add_subdirectory(lang-server)
add_subdirectory(debugger)
add_subdirectory(format)
add_subdirectory(codegen)
add_subdirectory(profiler)
