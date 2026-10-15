/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Public module boundary: function graphs are inspected, never traced by executing the body.
#include "camel/core/error/runtime.h"
#include "camel/core/mm/gen.h"
#include "camel/core/module/builtin.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "modules/tensor/ops/registry.h"
#include "modules/tensor/tensor.h"
#include "modules/tensor/type.h"
#include "tiles/program.h"
#include <cstring>

namespace camel::tiles {
namespace {
using namespace core::type;
using namespace core::context;
using namespace core::module;
namespace td = core::rtdata;

template <class F> slot_t checked(F &&body) {
    try {
        return body();
    } catch (const std::exception &e) {
        core::error::throwRuntimeFault(
            core::error::RuntimeDiag::RuntimeError,
            std::string("tiles: ") + e.what());
    }
}

Program prepare(ArgsView &with, ArgsView &norm) {
    // Only extraction holds GC pointers. CUDA work uses owned host snapshots and releases
    // them after execution; the cache never retains Camel objects or shape-only identities.
    core::mm::NoCollectionScope noCollection;
    auto *fn = with.get<::Function *>(0);
    std::vector<Argument> args;
    if (fn->graph()->normPorts().size() == 1) {
        args.push_back({norm.slot(0), norm.type(0)});
    } else {
        auto *tuple = norm.get<::Tuple *>(0);
        auto *type  = static_cast<TupleType *>(norm.type(0));
        for (size_t i = 0; i < type->size(); ++i)
            args.push_back({tuple->get<slot_t>(i), type->typeAt(i)});
    }
    return compile(fn, args);
}

slot_t sourceKernel(ArgsView &with, ArgsView &norm, Context &) {
    return checked([&] {
        auto program = prepare(with, norm);
        return td::toSlot(::String::from(program.source, core::mm::autoSpace()));
    });
}
slot_t inspectKernel(ArgsView &with, ArgsView &norm, Context &) {
    return checked([&] {
        return td::toSlot(::String::from(inspect(prepare(with, norm)), core::mm::autoSpace()));
    });
}
slot_t runKernel(ArgsView &with, ArgsView &norm, Context &) {
    return checked([&] {
        auto program = prepare(with, norm);
        auto data    = execute(program);
        auto *out    = tensor::TensorObject::create(
            TypeCode::Float32,
            program.nodes[program.output].shape,
            core::mm::autoSpace());
        if (!data.empty())
            std::memcpy(out->rawData(), data.data(), data.size() * sizeof(float));
        return td::toSlot(static_cast<td::Object *>(out));
    });
}
slot_t availableKernel(ArgsView &, ArgsView &, Context &) { return td::toSlot(available()); }
slot_t deviceKernel(ArgsView &, ArgsView &, Context &) {
    return checked([] { return td::toSlot(::String::from(deviceInfo(), core::mm::autoSpace())); });
}

std::optional<Type *> resolve(const type_vec_t &with, const type_vec_t &norm, bool source) {
    if (with.size() != 1 || norm.size() != 1 || with[0]->code() != TypeCode::Function)
        return std::nullopt;
    const auto *fn = static_cast<FunctionType *>(with[0]);
    if (!tensor::asTensorType(fn->exitType()) || fn->normTypesCount() == 0)
        return std::nullopt;
    type_vec_t args;
    if (fn->normTypesCount() == 1)
        args = {norm[0]};
    else if (norm[0]->code() == TypeCode::Tuple) {
        auto *tuple = static_cast<TupleType *>(norm[0]);
        for (size_t i = 0; i < tuple->size(); ++i)
            args.push_back(tuple->typeAt(i));
    }
    if (args.size() != fn->normTypesCount())
        return std::nullopt;
    for (size_t i = 0; i < args.size(); ++i) {
        if (!tensor::asTensorType(args[i]) || fn->normIsVarAt(i) ||
            !fn->normTypeAt(i)->assignableFrom(args[i]))
            return std::nullopt;
    }
    return source ? Type::String() : fn->exitType();
}

class TilesModule final : public BuiltinModule {
  public:
    explicit TilesModule(context_ptr_t ctx) : BuiltinModule("tiles", ctx) {
        tensor::ops::registerTensorOps();
        for (const std::string name : {"run", "source", "inspect"}) {
            exportEntity(
                name,
                OperatorGroup::create(
                    name,
                    {{"tiles:" + name,
                      DynamicFuncTypeResolver::create(
                          {{1, {false}}, {1, {false}}},
                          "<fn: (...) => Tensor>(inputs: Tensor | tuple) => " +
                              std::string(name != "run" ? "string" : "Tensor"),
                          [source = name != "run"](
                              const type_vec_t &with,
                              const type_vec_t &norm,
                              const ModifierSet &) { return resolve(with, norm, source); })}}));
        }
        exportEntity(
            "available",
            OperatorGroup::create(
                "available",
                {{"tiles:available", StaticFuncTypeResolver::create({}, {}, Type::Bool())}}));
        exportEntity(
            "device",
            OperatorGroup::create(
                "device",
                {{"tiles:device", StaticFuncTypeResolver::create({}, {}, Type::String())}}));
    }
    bool load() override {
        if (loaded_)
            return true;
        context_->registerExecutorFactory("tiles", [ctx = context_]() -> executor_ptr_t {
            return std::make_shared<Executor>(
                ctx,
                std::unordered_map<std::string, operator_t>{
                    {"run", &runKernel},
                    {"source", &sourceKernel},
                    {"inspect", &inspectKernel},
                    {"available", &availableKernel},
                    {"device", &deviceKernel}});
        });
        // run may initialize a driver/compile code; never execute it in constant folding.
        core::OperatorTable::instance().declare(
            "tiles",
            {{"run", false},
             {"source", false},
             {"inspect", false},
             {"available", false},
             {"device", false}});
        loaded_ = true;
        return true;
    }
};
} // namespace
} // namespace camel::tiles

extern "C" camel::core::module::Module *camel_module_create(camel::core::context::Context *ctx) {
    return new camel::tiles::TilesModule(ctx->shared_from_this());
}
