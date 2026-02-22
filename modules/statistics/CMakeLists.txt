cmake_minimum_required(VERSION 3.25)
add_library(statistics_cmo SHARED executor.cpp module.cpp operators.cpp)
target_compile_features(statistics_cmo PRIVATE cxx_std_23)
target_include_directories(statistics_cmo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(statistics_cmo PROPERTIES OUTPUT_NAME statistics SUFFIX ".cmo" PREFIX "")
if(WIN32)
    set_target_properties(statistics_cmo PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif()
target_link_libraries(statistics_cmo PRIVATE libcamel fmt::fmt)
target_compile_options(statistics_cmo PRIVATE
    $<$<CONFIG:Release>:-O3>
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-private-field
)
if(WIN32)
    target_compile_definitions(statistics_cmo PRIVATE NOMINMAX)
endif()
