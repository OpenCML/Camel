# camel-parsediff: the differential harness of the parser replacement (see
# docs/technical/34_handwritten_parser.md). It parses a corpus with the ANTLR parser and with the
# hand-written one and compares the abstract syntax trees node for node.
include(${CMAKE_CURRENT_SOURCE_DIR}/../libcamel_tool.cmake)

add_executable(camel-parsediff main.cpp antlr_stats.cpp)
set_target_properties(camel-parsediff PROPERTIES
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
)
target_compile_options(camel-parsediff PRIVATE $<$<CONFIG:Release>:-O2>)
target_libcamel_tool_options(camel-parsediff)
target_link_libcamel(camel-parsediff)
