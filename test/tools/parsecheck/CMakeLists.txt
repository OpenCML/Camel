# camel-parsecheck: the corpus checks of the front end (see main.cpp and
# docs/technical/34_handwritten_parser.md): the AST snapshot, the formatter property, the fuzzer
# and the benchmark.
#
# The tool moved here on 2026-11-22 (it lived in `tools/parsecheck/`), because its only readers are
# the test harness beside it (`check.py`, `corpus.py`, `probes.py`) and `feat/parse`; the two files
# it borrows from the product's tools are named from the source root, since the sibling they used to
# sit beside (`../libcamel_tool.cmake`, `../format/fmt.cpp`) is under `src/tools/` now.
include(${PROJECT_SOURCE_DIR}/src/tools/libcamel_tool.cmake)

camel_add_tool_objects(camel-parsecheck_objects
    main.cpp snapshot.cpp fuzz.cpp "${PROJECT_SOURCE_DIR}/src/tools/format/fmt.cpp")
add_executable(camel-parsecheck $<TARGET_OBJECTS:camel-parsecheck_objects>)
target_compile_options(camel-parsecheck_objects PRIVATE $<$<CONFIG:Release>:-O2>)
target_link_libcamel(camel-parsecheck)
