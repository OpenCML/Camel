// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// The kernels. What the file system can refuse is an error value (`T!`) whose kind is
// `NotFound`, `PermissionDenied`, `AlreadyExists` or `IOError`, and a wrong argument is a fault
// (design principles 3.9); the text functions are written in Camel over the binary ones.
slot_t __fs_read_bytes__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __fs_read_chunk__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __fs_write_bytes__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __fs_append_bytes__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __fs_exists__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __fs_is_file__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __fs_is_dir__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __fs_file_size__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __fs_list_dir__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __fs_mkdir__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __fs_mkdirs__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
/// `remove` and `remove_dir`: a missing path is an error of kind `NotFound`, a refusal by the
/// file system one of kind `PermissionDenied`, and anything else `IOError`; `remove` refuses a
/// directory and `remove_dir` a file, a symbolic link, or a directory that is not empty.
slot_t __fs_remove__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __fs_remove_dir__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getFsOpsMap();
