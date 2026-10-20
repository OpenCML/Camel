/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Atomic publication flushes a same-directory temporary before replacing the target.
#include "binary.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/utils/path.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <filesystem>
#include <fstream>
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
using namespace camel::core::rtdata;
using namespace camel::core::error;
namespace fs = std::filesystem;
void systemFailure(const char *op, const std::string &path, const std::string &reason) {
    throwRuntimeFault(RuntimeDiag::FileSystemError, op, path, reason);
}
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
slot_t write(ArgsView &norm, bool atomic) {
    const std::string path(norm.get<::String *>(0)->view());
    const std::string bytes(norm.get<BytesObject *>(1)->view());
    try {
        const fs::path target = camel::utils::pathFromUtf8(path);
        if (!target.parent_path().empty())
            fs::create_directories(target.parent_path());
        if (atomic) {
            atomicReplace(target, bytes);
        } else {
            std::ofstream file(target, std::ios::binary | std::ios::trunc);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            file.close();
            if (!file)
                throw std::runtime_error("cannot write the complete file");
        }
    } catch (const std::exception &error) {
        systemFailure(atomic ? "fs.replace" : "fs.write_bytes", path, error.what());
    }
    return NullSlot;
}
} // namespace
slot_t fsReadBytes(ArgsView &, ArgsView &norm, camel::core::context::Context &) {
    const std::string path(norm.get<::String *>(0)->view());
    std::ifstream file(camel::utils::pathFromUtf8(path), std::ios::binary);
    if (!file)
        systemFailure("fs.read_bytes", path, "cannot open file");
    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    if (fileSize < 0)
        systemFailure("fs.read_bytes", path, "cannot determine the file size");
    file.seekg(0, std::ios::beg);
    const size_t length = static_cast<size_t>(fileSize);
    if (length < camel::core::mm::kInlineBlockBytes) {
        std::string bytes(length, '\0');
        if (length > 0)
            file.read(bytes.data(), static_cast<std::streamsize>(length));
        bytes.resize(static_cast<size_t>(file.gcount()));
        if (file.bad())
            systemFailure("fs.read_bytes", path, "cannot read complete file");
        return toSlot(BytesObject::from(bytes, camel::core::mm::autoSpace()));
    }

    // Read directly into the data block: the Bytes value adopts it and the block does not move.
    camel::core::mm::Block *block = camel::core::mm::Block::create(length);
    file.read(reinterpret_cast<char *>(block->data()), static_cast<std::streamsize>(length));
    const size_t read = static_cast<size_t>(file.gcount());
    if (file.bad()) {
        block->release();
        systemFailure("fs.read_bytes", path, "cannot read complete file");
    }
    block->setBytes(read);
    return toSlot(BytesObject::adopt(block, read, camel::core::mm::autoSpace()));
}
slot_t fsWriteBytes(ArgsView &, ArgsView &norm, camel::core::context::Context &) {
    return write(norm, false);
}
slot_t fsReplaceBytes(ArgsView &, ArgsView &norm, camel::core::context::Context &) {
    return write(norm, true);
}
