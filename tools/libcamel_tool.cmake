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
endfunction()

# 链接 libcamel 及必要依赖（ANTLR4）；不包含 fmt、nlohmann（仅需 JSON 的 tool 自行链接 nlohmann）
function(target_link_libcamel target)
  target_compile_definitions(${target} PRIVATE ANTLR4CPP_STATIC)
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
    ${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
endfunction()
