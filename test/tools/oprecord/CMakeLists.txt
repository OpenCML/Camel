# camel-oprecord: the operator record's own check (see main.cpp). It runs the C++ registration
# entrance and reads the record back -- `panic`, the effect and the kernel -- and it answers the
# `mayFault` comparison the table of `nodeMayFault` is measured with.
include(${PROJECT_SOURCE_DIR}/src/tools/libcamel_tool.cmake)

camel_add_tool_objects(camel-oprecord_objects main.cpp)
add_executable(camel-oprecord $<TARGET_OBJECTS:camel-oprecord_objects>)
target_link_libcamel(camel-oprecord)