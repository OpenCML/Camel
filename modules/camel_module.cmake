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
    set(_python_sdks_root "${CMAKE_SOURCE_DIR}/modules/python/sdks")
    set(_python_dll_repo "${CMAKE_SOURCE_DIR}/modules/python/dlls")
    if(EXISTS "${_python_dll_repo}")
      file(GLOB _python_repo_dlls "${_python_dll_repo}/python*.dll")
      list(LENGTH _python_repo_dlls _python_repo_dll_count)
      message(STATUS "Python DLL repository: ${_python_dll_repo} (${_python_repo_dll_count} dlls)")
    else()
      message(STATUS "Python DLL repository not found: ${_python_dll_repo} (optional, gitignored)")
    endif()

    if(NOT pybind11_FOUND)
      set(_active_python_exe "")
      if(DEFINED ENV{VIRTUAL_ENV} AND EXISTS "$ENV{VIRTUAL_ENV}/Scripts/python.exe")
        set(_active_python_exe "$ENV{VIRTUAL_ENV}/Scripts/python.exe")
      elseif(DEFINED ENV{CONDA_PREFIX} AND EXISTS "$ENV{CONDA_PREFIX}/python.exe")
        set(_active_python_exe "$ENV{CONDA_PREFIX}/python.exe")
      endif()

      set(_active_mm "")
      if(NOT _active_python_exe STREQUAL "")
        execute_process(
          COMMAND "${_active_python_exe}" -c "import sys;print(f'{sys.version_info.major}{sys.version_info.minor:02d}')"
          OUTPUT_VARIABLE _active_mm
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET)
      endif()

      set(_sdk_hit "")
      if(EXISTS "${_python_sdks_root}")
        if(NOT _active_mm STREQUAL "")
          if(EXISTS "${_python_sdks_root}/python${_active_mm}/include/Python.h" AND EXISTS "${_python_sdks_root}/python${_active_mm}/libs")
            set(_sdk_hit "${_python_sdks_root}/python${_active_mm}")
          endif()
        endif()
        if(_sdk_hit STREQUAL "")
          file(GLOB _sdk_dirs LIST_DIRECTORIES true "${_python_sdks_root}/python3*")
          list(SORT _sdk_dirs COMPARE NATURAL ORDER DESCENDING)
          foreach(_d IN LISTS _sdk_dirs)
            if(EXISTS "${_d}/include/Python.h" AND EXISTS "${_d}/libs")
              set(_sdk_hit "${_d}")
              break()
            endif()
          endforeach()
        endif()
      endif()

      if(NOT _sdk_hit STREQUAL "")
        set(Python_ROOT_DIR "${_sdk_hit}")
        if(EXISTS "${_sdk_hit}/python.exe")
          set(Python_EXECUTABLE "${_sdk_hit}/python.exe")
        endif()
        message(STATUS "Python SDK selected from modules/python/sdks: ${_sdk_hit}")
      elseif(NOT _active_python_exe STREQUAL "")
        get_filename_component(Python_ROOT_DIR "${_active_python_exe}" DIRECTORY)
        get_filename_component(Python_ROOT_DIR "${Python_ROOT_DIR}" DIRECTORY)
        set(Python_EXECUTABLE "${_active_python_exe}")
        message(STATUS "Python SDK fallback to active environment: ${Python_ROOT_DIR}")
      endif()

      find_package(Python COMPONENTS Interpreter Development REQUIRED)
      find_package(pybind11 REQUIRED)
    endif()
    list(APPEND _libs pybind11::embed Python::Python)
    target_compile_definitions(${_target} PRIVATE CAMEL_PYTHON_DLL_REPO="${_python_dll_repo}")
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
