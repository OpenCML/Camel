# The Python binding (H253): the CPython extension `camel._camel`, built only where Python's
# development files are, and skipped with a STATUS line where they are not.
#
# Why a target of its own rather than a part of modules/python: that module embeds Python inside
# Camel (`import python`, an operator that calls Python). This one is the other direction -- a
# host of libcamel, like the `camel` command-line tool -- and the two share no code. It lives
# under bindings/ so that neither `tools/` (executables the SDK ships in bin/) nor `modules/`
# (.cmo modules `import` loads) has to carry it.
#
# The artifact lands in build/python/ and scripts/collect-out.js puts it in out/<tag>/python/, so
# the wheel's copy of the install carries it at camel/.cmlenv/python/ and libcamel at
# camel/.cmlenv/libs/ -- the same relative distance ($ORIGIN/../libs) the install has.

# Development.Module is the component for an extension module: headers and, on Windows, the
# import library. It is present wherever Python can build an extension at all, which is also
# where `pip install` can build this one from source.
find_package(Python3 3.10 COMPONENTS Interpreter Development.Module QUIET)

if(NOT Python3_FOUND)
  message(STATUS "Skipping bindings/python: no Python 3.10+ development files (Development.Module).")
  return()
endif()

include(${PROJECT_SOURCE_DIR}/tools/libcamel_tool.cmake)

# Two steps, like every other target that links libcamel (H36): the sources become an OBJECT
# library whose undefined symbols can be read before libcamel links, and the extension module is
# linked from those objects. On Windows that is what puts the symbols this file calls into
# libcamel's export list.
camel_add_tool_objects(camel_python_ext_objects module.cpp)
target_include_directories(camel_python_ext_objects PRIVATE ${Python3_INCLUDE_DIRS})
target_link_libraries(camel_python_ext_objects PRIVATE Python3::Module)

Python3_add_library(camel_python_ext MODULE $<TARGET_OBJECTS:camel_python_ext_objects> WITH_SOABI)
set_target_properties(camel_python_ext PROPERTIES OUTPUT_NAME "_camel")
target_link_libcamel(camel_python_ext)
target_link_libraries(camel_python_ext PRIVATE Python3::Module)

# The module is read from a directory that sits next to libs/: build/python/ beside build/, and
# out/<tag>/python/ beside out/<tag>/libs/.
set(_camel_python_rpath "$ORIGIN;$ORIGIN/..;$ORIGIN/../libs")
if(APPLE)
  set(_camel_python_rpath "@loader_path;@loader_path/..;@loader_path/../libs")
elseif(WIN32)
  set(_camel_python_rpath "")
endif()
set_target_properties(camel_python_ext PROPERTIES
  BUILD_RPATH "${_camel_python_rpath}"
  INSTALL_RPATH "${_camel_python_rpath}"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/python"
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/python"
)
# The module is copied to the install, not `install()`ed, so the build tree's runpath must
# already be the delivered one; otherwise the copied file names build/ and finds no libcamel in
# out/<tag>/libs.
set_property(TARGET camel_python_ext PROPERTY BUILD_WITH_INSTALL_RPATH TRUE)