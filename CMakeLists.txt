cmake_minimum_required(VERSION 3.25)
project(camel)

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

find_package(antlr4-runtime CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /utf-8
        /EHsc
        # 4003:宏定义参数不足 4005:宏重定义 4819:文件名中有非ASCII字符
        /wd4003 /wd4005 /wd4819
        $<$<CONFIG:Release>:/O2 /Ot>
        $<$<CONFIG:Debug>:/Od>
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE 
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX  # 禁用Windows.h的min/max宏
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3 -flto>
        $<$<CONFIG:Debug>:-O0 -g3>
        -Wall -Wextra -Wpedantic
    )
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_party
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    $<IF:$<TARGET_EXISTS:antlr4_shared>,antlr4_shared,antlr4_static>
    nlohmann_json::nlohmann_json
)