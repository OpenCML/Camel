# camel-cpp: a Camel program to an executable in one step (std::cpp, then the C++ compiler).
# It runs the `camel` next to it and the system's compiler, and links nothing of Camel itself.
add_executable(camel-cpp main.cpp)

set_target_properties(camel-cpp PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(camel-cpp PRIVATE ${PROJECT_SOURCE_DIR}/include)
