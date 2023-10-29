cmake_minimum_required(VERSION 3.25)
project(camel)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(CMAKE_MACOSX_RPATH OFF)

if(MSVC)
	set(CMAKE_CXX_FLAGS /source-charset:utf-8)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	# 4003:宏定义参数不足 4005:宏重定义 4819:文件名中有非ASCII字符
    add_compile_options(/wd4003 /wd4005 /wd4819 /O2)
endif()

file(GLOB_RECURSE SRC_FILES
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/*.h"
	"${PROJECT_SOURCE_DIR}/test/*.cpp"
	"${PROJECT_SOURCE_DIR}/test/*.h"
)

find_package(antlr4-runtime CONFIG REQUIRED)

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(
	${PROJECT_NAME} PUBLIC
	lib/
	src/
)

target_link_libraries(${PROJECT_NAME} antlr4_static)