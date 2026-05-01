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
    # PyPI rollup only (env set by scripts/build.js when rollupMm passed). Normal build: env unset → full SDK scan.
    set(_rollup_mm "")
    set(_rollup_sdk "")
    if(DEFINED ENV{CAMEL_PYPI_SINGLE_SDK_MM} AND NOT "$ENV{CAMEL_PYPI_SINGLE_SDK_MM}" STREQUAL "")
      set(_rollup_mm "$ENV{CAMEL_PYPI_SINGLE_SDK_MM}")
      set(_rollup_sdk "${_python_sdks_root}/python${_rollup_mm}")
    endif()
    set(_python_dll_repo "${CMAKE_SOURCE_DIR}/modules/python/dlls")
    if(EXISTS "${_python_dll_repo}")
      file(GLOB _python_repo_dlls "${_python_dll_repo}/python*.dll")
      list(LENGTH _python_repo_dlls _python_repo_dll_count)
      message(STATUS "Python DLL repository: ${_python_dll_repo} (${_python_repo_dll_count} dlls)")
    else()
      message(STATUS "Python DLL repository not found: ${_python_dll_repo} (optional, gitignored)")
    endif()

    if(NOT pybind11_FOUND)
      # Never use Windows registry for Python discovery (isolated sdks / venv builds).
      set(Python_FIND_REGISTRY NEVER)
      set(Python3_FIND_REGISTRY NEVER)
      # Prefer an active venv when CMake picks an interpreter by itself; explicit -DPython*_EXECUTABLE still wins.
      set(Python_FIND_VIRTUALENV FIRST)
      set(Python3_FIND_VIRTUALENV FIRST)

      set(_rollup_sdk_ok FALSE)
      if(NOT _rollup_sdk STREQUAL "" AND EXISTS "${_rollup_sdk}/include/Python.h" AND EXISTS "${_rollup_sdk}/libs")
        set(Python_ROOT_DIR "${_rollup_sdk}")
        if(WIN32 AND EXISTS "${_rollup_sdk}/python.exe")
          set(Python_EXECUTABLE "${_rollup_sdk}/python.exe")
        elseif(EXISTS "${_rollup_sdk}/bin/python3")
          set(Python_EXECUTABLE "${_rollup_sdk}/bin/python3")
        elseif(EXISTS "${_rollup_sdk}/bin/python")
          set(Python_EXECUTABLE "${_rollup_sdk}/bin/python")
        endif()
        message(STATUS "Python from CAMEL_PYPI_SINGLE_SDK_MM (PyPI wheel): ${_rollup_sdk}")
        set(_rollup_sdk_ok TRUE)
      endif()

      if(NOT _rollup_sdk_ok)
        # pypi-rollup + venv: build.js sets CAMEL_PYPI_SDK_ROOT to sys.base_prefix (not under modules/python/sdks).
        set(_camel_pypi_interpreter_tree FALSE)
        if(DEFINED ENV{CAMEL_PYPI_SDK_ROOT} AND NOT "$ENV{CAMEL_PYPI_SDK_ROOT}" STREQUAL "")
          set(Python_ROOT_DIR "$ENV{CAMEL_PYPI_SDK_ROOT}")
          if(DEFINED ENV{CAMEL_PYPI_PYTHON_EXECUTABLE} AND NOT "$ENV{CAMEL_PYPI_PYTHON_EXECUTABLE}" STREQUAL "")
            set(Python_EXECUTABLE "$ENV{CAMEL_PYPI_PYTHON_EXECUTABLE}")
            set(Python3_EXECUTABLE "$ENV{CAMEL_PYPI_PYTHON_EXECUTABLE}" CACHE FILEPATH "" FORCE)
          endif()
          message(STATUS "Python PyPI rollup: interpreter tree (CAMEL_PYPI_SDK_ROOT): ${Python_ROOT_DIR}")
          set(_camel_pypi_interpreter_tree TRUE)
        endif()
        if(NOT _camel_pypi_interpreter_tree)
        set(_active_python_exe "")
        if(DEFINED ENV{VIRTUAL_ENV})
          if(WIN32 AND EXISTS "$ENV{VIRTUAL_ENV}/Scripts/python.exe")
            set(_active_python_exe "$ENV{VIRTUAL_ENV}/Scripts/python.exe")
          elseif(EXISTS "$ENV{VIRTUAL_ENV}/bin/python3")
            set(_active_python_exe "$ENV{VIRTUAL_ENV}/bin/python3")
          elseif(EXISTS "$ENV{VIRTUAL_ENV}/bin/python")
            set(_active_python_exe "$ENV{VIRTUAL_ENV}/bin/python")
          endif()
        endif()
        if(_active_python_exe STREQUAL "" AND DEFINED ENV{CONDA_PREFIX})
          if(WIN32 AND EXISTS "$ENV{CONDA_PREFIX}/python.exe")
            set(_active_python_exe "$ENV{CONDA_PREFIX}/python.exe")
          elseif(EXISTS "$ENV{CONDA_PREFIX}/bin/python3")
            set(_active_python_exe "$ENV{CONDA_PREFIX}/bin/python3")
          elseif(EXISTS "$ENV{CONDA_PREFIX}/bin/python")
            set(_active_python_exe "$ENV{CONDA_PREFIX}/bin/python")
          endif()
        endif()
        if(_active_python_exe STREQUAL "")
          find_program(_active_python_exe NAMES python3 python NO_CACHE)
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
          if(WIN32 AND EXISTS "${_sdk_hit}/python.exe")
            set(Python_EXECUTABLE "${_sdk_hit}/python.exe")
          elseif(EXISTS "${_sdk_hit}/bin/python3")
            set(Python_EXECUTABLE "${_sdk_hit}/bin/python3")
          elseif(EXISTS "${_sdk_hit}/bin/python")
            set(Python_EXECUTABLE "${_sdk_hit}/bin/python")
          endif()
          message(STATUS "Python SDK selected from modules/python/sdks: ${_sdk_hit}")
        elseif(NOT _active_python_exe STREQUAL "")
          get_filename_component(_py_bin_dir "${_active_python_exe}" DIRECTORY)
          get_filename_component(_py_bin_leaf "${_py_bin_dir}" NAME)
          if(_py_bin_leaf STREQUAL "Scripts" OR _py_bin_leaf STREQUAL "bin")
            get_filename_component(Python_ROOT_DIR "${_py_bin_dir}" DIRECTORY)
          else()
            set(Python_ROOT_DIR "${_py_bin_dir}")
          endif()
          set(Python_EXECUTABLE "${_active_python_exe}")
          message(STATUS "Python SDK fallback to active environment: ${Python_ROOT_DIR}")
        endif()
        endif()
      endif()

      # Windows isolated SDK trees: set hints for FindPython3 (matches -DPython3_ROOT_DIR from build.js).
      if(WIN32 AND DEFINED Python_ROOT_DIR AND NOT Python_ROOT_DIR STREQUAL "")
        set(Python3_ROOT_DIR "${Python_ROOT_DIR}" CACHE PATH "" FORCE)
        if(DEFINED Python_EXECUTABLE AND NOT Python_EXECUTABLE STREQUAL "")
          set(Python3_EXECUTABLE "${Python_EXECUTABLE}" CACHE FILEPATH "" FORCE)
        endif()
        if(DEFINED ENV{CAMEL_PYPI_PY_INCLUDE_DIR} AND EXISTS "$ENV{CAMEL_PYPI_PY_INCLUDE_DIR}/Python.h")
          set(Python3_INCLUDE_DIR "$ENV{CAMEL_PYPI_PY_INCLUDE_DIR}" CACHE PATH "" FORCE)
        elseif(EXISTS "${Python_ROOT_DIR}/include/Python.h")
          set(Python3_INCLUDE_DIR "${Python_ROOT_DIR}/include" CACHE PATH "" FORCE)
        elseif(EXISTS "${Python_ROOT_DIR}/Include/Python.h")
          set(Python3_INCLUDE_DIR "${Python_ROOT_DIR}/Include" CACHE PATH "" FORCE)
        endif()
        set(_camel_lib_root "")
        if(DEFINED ENV{CAMEL_PYPI_PY_LIBS_DIR} AND IS_DIRECTORY "$ENV{CAMEL_PYPI_PY_LIBS_DIR}")
          set(_camel_lib_root "$ENV{CAMEL_PYPI_PY_LIBS_DIR}")
        elseif(EXISTS "${Python_ROOT_DIR}/libs")
          set(_camel_lib_root "${Python_ROOT_DIR}/libs")
        elseif(EXISTS "${Python_ROOT_DIR}/Libs")
          set(_camel_lib_root "${Python_ROOT_DIR}/Libs")
        endif()
        if(NOT _camel_lib_root STREQUAL "")
          file(GLOB _camel_py_libs "${_camel_lib_root}/python*.lib")
          list(SORT _camel_py_libs COMPARE NATURAL ORDER DESCENDING)
          set(_camel_import_lib "")
          foreach(_f IN LISTS _camel_py_libs)
            get_filename_component(_n "${_f}" NAME)
            string(TOLOWER "${_n}" _nl)
            # NTFS often preserves case; match case-insensitively (python39.lib, PYTHON39.LIB).
            if(_nl MATCHES "^python3[0-9]+\\.lib$" AND NOT _nl STREQUAL "python3.lib")
              set(_camel_import_lib "${_f}")
              break()
            endif()
          endforeach()
          if(_camel_import_lib STREQUAL "")
            foreach(_f IN LISTS _camel_py_libs)
              get_filename_component(_n "${_f}" NAME)
              string(TOLOWER "${_n}" _nl)
              if(_nl STREQUAL "python3.lib")
                set(_camel_import_lib "${_f}")
                break()
              endif()
            endforeach()
          endif()
          if(NOT _camel_import_lib STREQUAL "")
            set(Python3_LIBRARY "${_camel_import_lib}" CACHE FILEPATH "" FORCE)
          else()
            message(WARNING "No versioned python3*.lib under ${_camel_lib_root} — embeddable-only trees cannot satisfy Development.Embed; use a full CPython install or sync-python-sdks.")
          endif()
        endif()
      endif()

      find_package(Python3 COMPONENTS Interpreter Development Development.Module Development.Embed REQUIRED)
      find_package(pybind11 REQUIRED)
    endif()
    list(APPEND _libs pybind11::embed Python3::Python)
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
