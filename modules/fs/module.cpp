#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/type/resolver.h"
#include "executor.h"

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

FsModule::FsModule(context_ptr_t ctx) : BuiltinModule("fs", ctx) {
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
                  Type::String())}}));
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
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new FsModule(ctx->shared_from_this()); }
}
