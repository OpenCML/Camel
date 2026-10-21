# camel-parsecheck: the corpus checks of the front end (see main.cpp and
# docs/technical/34_handwritten_parser.md): the AST snapshot, the formatter property, the fuzzer
# and the benchmark.
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

camel_add_tool_objects(camel-parsecheck_objects main.cpp snapshot.cpp fuzz.cpp ../format/fmt.cpp)
add_executable(camel-parsecheck $<TARGET_OBJECTS:camel-parsecheck_objects>)
target_compile_options(camel-parsecheck_objects PRIVATE $<$<CONFIG:Release>:-O2>)
target_link_libcamel(camel-parsecheck)
