# python 模块：.cmo 外源动态库示例，import python 加载
include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)

set(CAMEL_PYTHON_SDKS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/sdks")

set(_has_python_sdk FALSE)
if(EXISTS "${CAMEL_PYTHON_SDKS_ROOT}")
  file(GLOB _sdk_dirs LIST_DIRECTORIES true "${CAMEL_PYTHON_SDKS_ROOT}/python3*")
  foreach(_sdk IN LISTS _sdk_dirs)
    if(EXISTS "${_sdk}/include/Python.h" AND EXISTS "${_sdk}/libs")
      set(_has_python_sdk TRUE)
      break()
    endif()
  endforeach()
endif()

set(_has_active_env FALSE)
if(DEFINED ENV{VIRTUAL_ENV} AND EXISTS "$ENV{VIRTUAL_ENV}/Scripts/python.exe")
  set(_has_active_env TRUE)
elseif(DEFINED ENV{CONDA_PREFIX} AND EXISTS "$ENV{CONDA_PREFIX}/python.exe")
  set(_has_active_env TRUE)
endif()

if(NOT _has_python_sdk AND NOT _has_active_env)
  message(WARNING "Skip python.cmo build: no SDK under modules/python/sdks and no active virtual environment found.")
  return()
endif()

add_camel_module(NAME python SOURCES executor.cpp module.cpp operators.cpp type.cpp USE_PYTHON)

# Multi-target bridge build: one bridge DLL per sdk folder (python3xx).
if(EXISTS "${CAMEL_PYTHON_SDKS_ROOT}")
  file(GLOB _sdk_dirs LIST_DIRECTORIES true "${CAMEL_PYTHON_SDKS_ROOT}/python3*")
  list(SORT _sdk_dirs COMPARE NATURAL ORDER ASCENDING)
  foreach(_sdk IN LISTS _sdk_dirs)
    get_filename_component(_sdk_name "${_sdk}" NAME)
    string(REGEX REPLACE "^python" "" _py_mm "${_sdk_name}")
    if(NOT EXISTS "${_sdk}/include/Python.h" OR NOT EXISTS "${_sdk}/libs")
      message(STATUS "Skip bridge ${_sdk_name}: missing include/Python.h or libs/")
      continue()
    endif()

    set(_bridge_target "py_bridge${_py_mm}")
    add_library(${_bridge_target} SHARED bridge_runtime.cpp)
    target_compile_features(${_bridge_target} PRIVATE cxx_std_23)
    target_include_directories(${_bridge_target} PRIVATE
      ${_sdk}/include
    )
    target_link_directories(${_bridge_target} PRIVATE "${_sdk}/libs")

    string(REGEX REPLACE "^3" "" _py_minor_str "${_py_mm}")
    set(_bridge_lib_a "${_sdk}/libs/python${_py_mm}.lib")
    set(_bridge_lib_b "${_sdk}/libs/python3${_py_minor_str}.lib")

    if(EXISTS "${_bridge_lib_a}")
      target_link_libraries(${_bridge_target} PRIVATE "${_bridge_lib_a}")
    elseif(EXISTS "${_bridge_lib_b}")
      target_link_libraries(${_bridge_target} PRIVATE "${_bridge_lib_b}")
    elseif(EXISTS "${_sdk}/libs/python3.lib")
      target_link_libraries(${_bridge_target} PRIVATE "${_sdk}/libs/python3.lib")
    else()
      message(STATUS "Skip bridge ${_sdk_name}: missing python import library")
      continue()
    endif()

    if(WIN32)
      target_compile_definitions(${_bridge_target} PRIVATE NOMINMAX)
      # Official Windows SDK trees ship release import libs (pythonXY.lib) only; pyconfig.h
      # uses #pragma comment(lib, "pythonXY_d.lib") when _DEBUG is set, which then fails
      # to link under Debug. Drop _DEBUG for this TU so the pragma matches the .lib we link.
      target_compile_options(${_bridge_target} PRIVATE
        $<$<CONFIG:Debug>:$<IF:$<BOOL:${MSVC}>,/U_DEBUG,-U_DEBUG>>)
      set_target_properties(${_bridge_target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
    endif()
    set_target_properties(${_bridge_target} PROPERTIES OUTPUT_NAME "${_bridge_target}")
  endforeach()
endif()
