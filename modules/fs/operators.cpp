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
 * Updated: Mar. 09, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"

#include <filesystem>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
#include <fstream>
#include <sstream>

namespace fs_impl = std::filesystem;

slot_t __fs_read_text__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *path = norm.get<String *>(0);
    std::ifstream f(path->c_str());
    if (!f.is_open()) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "<read_text> failed to open: " + std::string(path->c_str()));
    }
    std::ostringstream oss;
    oss << f.rdbuf();
    return toSlot(String::from(oss.str(), mm::autoSpace()));
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
                RuntimeDiag::RuntimeError,
                "<write_text> failed to create parent dir: " + parent.string());
        }
    }
    std::ofstream f(path->c_str());
    if (!f.is_open()) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "<write_text> failed to open: " + std::string(path->c_str()));
    }
    f << content->c_str();
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
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<file_size> failed: " + ec.message());
    }
    return toSlot(static_cast<int64_t>(size));
}

slot_t __fs_read_chunk__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *path   = norm.get<String *>(0);
    int64_t offset = norm.get<int64_t>(1);
    int64_t size   = norm.get<int64_t>(2);
    if (offset < 0 || size < 0) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<read_chunk> offset and size must be >= 0");
    }

    std::ifstream f(path->c_str(), std::ios::binary);
    if (!f.is_open()) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "<read_chunk> failed to open: " + std::string(path->c_str()));
    }

    f.seekg(0, std::ios::end);
    std::streamoff fileSize = f.tellg();
    if (fileSize < 0) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<read_chunk> failed to determine file size");
    }
    if (offset >= fileSize || size == 0) {
        return toSlot(String::from("", mm::autoSpace()));
    }

    std::streamoff remaining = fileSize - static_cast<std::streamoff>(offset);
    std::streamsize toRead =
        static_cast<std::streamsize>(std::min<int64_t>(size, static_cast<int64_t>(remaining)));
    std::string buffer(static_cast<size_t>(toRead), '\0');

    f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    f.read(buffer.data(), toRead);
    buffer.resize(static_cast<size_t>(f.gcount()));
    return toSlot(String::from(buffer, mm::autoSpace()));
}

slot_t __fs_mkdir__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    std::error_code ec;
    bool ok = fs_impl::create_directory(p->c_str(), ec);
    if (!ok && ec) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<mkdir> failed: " + ec.message());
    }
    return toSlot(ok);
}

slot_t __fs_mkdirs__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *p = norm.get<String *>(0);
    std::error_code ec;
    bool ok = fs_impl::create_directories(p->c_str(), ec);
    if (!ok && ec) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<mkdirs> failed: " + ec.message());
    }
    return toSlot(ok);
}

std::unordered_map<std::string, operator_t> getFsOpsMap() {
    return {
        {"read_text", __fs_read_text__},
        {"write_text", __fs_write_text__},
        {"exists", __fs_exists__},
        {"is_file", __fs_is_file__},
        {"is_dir", __fs_is_dir__},
        {"file_size", __fs_file_size__},
        {"read_chunk", __fs_read_chunk__},
        {"mkdir", __fs_mkdir__},
        {"mkdirs", __fs_mkdirs__}};
}
