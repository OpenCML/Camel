// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "binary.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm/block.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/path.h"

#include <algorithm>
#include <filesystem>
#include <vector>

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

// What the file system refuses is an error value whose kind says which (design principles 3.9):
// the path is not there, the file system refused, the path is taken, or anything else. A wrong
// argument is a fault.

namespace {

// The kind of an errno value: the same three kinds as `errorKind` of a std::error_code.
std::string kindOfErrno(int code) {
    switch (code) {
    case ENOENT:
    case ENOTDIR:
        return "NotFound";
    case EACCES:
    case EPERM:
    case EROFS:
        return "PermissionDenied";
    case EEXIST:
        return "AlreadyExists";
    default:
        return "IOError";
    }
}

// An error value for a file that could not be opened, in the words of `openFailureReason`.
slot_t openFailure(const std::string &location, const std::string &path, int code) {
    return makeError(kindOfErrno(code), "'" + path + "': " + openFailureReason(code), location);
}

// An error value for a failed std::filesystem call. `default_error_condition` is the portable
// errno of the platform's own code, so the kind and the words are the same on every platform.
slot_t filesystemFailure(
    const std::string &location, const std::string &path, const std::error_code &error) {
    const int code = error.default_error_condition().value();
    return makeError(kindOfErrno(code), "'" + path + "': " + openFailureReason(code), location);
}

const camel::core::type::TupleType *voidResult() {
    static const camel::core::type::TupleType *type =
        camel::core::type::TupleType::fallible(camel::core::type::Type::Void());
    return type;
}

slot_t okVoid() { return makeOk(NullSlot, voidResult()); }

// The parent directories of a file are created when they are not there.
bool makeParents(const fs_impl::path &target, std::error_code &error) {
    const auto parent = target.parent_path();
    if (parent.empty())
        return true;
    fs_impl::create_directories(parent, error);
    return !error;
}

slot_t writeImpl(ArgsView &with, ArgsView &norm, bool append) {
    const std::string location = append ? "<fs::append_bytes>" : "<fs::write_bytes>";
    const std::string_view bytes = norm.get<BytesObject *>(0)->view();
    String *path                 = with.get<String *>(0);
    const bool atomic            = !append && with.size() > 1 && with.get<Bool>(1);
    const std::string name(path->view());
    const fs_impl::path target = pathOf(path);
    std::error_code error;
    if (!makeParents(target, error))
        return filesystemFailure(location, name, error);
    if (atomic) {
        try {
            fsAtomicReplace(target, bytes);
        } catch (const std::system_error &failure) {
            return makeError(
                errorKind(failure.code()),
                "'" + name + "': " + failure.what(),
                location);
        } catch (const std::exception &failure) {
            return makeError("IOError", "'" + name + "': " + failure.what(), location);
        }
        return okVoid();
    }
    errno = 0;
    std::ofstream file(
        target,
        std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!file.is_open())
        return openFailure(location, name, errno);
    // The stored length, never C-string termination: a NUL is a byte like another.
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file)
        return makeError("IOError", "'" + name + "': cannot write the complete file", location);
    return okVoid();
}

// The bytes [offset, offset + size) of the file, fewer at its end; the whole file when size is
// negative.
slot_t readImpl(const std::string &location, String *path, int64_t offset, int64_t size) {
    static const camel::core::type::TupleType *resultType =
        camel::core::type::TupleType::fallible(camel::core::type::BytesType::get());
    const std::string name(path->view());
    const fs_impl::path target = pathOf(path);
    std::error_code error;
    if (fs_impl::is_directory(target, error))
        return makeError("IOError", "'" + name + "': " + openFailureReason(EISDIR), location);
    errno = 0;
    std::ifstream file(target, std::ios::binary);
    if (!file.is_open())
        return openFailure(location, name, errno);
    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    if (fileSize < 0)
        return makeError(
            "IOError",
            "'" + name + "': the size of the file cannot be determined",
            location);
    if (offset >= fileSize || size == 0)
        return makeOk(toSlot(BytesObject::from("", mm::autoSpace())), resultType);
    const int64_t remaining = fileSize - offset;
    const size_t length     = static_cast<size_t>(size < 0 ? remaining : std::min(size, remaining));
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (length < camel::core::mm::kInlineBlockBytes) {
        std::string buffer(length, '\0');
        file.read(buffer.data(), static_cast<std::streamsize>(length));
        buffer.resize(static_cast<size_t>(file.gcount()));
        if (file.bad())
            return makeError(
                "IOError",
                "'" + name + "': cannot read the complete file",
                location);
        return makeOk(toSlot(BytesObject::from(buffer, mm::autoSpace())), resultType);
    }
    // Past the inline threshold the bytes are read into the block the Bytes value adopts: the
    // block does not move.
    camel::core::mm::Block *block = camel::core::mm::Block::create(length);
    file.read(reinterpret_cast<char *>(block->data()), static_cast<std::streamsize>(length));
    const size_t read = static_cast<size_t>(file.gcount());
    if (file.bad()) {
        block->release();
        return makeError("IOError", "'" + name + "': cannot read the complete file", location);
    }
    block->setBytes(read);
    return makeOk(toSlot(BytesObject::adopt(block, read, mm::autoSpace())), resultType);
}

} // namespace

slot_t __fs_read_bytes__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return readImpl("<fs::read_bytes>", norm.get<String *>(0), 0, -1);
}

// The path is the ordinary argument; the offset and the size are with-arguments.
slot_t __fs_read_chunk__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const int64_t offset = with.get<int64_t>(0);
    const int64_t size   = with.get<int64_t>(1);
    if (offset < 0 || size < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "fs.read_chunk",
            std::format(
                "the offset is {} and the size is {}; neither may be negative",
                offset,
                size));
    }
    return readImpl("<fs::read_chunk>", norm.get<String *>(0), offset, size);
}

slot_t __fs_write_bytes__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return writeImpl(with, norm, false);
}

slot_t __fs_append_bytes__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return writeImpl(with, norm, true);
}

slot_t __fs_exists__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    std::error_code ec;
    return toSlot(fs_impl::exists(pathOf(p), ec));
}

slot_t __fs_is_file__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    std::error_code ec;
    return toSlot(fs_impl::is_regular_file(pathOf(p), ec));
}

slot_t __fs_is_dir__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    std::error_code ec;
    return toSlot(fs_impl::is_directory(pathOf(p), ec));
}

slot_t __fs_file_size__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const camel::core::type::TupleType *resultType =
        camel::core::type::TupleType::fallible(camel::core::type::Type::Int64());
    String *p = norm.get<String *>(0);
    std::error_code ec;
    const auto size = fs_impl::file_size(pathOf(p), ec);
    if (ec)
        return filesystemFailure("<fs::file_size>", std::string(p->view()), ec);
    return makeOk(toSlot(static_cast<int64_t>(size)), resultType);
}

// The names in the byte order of the names, so the order is the same on every platform.
slot_t __fs_list_dir__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static camel::core::type::ArrayType *listType =
        camel::core::type::ArrayType::create(camel::core::type::Type::String());
    static const camel::core::type::TupleType *resultType =
        camel::core::type::TupleType::fallible(listType);
    const std::string location = "<fs::list_dir>";
    String *p                  = norm.get<String *>(0);
    const std::string name(p->view());
    std::error_code ec;
    const fs_impl::path target = pathOf(p);
    if (!fs_impl::is_directory(target, ec)) {
        if (ec)
            return filesystemFailure(location, name, ec);
        return fs_impl::exists(target, ec)
                   ? makeError("IOError", "'" + name + "': " + openFailureReason(ENOTDIR), location)
                   : makeError("NotFound", "'" + name + "': no such file or directory", location);
    }
    std::vector<std::string> names;
    fs_impl::directory_iterator iterator(target, ec);
    const fs_impl::directory_iterator end;
    if (ec)
        return filesystemFailure(location, name, ec);
    for (; iterator != end; iterator.increment(ec)) {
        if (ec)
            return filesystemFailure(location, name, ec);
        names.push_back(camel::utils::pathToUtf8(iterator->path().filename()));
    }
    if (ec)
        return filesystemFailure(location, name, ec);
    std::sort(names.begin(), names.end());
    // The strings are allocated before the array that holds them is, and rooted by it only once
    // it is filled, so each is made after the array exists.
    Array *list = Array::create(mm::autoSpace(), names.size());
    for (size_t i = 0; i < names.size(); ++i)
        list->set<slot_t>(
            i,
            toSlot(checkedString(names[i], mm::autoSpace(), "fs.list_dir")),
            listType);
    return makeOk(toSlot(list), resultType);
}

slot_t __fs_mkdir__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    const std::string name(p->view());
    std::error_code ec;
    const bool created = fs_impl::create_directory(pathOf(p), ec);
    if (ec)
        return filesystemFailure("<fs::mkdir>", name, ec);
    if (!created)
        return makeError("AlreadyExists", "'" + name + "': " + openFailureReason(EEXIST), "<fs::mkdir>");
    return okVoid();
}

// Idempotent: a directory that is already there is not an error; a file at the path is.
slot_t __fs_mkdirs__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    const std::string name(p->view());
    const fs_impl::path target = pathOf(p);
    std::error_code ec;
    if (fs_impl::is_directory(target, ec))
        return okVoid();
    fs_impl::create_directories(target, ec);
    if (ec)
        return filesystemFailure("<fs::mkdirs>", name, ec);
    if (!fs_impl::is_directory(target, ec))
        return makeError("AlreadyExists", "'" + name + "': " + openFailureReason(EEXIST), "<fs::mkdirs>");
    return okVoid();
}

slot_t __fs_remove__(ArgsView &with, ArgsView &norm, Context &) {
    return fsRemoveImpl(with, norm, false);
}

slot_t __fs_remove_dir__(ArgsView &with, ArgsView &norm, Context &) {
    return fsRemoveImpl(with, norm, true);
}

std::unordered_map<std::string, operator_t> getFsOpsMap() {
    return {
        {"read_bytes", __fs_read_bytes__},
        {"read_chunk", __fs_read_chunk__},
        {"write_bytes", __fs_write_bytes__},
        {"append_bytes", __fs_append_bytes__},
        {"exists", __fs_exists__},
        {"is_file", __fs_is_file__},
        {"is_dir", __fs_is_dir__},
        {"file_size", __fs_file_size__},
        {"list_dir", __fs_list_dir__},
        {"mkdir", __fs_mkdir__},
        {"mkdirs", __fs_mkdirs__},
        {"remove", __fs_remove__},
        {"remove_dir", __fs_remove_dir__}};
}
