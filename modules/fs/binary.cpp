// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Atomic publication flushes a same-directory temporary before replacing the target.

#include "binary.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace {
namespace fs = std::filesystem;
void atomicReplace(const fs::path &path, std::string_view bytes) {
    static std::atomic<uint64_t> sequence{0};
    fs::path temporary;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    for (size_t attempt = 0; attempt < 128; ++attempt) {
        temporary = path;
        temporary += ".pending-" + std::to_string(GetCurrentProcessId()) + "-" +
                     std::to_string(sequence.fetch_add(1));
        file = CreateFileW(
            temporary.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file != INVALID_HANDLE_VALUE)
            break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            throw std::system_error(
                GetLastError(),
                std::system_category(),
                "create replacement temporary");
    }
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("cannot create replacement temporary");
    try {
        size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD written = 0;
            const DWORD count =
                static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1u << 30));
            if (!WriteFile(file, bytes.data() + offset, count, &written, nullptr) || written == 0)
                throw std::system_error(
                    GetLastError(),
                    std::system_category(),
                    "write replacement temporary");
            offset += written;
        }
        if (!FlushFileBuffers(file))
            throw std::system_error(
                GetLastError(),
                std::system_category(),
                "flush replacement temporary");
        const bool closed = CloseHandle(file);
        file              = INVALID_HANDLE_VALUE;
        if (!closed)
            throw std::system_error(
                GetLastError(),
                std::system_category(),
                "close replacement temporary");
        if (!MoveFileExW(
                temporary.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(), std::system_category(), "publish replacement");
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        DeleteFileW(temporary.c_str());
        throw;
    }
#else
    int file = -1;
    for (size_t attempt = 0; attempt < 128; ++attempt) {
        temporary = path;
        temporary +=
            ".pending-" + std::to_string(getpid()) + "-" + std::to_string(sequence.fetch_add(1));
        file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (file >= 0)
            break;
        if (errno != EEXIST)
            throw std::system_error(errno, std::generic_category(), "create replacement temporary");
    }
    if (file < 0)
        throw std::runtime_error("cannot create replacement temporary");
    try {
        size_t offset = 0;
        while (offset < bytes.size()) {
            const auto count = ::write(
                file,
                bytes.data() + offset,
                std::min<size_t>(bytes.size() - offset, 1u << 30));
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw std::system_error(
                    count == 0 ? EIO : errno,
                    std::generic_category(),
                    "write replacement temporary");
            offset += static_cast<size_t>(count);
        }
        if (::fsync(file) != 0)
            throw std::system_error(errno, std::generic_category(), "flush replacement temporary");
        const int closed = ::close(file);
        file             = -1;
        if (closed != 0)
            throw std::system_error(errno, std::generic_category(), "close replacement temporary");
        fs::rename(temporary, path);
        const auto parent   = path.parent_path().empty() ? fs::path(".") : path.parent_path();
        const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory < 0)
            throw std::system_error(errno, std::generic_category(), "open replacement directory");
        const int synced = ::fsync(directory), syncError = errno;
        ::close(directory);
        if (synced != 0)
            throw std::system_error(
                syncError,
                std::generic_category(),
                "flush replacement directory");
    } catch (...) {
        if (file >= 0)
            ::close(file);
        ::unlink(temporary.c_str());
        throw;
    }
#endif
}
} // namespace

// Writes the bytes to a temporary in the directory of the target, flushes it, and renames it over
// the target: a reader sees the old content or the new, never half of it. Throws on failure.
void fsAtomicReplace(const std::filesystem::path &path, std::string_view bytes) {
    atomicReplace(path, bytes);
}
