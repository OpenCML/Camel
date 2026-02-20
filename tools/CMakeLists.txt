# 基于 libcamel 的独立工具（shell）：camel-cli / format / codegen / profiler
# 均仅链接 libcamel，不包含核心实现

# ----- camel-cli：主入口 -----
add_subdirectory(camel-cli)

# ----- camel-format：格式化工具 -----
add_executable(camel-format format/main.cpp format/fmt.cpp)
target_compile_features(camel-format PRIVATE cxx_std_23)
target_compile_definitions(camel-format PRIVATE ANTLR4CPP_STATIC)
target_link_libraries(camel-format PRIVATE camel_options libcamel fmt::fmt nlohmann_json::nlohmann_json)
target_include_directories(camel-format PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/format
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(camel-format PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter
    -Wno-c99-extensions -Wno-gnu-anonymous-struct -Wno-nested-anon-types
    -Wno-unused-private-field -Wno-language-extension-token)
if(APPLE)
    set_target_properties(camel-format PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(camel-format PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

# ----- camel-codegen：从 AST 生成代码 -----
add_executable(camel-codegen codegen/main.cpp)
target_compile_features(camel-codegen PRIVATE cxx_std_23)
target_compile_definitions(camel-codegen PRIVATE ANTLR4CPP_STATIC)
target_link_libraries(camel-codegen PRIVATE camel_options libcamel fmt::fmt nlohmann_json::nlohmann_json)
target_include_directories(camel-codegen PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(camel-codegen PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter
    -Wno-c99-extensions -Wno-gnu-anonymous-struct -Wno-nested-anon-types
    -Wno-unused-private-field -Wno-language-extension-token)
if(APPLE)
    set_target_properties(camel-codegen PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(camel-codegen PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

# ----- camel-profiler：性能分析（独立入口，便于单独运行） -----
add_executable(camel-profiler profiler/main.cpp)
target_compile_features(camel-profiler PRIVATE cxx_std_23)
target_compile_definitions(camel-profiler PRIVATE ANTLR4CPP_STATIC)
target_link_libraries(camel-profiler PRIVATE camel_options libcamel fmt::fmt nlohmann_json::nlohmann_json)
target_include_directories(camel-profiler PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime
    ${CMAKE_SOURCE_DIR}
)
target_compile_options(camel-profiler PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter
    -Wno-c99-extensions -Wno-gnu-anonymous-struct -Wno-nested-anon-types
    -Wno-unused-private-field -Wno-language-extension-token)
if(APPLE)
    set_target_properties(camel-profiler PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(camel-profiler PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
