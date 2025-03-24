cmake_minimum_required(VERSION 3.25)
project(camel)

if(NOT DEFINED BUILD_TIMESTAMP)
    string(TIMESTAMP BUILD_TIMESTAMP "%y%m%d_%H%M%S")
endif()

set_source_files_properties("${PROJECT_SOURCE_DIR}/src/config.cpp" PROPERTIES COMPILE_DEFINITIONS BUILD_TIMESTAMP="${BUILD_TIMESTAMP}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 跨平台RPATH设置
if(APPLE)
    set(CMAKE_MACOSX_RPATH OFF)
    set(CMAKE_INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

file(GLOB_RECURSE SRC_FILES
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/third_party/*.cpp"
)

find_package(GTest REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if (NOT ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang"))
    message(FATAL_ERROR "This project requires Clang as the C++ compiler. Current compiler is ${CMAKE_CXX_COMPILER_ID}.")
endif()

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-flto>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-language-extension-token # for antlr4-runtime
    -Wno-dollar-in-identifier-extension # for antlr4-runtime
    -fcolor-diagnostics
    -fansi-escape-codes # windows only
)

target_compile_definitions(${PROJECT_NAME} PRIVATE ANTLR4CPP_STATIC)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_party
    ${PROJECT_SOURCE_DIR}/third_party/antlr4-runtime
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    gtest::gtest
    nlohmann_json::nlohmann_json
)
