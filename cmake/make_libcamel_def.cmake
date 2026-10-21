# Concatenates the export lists of one configuration into the .def file lld-link is given.
#
#   -DCFG=Release|RelWithDebInfo|Debug  -DCOMMON=<libcamel_exports.def>  -DEXTRA=<debug list>
#   -DOUT=<file to write>
#
# lld-link accepts only one /DEF, and the Debug library exports profiling entry points that a
# Release library does not define, so the two lists cannot each carry their own option.
if(NOT DEFINED CFG OR NOT DEFINED COMMON OR NOT DEFINED EXTRA OR NOT DEFINED OUT)
    message(FATAL_ERROR "make_libcamel_def.cmake needs CFG, COMMON, EXTRA and OUT")
endif()

file(READ "${COMMON}" content)
if(CFG STREQUAL "Debug")
    file(READ "${EXTRA}" extra)
    string(APPEND content "\n" "${extra}")
endif()
# Only write when the content changes: an unconditional write would relink libcamel (and with it
# every dependent target) on every build.
if(EXISTS "${OUT}")
    file(READ "${OUT}" previous)
    if(previous STREQUAL content)
        return()
    endif()
endif()
file(WRITE "${OUT}" "${content}")