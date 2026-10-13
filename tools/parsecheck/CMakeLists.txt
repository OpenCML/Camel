# camel-parsecheck: the corpus checks of the front end (see main.cpp and
# docs/technical/34_handwritten_parser.md): the AST snapshot, the formatter property, the fuzzer
# and the benchmark.
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

add_executable(camel-parsecheck main.cpp snapshot.cpp fuzz.cpp ../format/fmt.cpp)
set_target_properties(camel-parsecheck PROPERTIES
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
)
target_compile_options(camel-parsecheck PRIVATE $<$<CONFIG:Release>:-O2>)
target_libcamel_tool_options(camel-parsecheck)
target_link_libcamel(camel-parsecheck)
