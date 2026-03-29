# Camel 模块 (.cmo) 的通用 CMake 封装
# 用法：include 后调用 add_camel_module(...)
#
# add_camel_module(
#   NAME <output_name>           # 输出名，如 io -> io.cmo
#   [SOURCES src1 src2 ...]      # 默认 executor.cpp module.cpp operators.cpp
#   [EXTRA_LIBS lib1 lib2]       # 额外链接库（在 libcamel fmt 之外）
#   [EXTRA_INCLUDES dir1 dir2]   # 额外 include 目录
#   [EXTRA_DEFINITIONS def1]     # 额外编译定义
#   [USE_PYTHON]                 # 启用 Python/pybind11 依赖
# )

function(add_camel_module)
  cmake_parse_arguments(ARG "USE_PYTHON" "NAME" "SOURCES;EXTRA_LIBS;EXTRA_INCLUDES;EXTRA_DEFINITIONS" ${ARGN})
  if(NOT ARG_NAME)
    message(FATAL_ERROR "add_camel_module requires NAME")
  endif()

  set(_name ${ARG_NAME})
  set(_target ${_name}_cmo)

  if(ARG_SOURCES)
    set(_sources ${ARG_SOURCES})
  else()
    set(_sources executor.cpp module.cpp operators.cpp)
  endif()
  list(APPEND _sources ${CMAKE_SOURCE_DIR}/modules/camel_module_abi.cpp)

  add_library(${_target} SHARED ${_sources})

  target_compile_features(${_target} PRIVATE cxx_std_23)
  target_include_directories(${_target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime
    ${CMAKE_SOURCE_DIR}
    ${ARG_EXTRA_INCLUDES}
  )

  set_target_properties(${_target} PROPERTIES
    OUTPUT_NAME ${_name}
    SUFFIX ".cmo"
    PREFIX ""
  )

  if(WIN32)
    set_target_properties(${_target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
  endif()

  set(_libs libcamel fmt::fmt ${ARG_EXTRA_LIBS})
  if(ARG_USE_PYTHON)
    if(NOT pybind11_FOUND)
      if(EXISTS "${CMAKE_SOURCE_DIR}/venv/Scripts/python.exe")
        set(Python_ROOT_DIR "${CMAKE_SOURCE_DIR}/venv")
      elseif(EXISTS "${CMAKE_SOURCE_DIR}/.venv/Scripts/python.exe")
        set(Python_ROOT_DIR "${CMAKE_SOURCE_DIR}/.venv")
      elseif(DEFINED ENV{VIRTUAL_ENV})
        set(Python_ROOT_DIR "$ENV{VIRTUAL_ENV}")
      endif()
      find_package(Python COMPONENTS Interpreter Development REQUIRED)
      find_package(pybind11 REQUIRED)
    endif()
    list(APPEND _libs pybind11::embed Python::Python)
  endif()
  target_link_libraries(${_target} PRIVATE ${_libs})

  target_compile_options(${_target} PRIVATE
    $<$<CONFIG:Release>:-O3>
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-unused-private-field
  )

  if(WIN32)
    target_compile_definitions(${_target} PRIVATE NOMINMAX ${ARG_EXTRA_DEFINITIONS})
  endif()
endfunction()
