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

# 运行时能找到 libcamel：与可执行文件同目录或 RPATH
if(APPLE)
    set_target_properties(camel PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path"
    )
elseif(UNIX)
    set_target_properties(camel PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# Windows: 将 Python DLL 复制到 camel 可执行文件目录（与 libcamel 一致）
if(WIN32)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -c
        "import sys, pathlib; p=pathlib.Path(sys.base_prefix); print(p / f'python{sys.version_info.major}{sys.version_info.minor}.dll')"
        OUTPUT_VARIABLE _PYTHON_DLL_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(EXISTS "${_PYTHON_DLL_PATH}")
        add_custom_command(TARGET camel POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_PYTHON_DLL_PATH}"
                $<TARGET_FILE_DIR:camel>
        )
        message(STATUS "Python DLL for camel CLI: ${_PYTHON_DLL_PATH}")
    endif()
endif()
