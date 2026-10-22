// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "binary.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm/block.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/utils/path.h"

#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

// Every path in this module is a UTF-8 Camel string; converting it where the file system is
// reached keeps the rest of the module in one encoding (see camel/utils/path.h).
fs_impl::path pathOf(const String *text) { return camel::utils::pathFromUtf8(text->view()); }

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

// Whether the entry is a symbolic link (POSIX) or any reparse point (Windows: a symbolic link,
// a junction or a volume mount point). Such an entry is removed as a link and never followed,
// and `remove_dir` refuses it: following one could delete something outside the named path.
bool isLinkEntry(const fs_impl::path &path, std::error_code &error) {
    error.clear();
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return fs_impl::is_symlink(fs_impl::symlink_status(path, error));
#endif
}

// The kind of a failed file operation (design principles 3.9): "the path is not there" and
// "the file system refused" are told apart by kind; everything else is IOError. The location
// of an error value from a C++ kernel is the operator, since the kernel does not know the call
// site.
std::string errorKind(const std::error_code &error) {
    if (error == std::errc::no_such_file_or_directory)
        return "NotFound";
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted ||
        error == std::errc::read_only_file_system)
        return "PermissionDenied";
    return "IOError";
}

slot_t removeFailure(
    const std::string &location, const std::string &path, const std::string &kind,
    const std::string &reason) {
    return makeError(kind, "'" + path + "': " + reason, location);
}

slot_t fsRemoveImpl(ArgsView &, ArgsView &norm, bool directory) {
    const std::string location = directory ? "<fs::remove_dir>" : "<fs::remove>";
    String *path               = norm.get<String *>(0);
    const std::string name(path->view());
    const fs_impl::path target = pathOf(path);
    std::error_code error;
    const auto status = fs_impl::symlink_status(target, error);
    if (error)
        return error == std::errc::no_such_file_or_directory
                   ? removeFailure(location, name, "NotFound", "no such file or directory")
                   : removeFailure(location, name, errorKind(error), error.message());
    if (status.type() == fs_impl::file_type::not_found)
        return removeFailure(location, name, "NotFound", "no such file or directory");
    const bool link = isLinkEntry(target, error);
    if (error)
        return removeFailure(location, name, errorKind(error), error.message());
    if (directory) {
        if (link)
            return removeFailure(location, name, "IOError", "is a symbolic link; use fs.remove");
        if (!fs_impl::is_directory(status))
            return removeFailure(location, name, "IOError", "is not a directory");
        fs_impl::directory_iterator iterator(target, error);
        const fs_impl::directory_iterator end;
        if (error)
            return removeFailure(location, name, errorKind(error), error.message());
        if (iterator != end)
            return removeFailure(
                location,
                name,
                "IOError",
                "is a directory that is not empty; remove its contents first");
    } else if (!link && fs_impl::is_directory(status)) {
        return removeFailure(location, name, "IOError", "is a directory; use fs.remove_dir");
    }
    error.clear();
    if (!fs_impl::remove(target, error) && error)
        return removeFailure(location, name, errorKind(error), error.message());
    const auto *result = camel::core::type::asFallible(norm.resultType());
    if (!result)
        result = camel::core::type::TupleType::fallible(camel::core::type::Type::Void());
    return makeOk(NullSlot, result);
}

} // namespace

// Text is UTF-8. Binary mode preserves its exact line endings on every platform;
// arbitrary binary content belongs to read_bytes/write_bytes instead.
slot_t __fs_read_text__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *path = norm.get<String *>(0);
    errno        = 0;
    std::ifstream f(pathOf(path), std::ios::binary);
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
    const fs_impl::path target = pathOf(path);
    std::error_code ec;
    if (fs_impl::is_directory(target, ec)) {
        return makeError("IOError", "'" + name + "' is a directory", "<fs::try_read_text>");
    }
    std::ifstream f(target, std::ios::binary);
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
    fs_impl::path p = camel::utils::pathFromUtf8(path->view());
    auto parent     = p.parent_path();
    if (!parent.empty() && !fs_impl::exists(parent)) {
        std::error_code ec;
        if (!fs_impl::create_directories(parent, ec)) {
            throwRuntimeFault(
                RuntimeDiag::FileSystemError,
                "fs.write_text",
                std::string(path->c_str()),
                "cannot create the directory '" + camel::utils::pathToUtf8(parent) +
                    "': " + ec.message());
        }
    }
    errno = 0;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
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
    return toSlot(fs_impl::exists(camel::utils::pathFromUtf8(p->view())));
}

slot_t __fs_is_file__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    return toSlot(fs_impl::is_regular_file(camel::utils::pathFromUtf8(p->view())));
}

slot_t __fs_is_dir__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    return toSlot(fs_impl::is_directory(camel::utils::pathFromUtf8(p->view())));
}

slot_t __fs_file_size__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    std::error_code ec;
    auto size = fs_impl::file_size(camel::utils::pathFromUtf8(p->view()), ec);
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
    std::ifstream f(pathOf(path), std::ios::binary);
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

    f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (static_cast<size_t>(toRead) < camel::core::mm::kInlineBlockBytes) {
        std::string buffer(static_cast<size_t>(toRead), '\0');
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

    // A chunk past the inline threshold is read into the block the Bytes value adopts.
    camel::core::mm::Block *block = camel::core::mm::Block::create(static_cast<size_t>(toRead));
    f.read(reinterpret_cast<char *>(block->data()), toRead);
    const size_t read = static_cast<size_t>(f.gcount());
    if (f.bad()) {
        block->release();
        throwRuntimeFault(
            RuntimeDiag::FileSystemError,
            "fs.read_chunk",
            std::string(path->view()),
            "cannot read complete chunk");
    }
    block->setBytes(read);
    return toSlot(BytesObject::adopt(block, read, mm::autoSpace()));
}

slot_t __fs_mkdir__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    std::error_code ec;
    bool ok = fs_impl::create_directory(camel::utils::pathFromUtf8(p->view()), ec);
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
    bool ok = fs_impl::create_directories(camel::utils::pathFromUtf8(p->view()), ec);
    if (!ok && ec) {
        throwRuntimeFault(
            RuntimeDiag::FileSystemError,
            "fs.mkdirs",
            std::string(p->c_str()),
            ec.message());
    }
    return toSlot(ok);
}

slot_t __fs_remove__(ArgsView &with, ArgsView &norm, Context &) {
    return fsRemoveImpl(with, norm, false);
}

slot_t __fs_remove_dir__(ArgsView &with, ArgsView &norm, Context &) {
    return fsRemoveImpl(with, norm, true);
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
        {"mkdirs", __fs_mkdirs__},
        {"remove", __fs_remove__},
        {"remove_dir", __fs_remove_dir__}};
}
