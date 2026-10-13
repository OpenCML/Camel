# The workers of the parallel scheduler (std::tf / std::prl) are threads of tf::Executor, which
# creates them with std::thread: there is no way to say how large their native stack is. That
# size is the process's stack limit with glibc (8 MiB by default), the value in the executable's
# header on Windows, and 512 KiB on macOS, where the deepest recursion the default limit allows
# does not fit a worker.
#
# camel_taskflow_with_worker_stack(<target>) gives <target> a copy of the Taskflow headers in
# which a worker's thread is camel::utils::BigStackThread (include/camel/utils/big_stack_thread.h:
# 8 MiB on every platform) instead of std::thread. The copy is made at configure time from the
# package Conan provides and put before it on the include path; three lines of two headers are
# rewritten, and the configuration fails if one of them is not found (a new version of Taskflow:
# look at Executor::_spawn and Worker again).

function(camel_taskflow_with_worker_stack target)
    set(_source "")
    foreach(_dir IN LISTS Taskflow_INCLUDE_DIRS Taskflow_INCLUDE_DIR taskflow_INCLUDE_DIRS_RELEASE)
        if(EXISTS "${_dir}/taskflow/core/executor.hpp")
            set(_source "${_dir}")
            break()
        endif()
    endforeach()
    if(_source STREQUAL "")
        message(FATAL_ERROR
            "camel_taskflow_with_worker_stack: taskflow/core/executor.hpp not found in "
            "Taskflow_INCLUDE_DIRS ('${Taskflow_INCLUDE_DIRS}')")
    endif()

    set(_patched "${CMAKE_BINARY_DIR}/taskflow-worker-stack")
    # Copied with their time stamps, and the two rewritten headers written only when they
    # change, so that configuring again does not rebuild what includes Taskflow.
    file(COPY "${_source}/taskflow" DESTINATION "${_patched}"
        PATTERN "cuda" EXCLUDE
        PATTERN "sycl" EXCLUDE
        PATTERN "core/worker.hpp" EXCLUDE
        PATTERN "core/executor.hpp" EXCLUDE)

    # worker.hpp: the member and its accessor.
    file(READ "${_source}/taskflow/core/worker.hpp" _worker)
    foreach(_needle "std::thread& thread() { return _thread; }" "std::thread _thread;")
        string(FIND "${_worker}" "${_needle}" _at)
        if(_at EQUAL -1)
            message(FATAL_ERROR
                "camel_taskflow_with_worker_stack: '${_needle}' not found in taskflow/core/worker.hpp "
                "of ${_source}; see cmake/TaskflowWorkerStack.cmake")
        endif()
    endforeach()
    string(REPLACE
        "std::thread& thread() { return _thread; }"
        "::camel::utils::BigStackThread& thread() { return _thread; }"
        _worker "${_worker}")
    string(REPLACE
        "std::thread _thread;"
        "::camel::utils::BigStackThread _thread;"
        _worker "${_worker}")
    file(WRITE "${_patched}/worker.hpp.new"
        "// Patched by cmake/TaskflowWorkerStack.cmake: the worker's thread has a stated stack size.\n"
        "#include \"camel/utils/big_stack_thread.h\"\n"
        "${_worker}")

    # executor.hpp: where the thread is started.
    file(READ "${_source}/taskflow/core/executor.hpp" _executor)
    set(_needle "_workers[id]._thread = std::thread(")
    string(FIND "${_executor}" "${_needle}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR
            "camel_taskflow_with_worker_stack: '${_needle}' not found in taskflow/core/executor.hpp "
            "of ${_source}; see cmake/TaskflowWorkerStack.cmake")
    endif()
    string(REPLACE
        "${_needle}"
        "_workers[id]._thread = ::camel::utils::BigStackThread("
        _executor "${_executor}")
    file(WRITE "${_patched}/executor.hpp.new" "${_executor}")
    foreach(_name worker executor)
        execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_patched}/${_name}.hpp.new" "${_patched}/taskflow/core/${_name}.hpp")
    endforeach()

    target_include_directories(${target} BEFORE PRIVATE "${_patched}")
    message(STATUS "Taskflow workers: 8 MiB stacks (headers patched from ${_source})")
endfunction()
