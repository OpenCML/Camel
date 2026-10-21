# A tool that links libcamel is compiled in two steps so that its imports can be read before
# libcamel links (H36): the sources become an OBJECT library, and the executable is linked from
# those objects. Compiling a translation unit needs only the headers, so the object library has
# no dependency on libcamel and the export-list generation can use it.
#
#   camel_add_tool_objects(<name>_objects <sources...>)
#   add_executable(<name> $<TARGET_OBJECTS:<name>_objects>)
#
# Registration with camel_register_export_scan_target is what includes the objects in the scan.
function(camel_add_tool_objects target)
  add_library(${target} OBJECT ${ARGN})
  target_compile_features(${target} PRIVATE cxx_std_23)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
  )
  target_libcamel_tool_options(${target})
  target_link_libraries(${target} PRIVATE camel_options)
  target_compile_options(${target} PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-g3>
    $<$<CONFIG:RelWithDebInfo>:-fno-PIE>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
  )
  camel_register_export_scan_target(${target})
endfunction()

# libcamel 工具的通用配置：RPATH、DELAYLOAD、编译选项等
# 用法：include 后调用 target_link_libcamel_runtime(TARGET) 和/或 target_libcamel_tool_defaults(TARGET)

# 运行时查找 libcamel：base、base/libs、base/../libs
# Windows: DELAYLOAD；Linux/macOS: RPATH
function(target_link_libcamel_runtime target)
  if(APPLE)
    set_target_properties(${target} PROPERTIES
      BUILD_RPATH "@loader_path;@loader_path/libs;@loader_path/../libs"
      INSTALL_RPATH "@loader_path;@loader_path/libs;@loader_path/../libs"
    )
  elseif(UNIX)
    set_target_properties(${target} PROPERTIES
      BUILD_RPATH "$ORIGIN;$ORIGIN/libs;$ORIGIN/../libs"
      INSTALL_RPATH "$ORIGIN;$ORIGIN/libs;$ORIGIN/../libs"
    )
  endif()
  if(WIN32)
    target_link_libraries(${target} PRIVATE delayimp)
    target_link_options(${target} PRIVATE "-Xlinker" "/DELAYLOAD:libcamel.dll")
  endif()
  target_camel_stack_size(${target})
endfunction()

# The native stack of a Windows tool: 32 MiB, including room for SEH fault unwinding.
# The interpreters nest on the native stack (docs/cli.md, "Limits"), and the default recursion
# limit of 1000 is measured against that size. The Windows default is 1 MiB, with which the
# deepest recursion the default limit allows does not fit (it ends in the "native stack is full"
# diagnostic, or in a stack overflow while the fault unwinds), and neither does the compilation
# of a function of a few thousand statements. On Windows the value of the executable is also the
# default of every thread the process creates, so the workers of the parallel scheduler get it
# too, as on Linux, where a thread's stack has the size of the process's stack limit.
set(CAMEL_STACK_SIZE_BYTES 33554432)

# A Debug build nests larger native frames, and the limit a program meets must be the language's
# recursion limit, not an accident of the frame size (H30 review, answer 2; design principles
# 3.9: 1000 open calls). At -O0 one open call of std::nvm costs about 5.1 KiB of native stack
# (measured on Windows x64, H32), and Windows lets a call use only a sixteenth of the stack
# before the guard keeps the other fifteen sixteenths for SEH fault unwinding
# (src/execute/limits.cpp): 32 MiB therefore refuses the 400th open call, well before 1000.
#
# The Debug tools reserve 128 MiB instead, of which a sixteenth is about 8 MiB, or about 1605
# open calls at the measured cost: a margin of about 1.6x over the default limit. Release keeps
# the 32 MiB above.
#
# /STACK takes a reserve and, optionally, a commit size. Only the reserve is given, so the commit
# size stays the linker default (one page) and pages are committed as the stack is touched: this
# is address space, not committed memory. The value is also the default of every thread the
# process creates on Windows, and a std::tf worker takes the executable's reserve when it is
# larger than 8 MiB (include/camel/utils/big_stack_thread.h), so the workers get it too.
set(CAMEL_DEBUG_STACK_SIZE_BYTES 134217728)
function(target_camel_stack_size target)
  if(WIN32)
    # One option, with the size chosen by the configuration: two -Xlinker options would have the
    # second one dropped as a duplicate, and the linker then reads the value as an input file.
    target_link_options(${target} PRIVATE
      "LINKER:/STACK:$<IF:$<CONFIG:Debug>,${CAMEL_DEBUG_STACK_SIZE_BYTES},${CAMEL_STACK_SIZE_BYTES}>")
  endif()
endfunction()

# 链接 libcamel 及必要依赖；不包含 fmt、nlohmann（仅需 JSON 的 tool 自行链接 nlohmann）
function(target_link_libcamel target)
  target_link_libraries(${target} PRIVATE camel_options libcamel)
  target_link_libcamel_runtime(${target})
endfunction()

# 链接 libcamel 并附加 nlohmann_json（用于 camel-cli、lang-server 等需 JSON 输出的工具）
function(target_link_libcamel_with_json target)
  target_link_libcamel(${target})
  target_link_libraries(${target} PRIVATE nlohmann_json::nlohmann_json)
endfunction()

# libcamel 工具的通用编译选项与 include
function(target_libcamel_tool_options target)
  target_compile_features(${target} PRIVATE cxx_std_23)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-c99-extensions
    -Wno-gnu-anonymous-struct
    -Wno-nested-anon-types
    -Wno-unused-private-field
    -Wno-language-extension-token
  )
  target_include_directories(${target} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
endfunction()
