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
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "binary.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"

#include <filesystem>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
#include <cerrno>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs_impl = std::filesystem;

namespace {

// Why a file could not be opened, in the same words on every platform: the text of
// std::generic_category() is the C library's (glibc "No such file or directory", the MSVC
// runtime "no such file or directory"), and the diagnostic is compared with recorded output.
std::string openFailureReason(int code) {
    switch (code) {
    case ENOENT:
        return "No such file or directory";
    case EACCES:
        return "Permission denied";
    case EISDIR:
        return "Is a directory";
    case ENOTDIR:
        return "Not a directory";
    case EEXIST:
        return "File exists";
    case EMFILE:
    case ENFILE:
        return "Too many open files";
    case ENOSPC:
        return "No space left on device";
    case EROFS:
        return "Read-only file system";
    case ENAMETOOLONG:
        return "File name too long";
    case EINVAL:
        return "Invalid argument";
    case 0:
        return "The file cannot be opened";
    default:
        return std::generic_category().message(code);
    }
}

} // namespace

// Text is UTF-8. Binary mode preserves its exact line endings on every platform;
// arbitrary binary content belongs to read_bytes/write_bytes instead.
slot_t __fs_read_text__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *path = norm.get<String *>(0);
    errno        = 0;
    std::ifstream f(path->c_str(), std::ios::binary);
    if (!f.is_open()) {
        throwRuntimeFault(
            RuntimeDiag::FileOpenFailed,
            "fs.read_text",
            std::string(path->c_str()),
            openFailureReason(errno));
    }
    std::ostringstream oss;
    oss << f.rdbuf();
    if (f.bad())
        throwRuntimeFault(
            RuntimeDiag::FileSystemError,
            "fs.read_text",
            std::string(path->view()),
            "cannot read complete file");
    const auto text = oss.str();
    if (const auto bad = invalidUtf8Offset(text))
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "fs.read_text",
            "invalid UTF-8 at byte " + std::to_string(*bad) + "; use read_bytes for binary data");
    return toSlot(String::from(text, mm::autoSpace()));
}

slot_t __fs_try_read_text__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const camel::core::type::TupleType *resultType =
        camel::core::type::TupleType::fallible(camel::core::type::Type::String());
    String *path = norm.get<String *>(0);
    const std::string name(path->c_str());
    std::error_code ec;
    if (fs_impl::is_directory(name, ec)) {
        return makeError("IOError", "'" + name + "' is a directory", "<fs::try_read_text>");
    }
    std::ifstream f(name, std::ios::binary);
    if (!f.is_open()) {
        return makeError("IOError", "cannot open '" + name + "'", "<fs::try_read_text>");
    }
    std::ostringstream oss;
    oss << f.rdbuf();
    if (f.bad()) {
        return makeError("IOError", "cannot read '" + name + "'", "<fs::try_read_text>");
    }
    const auto text = oss.str();
    if (const auto bad = invalidUtf8Offset(text))
        return makeError(
            "EncodingError",
            "invalid UTF-8 at byte " + std::to_string(*bad),
            "<fs::try_read_text>");
    return makeOk(toSlot(String::from(text, mm::autoSpace())), resultType);
}

slot_t __fs_write_text__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *path    = norm.get<String *>(0);
    String *content = norm.get<String *>(1);
    fs_impl::path p(path->c_str());
    auto parent = p.parent_path();
    if (!parent.empty() && !fs_impl::exists(parent)) {
        std::error_code ec;
        if (!fs_impl::create_directories(parent, ec)) {
            throwRuntimeFault(
                RuntimeDiag::FileSystemError,
                "fs.write_text",
                std::string(path->c_str()),
                "cannot create the directory '" + parent.string() + "': " + ec.message());
        }
    }
    errno = 0;
    std::ofstream f(path->c_str(), std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        throwRuntimeFault(
            RuntimeDiag::FileOpenFailed,
            "fs.write_text",
            std::string(path->c_str()),
            openFailureReason(errno));
    }
    // UTF-8 permits NUL; use the stored length, never C-string termination.
    f.write(content->c_str(), static_cast<std::streamsize>(content->size()));
    f.close();
    if (!f) {
        throwRuntimeFault(
            RuntimeDiag::FileSystemError,
            "fs.write_text",
            std::string(path->c_str()),
            "cannot write the complete file");
    }
    return NullSlot;
}

slot_t __fs_exists__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    return toSlot(fs_impl::exists(p->c_str()));
}

slot_t __fs_is_file__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    return toSlot(fs_impl::is_regular_file(p->c_str()));
}

slot_t __fs_is_dir__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    return toSlot(fs_impl::is_directory(p->c_str()));
}

slot_t __fs_file_size__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    std::error_code ec;
    auto size = fs_impl::file_size(p->c_str(), ec);
    if (ec) {
        throwRuntimeFault(
            RuntimeDiag::FileSystemError,
            "fs.file_size",
            std::string(p->c_str()),
            ec.message());
    }
    return toSlot(static_cast<int64_t>(size));
}

slot_t __fs_read_chunk__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *path   = norm.get<String *>(0);
    int64_t offset = norm.get<int64_t>(1);
    int64_t size   = norm.get<int64_t>(2);
    if (offset < 0 || size < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "fs.read_chunk",
            std::format(
                "the offset is {} and the size is {}; neither may be negative",
                offset,
                size));
    }

    errno = 0;
    std::ifstream f(path->c_str(), std::ios::binary);
    if (!f.is_open()) {
        throwRuntimeFault(
            RuntimeDiag::FileOpenFailed,
            "fs.read_chunk",
            std::string(path->c_str()),
            openFailureReason(errno));
    }

    f.seekg(0, std::ios::end);
    std::streamoff fileSize = f.tellg();
    if (fileSize < 0) {
        throwRuntimeFault(
            RuntimeDiag::FileSystemError,
            "fs.read_chunk",
            std::string(path->c_str()),
            "the size of the file cannot be determined");
    }
    if (offset >= fileSize || size == 0) {
        return toSlot(BytesObject::from("", mm::autoSpace()));
    }

    std::streamoff remaining = fileSize - static_cast<std::streamoff>(offset);
    std::streamsize toRead =
        static_cast<std::streamsize>(std::min<int64_t>(size, static_cast<int64_t>(remaining)));
    std::string buffer(static_cast<size_t>(toRead), '\0');

    f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    f.read(buffer.data(), toRead);
    buffer.resize(static_cast<size_t>(f.gcount()));
    if (f.bad())
        throwRuntimeFault(
            RuntimeDiag::FileSystemError,
            "fs.read_chunk",
            std::string(path->view()),
            "cannot read complete chunk");
    return toSlot(BytesObject::from(buffer, mm::autoSpace()));
}

slot_t __fs_mkdir__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    std::error_code ec;
    bool ok = fs_impl::create_directory(p->c_str(), ec);
    if (!ok && ec) {
        throwRuntimeFault(
            RuntimeDiag::FileSystemError,
            "fs.mkdir",
            std::string(p->c_str()),
            ec.message());
    }
    return toSlot(ok);
}

slot_t __fs_mkdirs__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    std::error_code ec;
    bool ok = fs_impl::create_directories(p->c_str(), ec);
    if (!ok && ec) {
        throwRuntimeFault(
            RuntimeDiag::FileSystemError,
            "fs.mkdirs",
            std::string(p->c_str()),
            ec.message());
    }
    return toSlot(ok);
}

std::unordered_map<std::string, operator_t> getFsOpsMap() {
    return {
        {"read_bytes", fsReadBytes},
        {"write_bytes", fsWriteBytes},
        {"replace", fsReplaceBytes},
        {"read_text", __fs_read_text__},
        {"try_read_text", __fs_try_read_text__},
        {"write_text", __fs_write_text__},
        {"exists", __fs_exists__},
        {"is_file", __fs_is_file__},
        {"is_dir", __fs_is_dir__},
        {"file_size", __fs_file_size__},
        {"read_chunk", __fs_read_chunk__},
        {"mkdir", __fs_mkdir__},
        {"mkdirs", __fs_mkdirs__}};
}
