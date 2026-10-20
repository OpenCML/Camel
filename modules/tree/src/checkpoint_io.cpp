/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// A checkpoint is immutable: flush a private sibling directory, then publish once.
#include "include/tree/checkpoint_io.h"
#include "camel/utils/path.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace camel::treelib {
namespace fs = std::filesystem;
namespace {
void nameCheck(const std::string &name) {
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\:") != std::string::npos)
        throw std::invalid_argument("invalid checkpoint leaf filename");
}
void writeSynced(const fs::path &path, std::string_view bytes) {
#ifdef _WIN32
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::system_error(GetLastError(), std::system_category(), "create checkpoint file");
    try {
        size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD written = 0;
            if (!WriteFile(
                    file,
                    bytes.data() + offset,
                    static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1u << 30)),
                    &written,
                    nullptr) ||
                !written)
                throw std::system_error(
                    GetLastError(),
                    std::system_category(),
                    "write checkpoint file");
            offset += written;
        }
        if (!FlushFileBuffers(file))
            throw std::system_error(
                GetLastError(),
                std::system_category(),
                "flush checkpoint file");
    } catch (...) {
        CloseHandle(file);
        throw;
    }
    if (!CloseHandle(file))
        throw std::system_error(GetLastError(), std::system_category(), "close checkpoint file");
#else
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (file < 0)
        throw std::system_error(errno, std::generic_category(), "create checkpoint file");
    try {
        size_t offset = 0;
        while (offset < bytes.size()) {
            const auto n = ::write(
                file,
                bytes.data() + offset,
                std::min<size_t>(bytes.size() - offset, 1u << 30));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                throw std::system_error(
                    n ? errno : EIO,
                    std::generic_category(),
                    "write checkpoint file");
            offset += static_cast<size_t>(n);
        }
        if (::fsync(file))
            throw std::system_error(errno, std::generic_category(), "flush checkpoint file");
    } catch (...) {
        ::close(file);
        throw;
    }
    if (::close(file))
        throw std::system_error(errno, std::generic_category(), "close checkpoint file");
#endif
}
void syncDirectory(const fs::path &path) {
#ifndef _WIN32
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(), "open checkpoint directory");
    const int result = ::fsync(fd), error = errno;
    ::close(fd);
    if (result)
        throw std::system_error(error, std::generic_category(), "flush checkpoint directory");
#endif
}
} // namespace

void publishCheckpoint(const fs::path &requested, const CheckpointFiles &files) {
    const auto target = fs::absolute(requested).lexically_normal();
    if (target.filename().empty() || target == target.root_path())
        throw std::invalid_argument("invalid checkpoint directory");
    fs::create_directories(target.parent_path());
    if (fs::exists(target))
        throw std::invalid_argument(
            "checkpoint already exists: " + camel::utils::pathToUtf8(target));
    static std::atomic<uint64_t> sequence{0};
#ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    fs::path pending;
    bool owned = false;
    for (size_t i = 0; i < 128; ++i) {
        pending = target;
        pending += ".pending-" + std::to_string(pid) + "-" + std::to_string(sequence.fetch_add(1));
        if (fs::create_directory(pending)) {
            owned = true;
            break;
        }
    }
    if (!owned)
        throw std::runtime_error("cannot reserve checkpoint temporary directory");
    try {
        for (const auto &[name, bytes] : files) {
            nameCheck(name);
            writeSynced(pending / name, bytes);
        }
        syncDirectory(pending);
#ifdef _WIN32
        if (!MoveFileExW(pending.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::system_error(
                GetLastError(),
                std::system_category(),
                "publish checkpoint directory");
#elif defined(SYS_renameat2)
        constexpr unsigned noReplace = 1;
        if (::syscall(
                SYS_renameat2,
                AT_FDCWD,
                pending.c_str(),
                AT_FDCWD,
                target.c_str(),
                noReplace))
            throw std::system_error(errno, std::generic_category(), "publish checkpoint directory");
#else
        throw std::runtime_error(
            "this platform lacks atomic no-replace checkpoint directory publication");
#endif
        syncDirectory(target.parent_path());
    } catch (...) {
        // Remove only files owned by this attempt, never recurse into an arbitrary directory.
        std::error_code ignored;
        for (const auto &[name, bytes] : files) {
            (void)bytes;
            if (name.find_first_of("/\\:") == std::string::npos && name != "." && name != "..")
                fs::remove(pending / name, ignored);
        }
        fs::remove(pending, ignored);
        throw;
    }
}

std::string readCheckpointFile(const fs::path &directory, const std::string &name) {
    nameCheck(name);
    const auto path = directory / name;
    if (!fs::is_regular_file(fs::symlink_status(path)))
        throw std::invalid_argument(name + ": expected a regular checkpoint file");
    const auto size = fs::file_size(path);
    if (size > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()) ||
        size > std::string().max_size())
        throw std::invalid_argument(name + ": checkpoint file exceeds addressable size");
    std::ifstream input(path, std::ios::binary);
    std::string bytes(static_cast<size_t>(size), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::char_traits<char>::eof())
        throw std::invalid_argument(name + ": checkpoint file changed or read failed");
    return bytes;
}
} // namespace camel::treelib
