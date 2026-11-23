# camel-facts: the fact framework's own check (see main.cpp). It registers a kind, asks a store,
# keeps it across a version change and reads back what it kept -- the path
# `camel/facts/facts.h` declares.
include(${PROJECT_SOURCE_DIR}/src/tools/libcamel_tool.cmake)

camel_add_tool_objects(camel-facts_objects main.cpp)
add_executable(camel-facts $<TARGET_OBJECTS:camel-facts_objects>)
target_link_libcamel(camel-facts)