/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 13, 2026
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * A thread whose native stack has a stated size: the part of std::thread that the workers of
 * the parallel scheduler need (start, join), with the one thing std::thread cannot be told.
 *
 * The evaluators nest on the native stack, and the default recursion limit is measured against
 * a stack of 8 MiB (docs/cli.md, "Limits"). What a thread created without a size gets depends
 * on the platform: the process's stack limit with glibc (8 MiB by default), the value in the
 * executable's header on Windows (1 MiB unless the host was linked otherwise, e.g. python.exe
 * for the Python package), and 512 KiB on macOS whatever the limit of the process is. A worker
 * of std::tf is created through this class (cmake/TaskflowWorkerStack.cmake puts it in place of
 * std::thread in Taskflow's worker), so that it has kWorkerStackBytes at least on all three:
 * the process's stack limit (Unix) or the executable's stack size (Windows) where that is larger.
 */

#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <cstdint>
#include <process.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sys/resource.h>
#endif

namespace camel::utils {

/// The native stack of a worker thread: the size of the main thread's on Linux and macOS, and
/// of the tools' on Windows (tools/libcamel_tool.cmake).
inline constexpr std::size_t kWorkerStackBytes = std::size_t{8} * 1024 * 1024;

class BigStackThread {
  public:
    BigStackThread() noexcept = default;

    template <typename F> explicit BigStackThread(F &&body) {
        auto owned = std::make_unique<std::function<void()>>(std::forward<F>(body));
        start(owned.get());
        (void)owned.release(); // the thread owns it now
    }

    BigStackThread(const BigStackThread &)            = delete;
    BigStackThread &operator=(const BigStackThread &) = delete;

    BigStackThread(BigStackThread &&other) noexcept
        : handle_(other.handle_), running_(std::exchange(other.running_, false)) {}

    BigStackThread &operator=(BigStackThread &&other) noexcept {
        if (running_) {
            std::terminate(); // as std::thread: a running thread is joined, not dropped
        }
        handle_  = other.handle_;
        running_ = std::exchange(other.running_, false);
        return *this;
    }

    ~BigStackThread() {
        if (running_) {
            std::terminate();
        }
    }

    bool joinable() const noexcept { return running_; }

    void join() {
        if (!running_) {
            throw std::system_error(std::make_error_code(std::errc::invalid_argument));
        }
#ifdef _WIN32
        WaitForSingleObject(handle_, INFINITE);
        CloseHandle(handle_);
#else
        pthread_join(handle_, nullptr);
#endif
        running_ = false;
    }

    /// The stack a thread started now gets.
    static std::size_t stackBytes() noexcept {
        std::size_t bytes = kWorkerStackBytes;
#ifdef _WIN32
        // Not less than a thread had before: the stack size in the header of the executable
        // (the tools are linked with more than 8 MiB, tools/libcamel_tool.cmake).
        if (const auto *base = reinterpret_cast<const unsigned char *>(GetModuleHandleW(nullptr))) {
            const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
            if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
                const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
                const std::size_t reserve =
                    nt->Signature == IMAGE_NT_SIGNATURE
                        ? static_cast<std::size_t>(nt->OptionalHeader.SizeOfStackReserve)
                        : 0;
                if (reserve > bytes && reserve <= std::size_t{1} << 30) {
                    bytes = reserve;
                }
            }
        }
#else
        // Not less than a thread had before: with glibc that is the stack limit of the process.
        struct rlimit limit {};
        if (getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY &&
            static_cast<std::size_t>(limit.rlim_cur) > bytes &&
            static_cast<std::size_t>(limit.rlim_cur) <= std::size_t{1} << 30) {
            bytes = static_cast<std::size_t>(limit.rlim_cur);
        }
#endif
        return bytes;
    }

  private:
    static void run(void *arg) noexcept {
        // An exception that leaves the body ends the process, as with std::thread.
        std::unique_ptr<std::function<void()>> body(static_cast<std::function<void()> *>(arg));
        (*body)();
    }

#ifdef _WIN32
    static unsigned __stdcall entry(void *arg) {
        run(arg);
        return 0;
    }

    void start(std::function<void()> *body) {
        // The size is the reservation: pages are committed as the stack grows.
        const uintptr_t handle = _beginthreadex(
            nullptr,
            static_cast<unsigned>(stackBytes()),
            &BigStackThread::entry,
            body,
            STACK_SIZE_PARAM_IS_A_RESERVATION,
            nullptr);
        if (handle == 0) {
            throw std::system_error(
                std::make_error_code(std::errc::resource_unavailable_try_again),
                "cannot start a worker thread");
        }
        handle_  = reinterpret_cast<HANDLE>(handle);
        running_ = true;
    }

    HANDLE handle_ = nullptr;
#else
    static void *entry(void *arg) {
        run(arg);
        return nullptr;
    }

    void start(std::function<void()> *body) {
        pthread_attr_t attr;
        int error = pthread_attr_init(&attr);
        if (error == 0) {
            // A size the platform refuses leaves the default in place.
            (void)pthread_attr_setstacksize(&attr, stackBytes());
            error = pthread_create(&handle_, &attr, &BigStackThread::entry, body);
            pthread_attr_destroy(&attr);
        }
        if (error != 0) {
            throw std::system_error(
                std::error_code(error, std::generic_category()),
                "cannot start a worker thread");
        }
        running_ = true;
    }

    pthread_t handle_{};
#endif
    bool running_ = false;
};

} // namespace camel::utils
