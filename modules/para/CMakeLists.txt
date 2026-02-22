cmake_minimum_required(VERSION 3.25)
add_library(para_cmo SHARED module.cpp)
target_compile_features(para_cmo PRIVATE cxx_std_23)
target_include_directories(para_cmo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(para_cmo PROPERTIES OUTPUT_NAME para SUFFIX ".cmo" PREFIX "")
if(WIN32)
    set_target_properties(para_cmo PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif()
target_link_libraries(para_cmo PRIVATE libcamel fmt::fmt)
target_compile_options(para_cmo PRIVATE
    $<$<CONFIG:Release>:-O3>
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-private-field
)
if(WIN32)
    target_compile_definitions(para_cmo PRIVATE NOMINMAX)
endif()
