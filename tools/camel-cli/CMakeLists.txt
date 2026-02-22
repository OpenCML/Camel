# CLI shell：链接 libcamel，提供 camel 命令
set(CLI_SRC main.cpp config.cpp)
add_executable(camel ${CLI_SRC})

# BUILD_FOOTPRINT 由根 CMake 传入，用 generated header 避免命令行转义问题
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/build_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/build_config.h
    @ONLY
)

target_compile_features(camel PRIVATE cxx_std_23)
set_target_properties(camel PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_compile_options(camel PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-g3>
    $<$<CONFIG:RelWithDebInfo>:-fno-PIE>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-c99-extensions
    -Wno-gnu-anonymous-struct
    -Wno-nested-anon-types
    -Wno-unused-private-field
    -Wno-language-extension-token
)

target_link_options(camel PRIVATE
    $<$<CONFIG:RelWithDebInfo>:-no-pie>
)

# 与 libcamel 一致的选项；需显式链接 fmt/nlohmann 以解析 transitive includes
# ANTLR4CPP_STATIC 与 libcamel 一致，避免 antlr4 的 dllimport 期望
target_compile_definitions(camel PRIVATE ANTLR4CPP_STATIC)
target_link_libraries(camel PRIVATE camel_options libcamel fmt::fmt nlohmann_json::nlohmann_json)

target_include_directories(camel PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 运行时能找到 libcamel：可执行文件同目录或 ./libs（与 Windows SetDllDirectory 行为一致）
if(APPLE)
    set_target_properties(camel PROPERTIES
        BUILD_RPATH "@loader_path;@loader_path/libs"
        INSTALL_RPATH "@loader_path;@loader_path/libs"
    )
elseif(UNIX)
    set_target_properties(camel PROPERTIES
        BUILD_RPATH "$ORIGIN;$ORIGIN/libs"
        INSTALL_RPATH "$ORIGIN;$ORIGIN/libs"
    )
endif()

# Windows: 延迟加载 libcamel，使 main 开头可调用 SetDllDirectory，从而支持从 ./libs 查找 libcamel.dll
if(WIN32)
    target_link_libraries(camel PRIVATE delayimp)
    target_link_options(camel PRIVATE "-Xlinker" "/DELAYLOAD:libcamel.dll")
endif()
