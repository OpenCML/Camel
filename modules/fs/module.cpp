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
 * Created: Feb. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The file system: reading and writing files, the few questions about a path, and the two
 * removals. Every operator touches the file system, outside the program (design principles 3.3),
 * so every one has an effect: its call runs only when it is reachable from a sync block, and no
 * pass folds or merges it. The resources each operator touches are declared here, with the
 * executor and the operator traits.
 *
 * The signatures are written once, in the declaration module `stdlib/fs.cml` (design principles
 * 6.4; handover H19, step 10). A declaration carries a signature, not an effect: the effect and
 * resource facts stay here, where only C++ knows them.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

FsModule::FsModule(context_ptr_t ctx) : BuiltinModule("fs", ctx) {}

module_ptr_t FsModule::create(context_ptr_t ctx) { return std::make_shared<FsModule>(ctx); }

bool FsModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("fs", [ctx = context_]() { return createFsExecutor(ctx); });
    // The purity of every operator (design principles 3.3): each reads or writes the file system,
    // so none is a function of its arguments.
    camel::core::OperatorTable::instance().declare(
        "fs",
        {
            // Reading depends on what is in the file system at the time.
            {"read_text", false},
            {"try_read_text", false},
            {"read_chunk", false},
            {"read_bytes", false},
            {"write_bytes", false},
            {"replace", false},
            {"exists", false},
            {"is_file", false},
            {"is_dir", false},
            {"file_size", false},
            {"write_text", false},
            {"mkdir", false},
            {"mkdirs", false},
            {"remove", false},
            {"remove_dir", false},
        });
    // What each touches (docs/design/op-semantics 8): the file system, outside the program.
    {
        using camel::core::sem::AccessMode;
        using camel::core::sem::externalAmbient;
        camel::core::OperatorTable::instance().declareResources(
            "fs",
            {
                {"read_text", externalAmbient("fs", AccessMode::Read)},
                {"try_read_text", externalAmbient("fs", AccessMode::Read)},
                {"read_chunk", externalAmbient("fs", AccessMode::Read)},
                {"read_bytes", externalAmbient("fs", AccessMode::Read)},
                {"write_bytes", externalAmbient("fs", AccessMode::ReadModifyWrite)},
                {"replace", externalAmbient("fs", AccessMode::ReadModifyWrite)},
                {"exists", externalAmbient("fs", AccessMode::Read)},
                {"is_file", externalAmbient("fs", AccessMode::Read)},
                {"is_dir", externalAmbient("fs", AccessMode::Read)},
                {"file_size", externalAmbient("fs", AccessMode::Read)},
                {"write_text", externalAmbient("fs", AccessMode::ReadModifyWrite)},
                {"mkdir", externalAmbient("fs", AccessMode::ReadModifyWrite)},
                {"mkdirs", externalAmbient("fs", AccessMode::ReadModifyWrite)},
                {"remove", externalAmbient("fs", AccessMode::ReadModifyWrite)},
                {"remove_dir", externalAmbient("fs", AccessMode::ReadModifyWrite)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new FsModule(ctx->shared_from_this()); }
}