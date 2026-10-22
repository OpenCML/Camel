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
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

// A checkpoint is replaced as a whole: write a complete private sibling, then swap it in.
//
// The protocol (destination D, temporary T = D.pending-<pid>-<seq>, previous
// P = `.<name>.ckpt-previous` beside D: the directory name of D with a fixed hidden prefix):
//
//   1. Recover: if P exists and D does not, the previous save was interrupted between the two
//      renames; rename P back to D. If both exist, a published save could not remove P;
//      remove it. A path that is a symbolic link or a reparse point is refused, so a rename
//      never moves (or a cleanup never follows) something outside the checkpoint itself.
//   2. Write every file into a freshly created T and flush it.
//   3. If D exists, rename D to P (one directory entry moved on one volume); then rename T to
//      D. Both renames are single atomic namespace operations. On Windows a directory cannot
//      be replaced in place (MoveFileExW with MOVEFILE_REPLACE_EXISTING works for files
//      only), and POSIX rename() refuses a non-empty destination directory, so the swap is
//      necessarily two renames; P is what makes that window recoverable.
//   4. Remove P. If that fails, the new checkpoint is already in place: the leftover is
//      removed by the next save or ignored by a load, not reported as a failed save.
//
// Failure: an error before step 3 touches nothing; an error in the rename chain puts P back
// (and T is removed). A process that dies between the two renames leaves D missing and P
// complete: the next save renames P back before replacing it, and load_tree reads P in the
// meantime. A process that dies after the second rename leaves D new and P stale: the next
// save removes P. D itself is never written into, so no reader can observe a half-written
// checkpoint there.
//
// One writer at a time: two saves of the same D are not serialized (there is no lock file).
// Each builds its own T; the second swap can consume the P the first one left, and one of
// the two saves then fails explicitly, naming P. A reader still sees only a complete
// checkpoint. `save_tree` documents this single-writer contract.
//
// All three names are siblings, so every rename stays on one volume. A destination that is a
// mount point, a junction or a symbolic link to another volume is refused before anything is
// moved; if the OS still reports a cross-volume rename (EXDEV / ERROR_NOT_SAME_DEVICE), the
// save fails with that reason and the old checkpoint stays. A crash can leave an unused
// `.pending-*` directory; it is never read and can be deleted.
#include "include/ckpt/checkpoint_io.h"
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
#if defined(__APPLE__)
#include <stdio.h>
#include <sys/stdio.h>
#endif
#endif
namespace camel::ckptlib {
namespace fs = std::filesystem;
namespace {
// The previous checkpoint is a hidden sibling of the destination: a fixed prefix, the
// destination's own directory name, and a fixed suffix, so it is named for the checkpoint
// rather than read as a user's own `dir.previous`, and a reader can find it without scanning
// the parent directory.
constexpr const char *kPreviousPrefix = ".";
constexpr const char *kPreviousSuffix = ".ckpt-previous";

void nameCheck(const std::string &name) {
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\:") != std::string::npos)
        throw std::invalid_argument("invalid checkpoint leaf filename");
}

std::string display(const fs::path &path) { return camel::utils::pathToUtf8(path); }

bool isCrossDevice(const std::error_code &error) {
    if (error == std::errc::cross_device_link)
        return true;
#ifdef _WIN32
    if (error.value() == ERROR_NOT_SAME_DEVICE)
        return true;
#endif
    return false;
}

/// A failed file system operation, with UTF-8 paths in the message (a diagnostic is compared
/// with recorded output, and the narrow form of a path on Windows is not UTF-8).
[[noreturn]] void
report(const std::string &action, const fs::path &path, const std::error_code &error) {
    std::string message = action + " '" + display(path) + "': " + error.message();
    if (isCrossDevice(error))
        message += " (the two names are on different volumes; a checkpoint is published by "
                   "renaming inside one directory, so a destination that crosses a volume "
                   "boundary is not supported)";
    throw std::runtime_error(message);
}

/// Whether an entry exists at the path. A symbolic link counts as an entry even when its
/// target is missing. Errors other than "no such file" are real failures, not absence.
bool entryExists(const fs::path &path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error) {
        if (error == std::errc::no_such_file_or_directory)
            return false;
        report("cannot inspect the checkpoint path", path, error);
    }
    return status.type() != fs::file_type::not_found;
}

/// Whether the entry is a symbolic link (POSIX) or any reparse point (Windows: a symbolic
/// link, a junction, or a volume mount point). Renames move such an entry itself and a
/// recursive removal must not follow it, so the checkpoint protocol refuses one as its
/// destination.
bool isLinkEntry(const fs::path &path) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
            return false;
        report(
            "cannot inspect the checkpoint path",
            path,
            std::error_code(static_cast<int>(code), std::system_category()));
    }
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error) {
        if (error == std::errc::no_such_file_or_directory)
            return false;
        report("cannot inspect the checkpoint path", path, error);
    }
    return fs::is_symlink(status);
#endif
}

/// Deletes a directory tree without following symbolic links or reparse points: a link is
/// removed as an entry, never traversed.
void removeEntry(const fs::path &path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error) {
        if (error == std::errc::no_such_file_or_directory)
            return;
        report("cannot inspect the checkpoint path", path, error);
    }
    if (status.type() == fs::file_type::not_found)
        return;
    if (!fs::is_directory(status) || isLinkEntry(path)) {
        if (!fs::remove(path, error) && error)
            report("cannot remove the checkpoint entry", path, error);
        return;
    }
    fs::directory_iterator iterator(path, error), end;
    while (!error && iterator != end) {
        removeEntry(iterator->path());
        iterator.increment(error);
    }
    if (error)
        report("cannot list the checkpoint directory", path, error);
    if (!fs::remove(path, error) && error)
        report("cannot remove the checkpoint directory", path, error);
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
        report(
            "cannot create the checkpoint file",
            path,
            std::error_code(static_cast<int>(GetLastError()), std::system_category()));
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
                report(
                    "cannot write the checkpoint file",
                    path,
                    std::error_code(static_cast<int>(GetLastError()), std::system_category()));
            offset += written;
        }
        if (!FlushFileBuffers(file))
            report(
                "cannot flush the checkpoint file",
                path,
                std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    } catch (...) {
        CloseHandle(file);
        throw;
    }
    if (!CloseHandle(file))
        report(
            "cannot close the checkpoint file",
            path,
            std::error_code(static_cast<int>(GetLastError()), std::system_category()));
#else
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (file < 0)
        report(
            "cannot create the checkpoint file",
            path,
            std::error_code(errno, std::generic_category()));
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
                report(
                    "cannot write the checkpoint file",
                    path,
                    std::error_code(n ? errno : EIO, std::generic_category()));
            offset += static_cast<size_t>(n);
        }
        if (::fsync(file))
            report(
                "cannot flush the checkpoint file",
                path,
                std::error_code(errno, std::generic_category()));
    } catch (...) {
        ::close(file);
        throw;
    }
    if (::close(file))
        report(
            "cannot close the checkpoint file",
            path,
            std::error_code(errno, std::generic_category()));
#endif
}

void syncDirectory(const fs::path &path) {
#ifndef _WIN32
    const int file = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (file < 0)
        report(
            "cannot open the checkpoint directory",
            path,
            std::error_code(errno, std::generic_category()));
    const int result = ::fsync(file), error = errno;
    ::close(file);
    if (result)
        report(
            "cannot flush the checkpoint directory",
            path,
            std::error_code(error, std::generic_category()));
#else
    (void)path;
#endif
}

/// Renames one entry, which must not exist at the destination; used for the two halves of the
/// swap (moving the old directory aside and putting it back), where an existing destination is
/// an error rather than something to replace.
void renameAside(const fs::path &from, const fs::path &to, const std::string &action) {
#ifdef _WIN32
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        return;
    report(action, from, std::error_code(static_cast<int>(GetLastError()), std::system_category()));
#else
    std::error_code error;
    fs::rename(from, to, error);
    if (!error)
        return;
    report(action, from, error);
#endif
}

/// Publishes the completed directory at the destination. The destination must not exist: the
/// no-replace form is what makes "the old directory or the new one, never a mix" hold even if
/// another writer appears between the two renames.
void publishRename(const fs::path &from, const fs::path &to) {
#ifdef _WIN32
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        return;
    report(
        "cannot publish the checkpoint directory",
        from,
        std::error_code(static_cast<int>(GetLastError()), std::system_category()));
#elif defined(__APPLE__)
    // macOS has no renameat2; renamex_np with RENAME_EXCL is its atomic no-replace rename.
    if (::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0)
        return;
    report(
        "cannot publish the checkpoint directory",
        from,
        std::error_code(errno, std::generic_category()));
#elif defined(SYS_renameat2)
    constexpr unsigned noReplace = 1; // RENAME_NOREPLACE
    if (::syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), noReplace) == 0)
        return;
    report(
        "cannot publish the checkpoint directory",
        from,
        std::error_code(errno, std::generic_category()));
#else
    (void)from;
    (void)to;
    throw std::runtime_error(
        "this platform lacks an atomic no-replace directory rename, which publishing a "
        "checkpoint requires");
#endif
}

fs::path previousPathOf(const fs::path &target) {
    fs::path name = kPreviousPrefix;
    name += target.filename();
    name += kPreviousSuffix;
    return target.parent_path() / name;
}

/// Restores or removes the previous checkpoint left by an interrupted or only partly cleaned
/// save, so the two names D and P describe one consistent state before a new save starts.
void recoverPrevious(const fs::path &target, const fs::path &previous) {
    if (!entryExists(previous))
        return;
    if (!entryExists(target)) {
        renameAside(previous, target, "cannot restore the previous checkpoint");
        syncDirectory(target.parent_path());
        return;
    }
    removeEntry(previous);
    syncDirectory(target.parent_path());
}

/// Removes only the entries this attempt created: a temporary directory that something else
/// put a file into is not ours to delete recursively.
void removePending(const fs::path &pending, const CheckpointFiles &files) {
    std::error_code ignored;
    for (const auto &[name, bytes] : files) {
        (void)bytes;
        if (name.find_first_of("/\\:") == std::string::npos && name != "." && name != "..")
            fs::remove(pending / name, ignored);
    }
    fs::remove(pending, ignored);
}

/// The absolute form of a checkpoint directory, without a trailing separator (`save_tree` and
/// `load_tree` accept the same names), rejecting the root and an empty name, which are not
/// directories a checkpoint can be published beside.
fs::path checkpointTargetPath(const fs::path &requested) {
    fs::path target = fs::absolute(requested).lexically_normal();
    if (target.filename().empty() && target != target.root_path())
        target = target.parent_path().lexically_normal();
    if (target.filename().empty() || target == target.root_path())
        throw std::invalid_argument("invalid checkpoint directory");
    return target;
}
} // namespace

void publishCheckpoint(const fs::path &requested, const CheckpointFiles &files) {
    const auto target = checkpointTargetPath(requested);
    const auto parent = target.parent_path();
    {
        std::error_code error;
        if (!fs::create_directories(parent, error) && error)
            report("cannot create the parent directory of the checkpoint", parent, error);
    }
    if (isLinkEntry(target))
        throw std::runtime_error(
            "the checkpoint path '" + display(target) +
            "' is a symbolic link or a reparse point; save_tree replaces a directory in place "
            "and never follows a link");
    const auto previous = previousPathOf(target);
    recoverPrevious(target, previous);

    static std::atomic<uint64_t> sequence{0};
#ifdef _WIN32
    const auto process = GetCurrentProcessId();
#else
    const auto process = getpid();
#endif
    fs::path pending;
    bool reserved = false;
    for (size_t attempt = 0; attempt < 128; ++attempt) {
        pending = target;
        pending +=
            ".pending-" + std::to_string(process) + "-" + std::to_string(sequence.fetch_add(1));
        std::error_code error;
        if (fs::create_directory(pending, error)) {
            reserved = true;
            break;
        }
        if (error)
            report("cannot create the checkpoint temporary directory", pending, error);
    }
    if (!reserved)
        throw std::runtime_error(
            "cannot reserve a checkpoint temporary directory next to '" + display(target) + "'");

    try {
        for (const auto &[name, bytes] : files) {
            nameCheck(name);
            writeSynced(pending / name, bytes);
        }
        syncDirectory(pending);

        const bool hadTarget = entryExists(target);
        if (hadTarget)
            renameAside(target, previous, "cannot move the previous checkpoint aside");
        try {
            publishRename(pending, target);
        } catch (const std::exception &failure) {
            if (hadTarget) {
                try {
                    renameAside(previous, target, "cannot put the previous checkpoint back");
                } catch (const std::exception &restore) {
                    throw std::runtime_error(
                        std::string(failure.what()) + "; the previous checkpoint is at '" +
                        display(previous) + "' and could not be put back: " + restore.what());
                }
            }
            throw;
        }
        if (hadTarget) {
            // The new checkpoint is published; a previous one that cannot be removed is left
            // for the next save (recoverPrevious) instead of turning a successful save into a
            // fault that would suggest the old checkpoint is still the valid one.
            try {
                removeEntry(previous);
            } catch (const std::exception &) {
            }
        }
        // The swap is complete: the new checkpoint is in place. A failed directory flush is a
        // durability problem, not a save that can be undone, so it is not reported as a fault
        // (the files themselves were flushed before the rename).
        try {
            syncDirectory(parent);
        } catch (const std::exception &) {
        }
    } catch (...) {
        removePending(pending, files);
        throw;
    }
}

fs::path checkpointReadDirectory(const fs::path &requested, bool &recovered) {
    recovered           = false;
    const auto target   = checkpointTargetPath(requested);
    const auto previous = previousPathOf(target);
    if (!entryExists(target) && entryExists(previous)) {
        recovered = true;
        return previous;
    }
    return target;
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
} // namespace camel::ckptlib