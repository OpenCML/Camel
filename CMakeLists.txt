cmake_minimum_required(VERSION 3.25)
project(camel)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB_RECURSE SRC_FILES
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/*.h"
	"${PROJECT_SOURCE_DIR}/third_party/*.cpp"
	"${PROJECT_SOURCE_DIR}/third_party/*.h"
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /source-charset:utf-8
        /EHsc
        # 4003:宏定义参数不足 4005:宏重定义 4819:文件名中有非ASCII字符
        /wd4003 /wd4005 /wd4819
        $<$<CONFIG:Release>:/O2 /Ot>
        $<$<CONFIG:Debug>:/Od>
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-O0 -g>
    )
endif()

find_package(antlr4-runtime CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/lib
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_party
)

target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

if(TARGET antlr4_shared)
    target_link_libraries(${PROJECT_NAME} PRIVATE antlr4_shared)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE antlr4_static)
endif()