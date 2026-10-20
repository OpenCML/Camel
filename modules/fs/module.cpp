#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

FsModule::FsModule(context_ptr_t ctx) : BuiltinModule("fs", ctx) {
    exportEntity(
        "read_bytes",
        OperatorGroup::create(
            "read_bytes",
            {{"fs:read_bytes",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, BytesType::get())}}));
    for (const std::string name : {"write_bytes", "replace"})
        exportEntity(
            name,
            OperatorGroup::create(
                name,
                {{"fs:" + name,
                  StaticFuncTypeResolver::create(
                      {},
                      {{Type::String(), false}, {BytesType::get(), false}},
                      Type::Void())}}));
    exportEntity(
        "read_text",
        OperatorGroup::create(
            "read_text",
            {{"fs:read_text",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::String())}}));
    exportEntity(
        "write_text",
        OperatorGroup::create(
            "write_text",
            {{"fs:write_text",
              StaticFuncTypeResolver::create(
                  {},
                  {{Type::String(), false}, {Type::String(), false}},
                  Type::Void())}}));
    // The same read as an operation that can fail (design principles 3.9): an error value
    // instead of a fault when the file cannot be opened.
    exportEntity(
        "try_read_text",
        OperatorGroup::create(
            "try_read_text",
            {{"fs:try_read_text",
              StaticFuncTypeResolver::create(
                  {},
                  {{Type::String(), false}},
                  TupleType::fallible(Type::String()))}}));
    exportEntity(
        "exists",
        OperatorGroup::create(
            "exists",
            {{"fs:exists",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Bool())}}));
    exportEntity(
        "is_file",
        OperatorGroup::create(
            "is_file",
            {{"fs:is_file",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Bool())}}));
    exportEntity(
        "is_dir",
        OperatorGroup::create(
            "is_dir",
            {{"fs:is_dir",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Bool())}}));
    exportEntity(
        "file_size",
        OperatorGroup::create(
            "file_size",
            {{"fs:file_size",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Int64())}}));
    exportEntity(
        "read_chunk",
        OperatorGroup::create(
            "read_chunk",
            {{"fs:read_chunk",
              StaticFuncTypeResolver::create(
                  {},
                  {{Type::String(), false}, {Type::Int64(), false}, {Type::Int64(), false}},
                  BytesType::get())}}));
    exportEntity(
        "mkdir",
        OperatorGroup::create(
            "mkdir",
            {{"fs:mkdir",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Bool())}}));
    exportEntity(
        "mkdirs",
        OperatorGroup::create(
            "mkdirs",
            {{"fs:mkdirs",
              StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Bool())}}));
}

module_ptr_t FsModule::create(context_ptr_t ctx) { return std::make_shared<FsModule>(ctx); }

bool FsModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("fs", [ctx = context_]() { return createFsExecutor(ctx); });
    // The purity of every operator (design principles 3.3).
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
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new FsModule(ctx->shared_from_this()); }
}
