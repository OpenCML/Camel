/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Public module boundary: function graphs are inspected, never traced by executing the body.
#include "camel/core/error/runtime.h"
#include "camel/core/mm/gen.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/module/builtin.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/core/type/tree.h"
#include "camel/execute/executor.h"
#include "camel/execute/function_call.h"
#include "camel/profile/profile.h"
#include "camel/utils/log.h"
#include "modules/tensor/ops/registry.h"
#include "modules/tensor/tensor.h"
#include "modules/tensor/type.h"
#include "tiles/device.h"
#include "tiles/internal/capability.h"
#include "tiles/internal/compiled_region.h"
#include "tiles/internal/dispatch.h"
#include "tiles/internal/graph_values.h"
#include "tiles/internal/profile_hooks.h"
#include "tiles/internal/regions.h"
#include "tiles/internal/tree_transfer.h"
#include "tiles/program.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace camel::tiles {
namespace {
using namespace core::type;
using namespace core::context;
using namespace core::module;
namespace td = core::rtdata;

template <class F> slot_t checked(F &&body) {
    try {
        return body();
    } catch (const DeviceUnavailable &e) {
        // The run named this device (`--devices`); it is missing, so the report names it rather
        // than moving the work to the host.
        core::error::throwRuntimeFault(
            core::error::RuntimeDiag::DeviceUnavailable,
            std::to_string(e.ordinal()),
            e.what());
    } catch (const std::exception &e) {
        core::error::throwRuntimeFault(
            core::error::RuntimeDiag::RuntimeError,
            std::string("tiles: ") + e.what());
    }
}

Program prepare(ArgsView &with, ArgsView &norm, Context &ctx, bool staticRegion = false) {
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
    return prepareProgram(fn, args, ctx.facts(), staticRegion);
}

slot_t regionsKernel(ArgsView &with, ArgsView &, Context &ctx) {
    return checked([&] {
        core::mm::NoCollectionScope noCollection;
        auto text = inspectRegions(
            *with.get<::Function *>(0)->program(),
            *with.get<::Function *>(0)->graph(),
            ctx.facts());
        return td::toSlot(::String::from(text, core::mm::autoSpace()));
    });
}

slot_t sourceKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    return checked([&] {
        auto program = prepare(with, norm, ctx);
        return td::toSlot(::String::from(program.source, core::mm::autoSpace()));
    });
}
slot_t inspectKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    return checked([&] {
        return td::toSlot(::String::from(inspect(prepare(with, norm, ctx)), core::mm::autoSpace()));
    });
}
slot_t benchmarkKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    return checked([&] {
        return td::toSlot(
            ::String::from(
                benchmark(std::make_shared<const Program>(prepare(with, norm, ctx))),
                core::mm::autoSpace()));
    });
}
slot_t runPrepared(ArgsView &with, std::shared_ptr<const Program> program) {
    auto data = execute(program);
    // Prevent collection of partially assembled aggregates while allocating leaves.
    core::mm::NoCollectionScope noCollection;
    size_t leaf = 0, offset = 0;
    auto rebuild = [&](auto &&self, Type *type) -> slot_t {
        if (type->code() == TypeCode::Float64) {
            ++leaf;
            double value;
            std::memcpy(&value, data.data() + offset, sizeof(value));
            offset += sizeof(value);
            return td::toSlot(value);
        }
        if (tensor::asTensorType(type)) {
            const auto &shape  = program->nodes[program->outputs.at(leaf++)].shape;
            const size_t count = elements(shape);
            auto *out = tensor::createTensor(TypeCode::Float32, shape, core::mm::autoSpace());
            if (count)
                std::memcpy(out->rawData(), data.data() + offset, count * sizeof(float));
            offset += count * sizeof(float);
            return td::toSlot(static_cast<td::Object *>(out));
        }
        if (type->code() == TypeCode::Tuple) {
            auto *types = static_cast<TupleType *>(type);
            auto *tuple = ::Tuple::create(types->size(), core::mm::autoSpace());
            for (size_t i = 0; i < types->size(); ++i)
                tuple->set(i, self(self, types->typeAt(i)), types);
            return td::toSlot(tuple);
        }
        auto *types  = static_cast<StructType *>(type);
        auto *object = ::Struct::create(types->size(), core::mm::autoSpace());
        for (size_t i = 0; i < types->size(); ++i)
            object->set(i, self(self, types->typeAt(i)), types);
        return td::toSlot(object);
    };
    return rebuild(rebuild, static_cast<FunctionType *>(with.type(0))->exitType());
}
slot_t runKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    return checked([&] {
        return runPrepared(with, std::make_shared<const Program>(prepare(with, norm, ctx)));
    });
}
slot_t runCompiledKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    return checked([&] {
        return runPrepared(with, std::make_shared<const Program>(prepare(with, norm, ctx, true)));
    });
}
slot_t preferGpuKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::shared_ptr<const Program> program;
    const bool supported = checked([&] {
        // `--devices cpu`: the host path, exactly as when no device is available.
        if (!configuredCudaOrdinal())
            return false;
        try {
            (void)deviceInfo();
            program = std::make_shared<const Program>(prepare(with, norm, ctx));
            return true;
        } catch (const UnsupportedComputation &) {
            return false;
        } catch (const BackendUnavailable &) {
            return false;
        }
    });
    if (supported)
        return checked([&] { return runPrepared(with, std::move(program)); });
    // No device work has executed. Keep the original function and arguments rooted in
    // their caller frame; the backend binds them before reaching a safepoint.
    auto *function = with.get<::Function *>(0);
    std::vector<slot_t> arguments;
    if (function->graph()->normPorts().size() == 1) {
        arguments.push_back(norm.slot(0));
    } else {
        auto *tuple = norm.get<::Tuple *>(0);
        for (size_t i = 0; i < function->graph()->normPorts().size(); ++i)
            arguments.push_back(tuple->get<slot_t>(i));
    }
    return execute::invokeFunction(ctx, function, {}, arguments);
}
slot_t availableKernel(ArgsView &, ArgsView &, Context &) {
    // checked, so that a device the run named and that is missing is a diagnostic rather than
    // "no device here": `available()` answers a capability question, not a configuration one.
    return checked([] { return td::toSlot(available()); });
}
slot_t gpuKernel(ArgsView &, ArgsView &norm, Context &ctx);
slot_t wrapDevice(std::shared_ptr<DeviceValue> resident) {
    auto *owner = new std::shared_ptr<DeviceValue>(std::move(resident));
    td::ForeignResourceDescriptor descriptor;
    descriptor.kind    = "tiles.Device";
    descriptor.deleter = [](void *p) { delete static_cast<std::shared_ptr<DeviceValue> *>(p); };
    return td::toSlot(
        td::ForeignResourceObject::create(core::mm::autoSpace(), std::move(descriptor), owner));
}
slot_t runResidentPrepared(ArgsView &with, std::shared_ptr<const Program> program) {
    return checked([&] {
        auto values = executeResident(std::move(program));
        core::mm::NoCollectionScope noCollection;
        auto *resultType = residentType(static_cast<FunctionType *>(with.type(0))->exitType());
        size_t leaf      = 0;
        auto rebuild     = [&](auto &&self, Type *type) -> slot_t {
            if (asDeviceType(type))
                return wrapDevice(values.at(leaf++));
            if (type->code() == TypeCode::Tuple) {
                auto *types = static_cast<TupleType *>(type);
                auto *out   = ::Tuple::create(types->size(), core::mm::autoSpace());
                for (size_t i = 0; i < types->size(); ++i)
                    out->set(i, self(self, types->typeAt(i)), types);
                return td::toSlot(out);
            }
            auto *types = static_cast<StructType *>(type);
            auto *out   = ::Struct::create(types->size(), core::mm::autoSpace());
            for (size_t i = 0; i < types->size(); ++i)
                out->set(i, self(self, types->typeAt(i)), types);
            return td::toSlot(out);
        };
        return rebuild(rebuild, resultType);
    });
}
slot_t runDeviceKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::shared_ptr<const Program> program;
    checked([&] {
        program = std::make_shared<const Program>(prepare(with, norm, ctx));
        return slot_t{};
    });
    return runResidentPrepared(with, std::move(program));
}
slot_t runCompiledDeviceKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::shared_ptr<const Program> program;
    checked([&] {
        program = std::make_shared<const Program>(prepare(with, norm, ctx, true));
        return slot_t{};
    });
    return runResidentPrepared(with, std::move(program));
}
slot_t uploadKernel(ArgsView &, ArgsView &norm, Context &) {
    return checked([&] { return transferTree(norm, true); });
}
slot_t downloadKernel(ArgsView &, ArgsView &norm, Context &) {
    return checked([&] { return transferTree(norm, false); });
}
// Called only while collection is inhibited; aggregates are rebuilt with exact types.
slot_t hostResidentValue(Type *hostType, slot_t value, bool retain) {
    if (hostType->code() == TypeCode::Tuple || hostType->code() == TypeCode::Struct) {
        const bool tuple   = hostType->code() == TypeCode::Tuple;
        const size_t count = tuple ? static_cast<TupleType *>(hostType)->size()
                                   : static_cast<StructType *>(hostType)->size();
        auto *outType      = retain ? residentType(hostType) : hostType;
        if (tuple) {
            auto *input  = td::fromSlot<::Tuple *>(value);
            auto *output = ::Tuple::create(count, core::mm::autoSpace());
            for (size_t i = 0; i < count; ++i)
                output->set(
                    i,
                    hostResidentValue(
                        static_cast<TupleType *>(hostType)->typeAt(i),
                        input->get<slot_t>(i),
                        retain),
                    static_cast<TupleType *>(outType));
            return td::toSlot(output);
        }
        auto *input  = td::fromSlot<::Struct *>(value);
        auto *output = ::Struct::create(count, core::mm::autoSpace());
        for (size_t i = 0; i < count; ++i)
            output->set(
                i,
                hostResidentValue(
                    static_cast<StructType *>(hostType)->typeAt(i),
                    input->get<slot_t>(i),
                    retain),
                static_cast<StructType *>(outType));
        return td::toSlot(output);
    }
    if (retain) {
        if (hostType->code() == TypeCode::Float64) {
            const double scalar = td::fromSlot<double>(value);
            return wrapDevice(
                retainHostValue({}, DType::F64, std::as_bytes(std::span(&scalar, 1))));
        }
        auto *tensor = td::fromSlot<tensor::Array *>(value);
        if (tensor->dtype() != TypeCode::Float32)
            throw std::runtime_error("host resident tensors require f32 elements");
        const auto shape = tensor->shapeVector();
        return wrapDevice(retainHostValue(
            shape,
            DType::F32,
            std::as_bytes(std::span(tensor->dataAs<float>(), elements(shape)))));
    }
    auto *wrapper       = td::fromSlot<td::ForeignResourceObject *>(value);
    const auto resident = *wrapper->resourceAs<std::shared_ptr<DeviceValue>>();
    const auto data     = downloadValue(*resident);
    if (hostType->code() == TypeCode::Float64) {
        if (resident->dtype != DType::F64 || data.size() != sizeof(double))
            throw std::runtime_error("resident scalar type mismatch");
        double scalar;
        std::memcpy(&scalar, data.data(), sizeof(scalar));
        return td::toSlot(scalar);
    }
    if (resident->dtype != DType::F32)
        throw std::runtime_error("resident tensor type mismatch");
    auto *tensor = tensor::createTensor(TypeCode::Float32, resident->shape, core::mm::autoSpace());
    if (!data.empty())
        std::memcpy(tensor->rawData(), data.data(), data.size());
    return td::toSlot(tensor);
}
slot_t retainHostKernel(ArgsView &, ArgsView &norm, Context &) {
    return checked([&] {
        core::mm::NoCollectionScope noCollection;
        return hostResidentValue(norm.type(0), norm.slot(0), true);
    });
}
slot_t runHostResidentKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    auto *type = static_cast<FunctionType *>(with.type(0));
    core::mm::RootHandle root(core::mm::autoSpace(), "tiles.host.arguments");
    std::vector<slot_t> arguments;
    checked([&] {
        core::mm::NoCollectionScope noCollection;
        type_vec_t fields;
        for (size_t i = 0; i < type->normTypesCount(); ++i)
            fields.push_back(type->normTypeAt(i));
        auto *tupleType = TupleType::create(fields);
        auto *tuple     = ::Tuple::create(fields.size(), core::mm::autoSpace());
        root.reset(tuple, tupleType);
        for (size_t i = 0; i < fields.size(); ++i) {
            const auto value =
                fields.size() == 1 ? norm.slot(0) : norm.get<::Tuple *>(0)->get<slot_t>(i);
            const auto host = hostResidentValue(fields[i], value, false);
            tuple->set(i, host, tupleType);
            arguments.push_back(host);
        }
        return slot_t{};
    });
    // Binding precedes the callee's first safepoint. Do not suppress GC through
    // graph execution, and do not translate/replay faults from the original graph.
    const auto result = execute::invokeFunction(ctx, with.get<::Function *>(0), {}, arguments);
    return checked([&] {
        core::mm::NoCollectionScope noCollection;
        return hostResidentValue(type->exitType(), result, true);
    });
}
slot_t retainPreferredKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    const bool gpu = checked([&] {
        if (!configuredCudaOrdinal())
            return false;
        try {
            (void)deviceInfo();
            return true;
        } catch (const BackendUnavailable &) {
            return false;
        }
    });
    return gpu ? uploadKernel(with, norm, ctx) : retainHostKernel(with, norm, ctx);
}
slot_t runPreferredResidentKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::shared_ptr<Program> program;
    const bool gpu = checked([&] {
        if (!configuredCudaOrdinal())
            return false;
        try {
            (void)deviceInfo();
            program = std::make_shared<Program>(prepare(with, norm, ctx, true));
            return true;
        } catch (const BackendUnavailable &) {
            return false;
        } catch (const UnsupportedComputation &) {
            return false;
        }
    });
    if (!gpu)
        return runHostResidentKernel(with, norm, ctx);
    // Selection is complete before transfers. Transfer/compile/launch faults
    // propagate; none trigger replay of the graph on another target.
    checked([&] {
        std::unordered_map<const DeviceValue *, std::shared_ptr<const DeviceValue>> placed;
        for (auto &node : program->nodes)
            if (node.resident) {
                const auto *key = node.resident.get();
                auto found      = placed.find(key);
                if (found == placed.end())
                    found = placed.emplace(key, placeOnDevice(node.resident)).first;
                node.resident = found->second;
            }
        return slot_t{};
    });
    return runResidentPrepared(with, std::move(program));
}
slot_t gpuKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    return checked([&] {
        return td::toSlot(gpuFunction(
            norm.get<::Function *>(0),
            &preferGpuKernel,
            ctx,
            {&retainPreferredKernel,
             &runPreferredResidentKernel,
             &downloadKernel,
             "tiles:retain_preferred",
             "tiles:run_preferred_resident",
             "tiles:download"}));
    });
}
slot_t deviceKernel(ArgsView &, ArgsView &, Context &) {
    return checked([] { return td::toSlot(::String::from(deviceInfo(), core::mm::autoSpace())); });
}

bool supportedLeaves(Type *type) {
    return !core::type::tree::firstRejectedLeaf(type, [](Type *leaf) {
        return leaf->code() == TypeCode::Float64 || tensor::asTensorType(leaf) != nullptr;
    });
}
// Placement changes storage, not the computation function's tensor signature.
bool acceptsInput(Type *expected, Type *actual) {
    if (const auto *device = asDeviceType(actual))
        return expected->assignableFrom(device->hostType());
    if (actual->code() == TypeCode::Tuple && expected->code() == TypeCode::Tuple) {
        auto *a = static_cast<TupleType *>(actual);
        auto *e = static_cast<TupleType *>(expected);
        if (a->size() != e->size())
            return false;
        for (size_t i = 0; i < a->size(); ++i)
            if (!acceptsInput(e->typeAt(i), a->typeAt(i)))
                return false;
        return true;
    }
    if (actual->code() == TypeCode::Struct && expected->code() == TypeCode::Struct) {
        auto *a = static_cast<StructType *>(actual);
        auto *e = static_cast<StructType *>(expected);
        if (a->size() != e->size())
            return false;
        for (size_t i = 0; i < a->size(); ++i)
            if (a->fieldName(i) != e->fieldName(i) || !acceptsInput(e->typeAt(i), a->typeAt(i)))
                return false;
        return true;
    }
    return supportedLeaves(actual) && expected->assignableFrom(actual);
}
std::optional<Type *> resolve(const type_vec_t &with, const type_vec_t &norm, bool source) {
    if (with.size() != 1 || norm.size() != 1 || with[0]->code() != TypeCode::Function)
        return std::nullopt;
    const auto *fn = static_cast<FunctionType *>(with[0]);
    if (!supportedLeaves(fn->exitType()) || fn->normTypesCount() == 0)
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
        if (fn->normIsVarAt(i) || !acceptsInput(fn->normTypeAt(i), args[i]))
            return std::nullopt;
    }
    return source ? Type::String() : fn->exitType();
}

// The operator groups whose signature the module computes from an argument; a declaration cannot
// write any of them (standard library conventions 1-2; handover H156). `gpu` returns the type of
// its function argument; `upload` and `download` map every leaf of their argument into or out of
// the `Device<...>` handle; the run/query groups take the function's parameter list as their
// ordinary parameter (`run` also answers its result type); `regions` accepts any function value.
// `stdlib/signatures/tiles.cml` records their reference entries, and the two fixed signatures
// (`available`, `device`) are declared in `stdlib/tiles.cml`, so no group here is a declaration's
// duplicate.
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups{
        OperatorGroup::create(
            "gpu",
            {{"tiles:gpu",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(fn: Function) => Function",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (!with.empty() || norm.size() != 1 ||
                          norm[0]->code() != TypeCode::Function)
                          return std::nullopt;
                      auto *fn = static_cast<FunctionType *>(norm[0]);
                      if (fn->withTypesCount() || !fn->normTypesCount() ||
                          !supportedLeaves(fn->exitType()))
                          return std::nullopt;
                      for (size_t i = 0; i < fn->normTypesCount(); ++i)
                          if (fn->normIsVarAt(i) || !supportedLeaves(fn->normTypeAt(i)))
                              return std::nullopt;
                      return fn;
                  })}}),
        OperatorGroup::create(
            "upload",
            {{"tiles:upload",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(T: Tensor, float, struct or tuple) => Device<T>",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (!with.empty() || norm.size() != 1)
                          return std::nullopt;
                      auto *result = transferTreeType(norm[0], true);
                      return result ? std::optional<Type *>(result) : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "download",
            {{"tiles:download",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(Device<T>) => T",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (!with.empty() || norm.size() != 1)
                          return std::nullopt;
                      auto *result = transferTreeType(norm[0], false);
                      return result ? std::optional<Type *>(result) : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "run",
            {{"tiles:run",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {1, {false}}},
                  "<fn: (...) => NumericAggregate>(inputs: NumericAggregate) => NumericAggregate",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolve(with, norm, false); })}}),
        OperatorGroup::create(
            "run_device",
            {{"tiles:run_device",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {1, {false}}},
                  "<fn: (...) => NumericAggregate>(inputs: NumericAggregate) => DeviceAggregate",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      auto result = resolve(with, norm, false);
                      if (!result)
                          return result;
                      auto *type = residentType(*result);
                      return type ? std::optional<Type *>(type) : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "run_preferred_resident",
            {{"tiles:run_preferred_resident",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {1, {false}}},
                  "<fn: (...) => NumericAggregate>(inputs: NumericAggregate) => DeviceAggregate",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      auto result = resolve(with, norm, false);
                      if (!result)
                          return result;
                      auto *type = residentType(*result);
                      return type ? std::optional<Type *>(type) : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "source",
            {{"tiles:source",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {1, {false}}},
                  "<fn: (...) => NumericAggregate>(inputs: NumericAggregate) => string",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolve(with, norm, true); })}}),
        OperatorGroup::create(
            "inspect",
            {{"tiles:inspect",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {1, {false}}},
                  "<fn: (...) => NumericAggregate>(inputs: NumericAggregate) => string",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolve(with, norm, true); })}}),
        OperatorGroup::create(
            "benchmark",
            {{"tiles:benchmark",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {1, {false}}},
                  "<fn: (...) => NumericAggregate>(inputs: NumericAggregate) => string",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> { return resolve(with, norm, true); })}}),
        OperatorGroup::create(
            "regions",
            {{"tiles:regions",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {0, {}}},
                  "<fn: Function>() => string",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (with.size() != 1 || !norm.empty() ||
                          with[0]->code() != TypeCode::Function)
                          return std::nullopt;
                      return Type::String();
                  })}})};
    return groups;
}

class TilesModule final : public BuiltinModule {
  public:
    explicit TilesModule(context_ptr_t ctx) : BuiltinModule("tiles", ctx) {
        tensor::ops::registerTensorOps();
        registerRegionPass(
            {&retainPreferredKernel,
             &runPreferredResidentKernel,
             &downloadKernel,
             "tiles:retain_preferred",
             "tiles:run_preferred_resident",
             "tiles:download"});
        registerRegionTestPasses(
            &runCompiledKernel,
            {&uploadKernel,
             &runCompiledDeviceKernel,
             &downloadKernel,
             "tiles:upload",
             "tiles:run_compiled_device",
             "tiles:download"},
            {&retainHostKernel,
             &runHostResidentKernel,
             &downloadKernel,
             "tiles:retain_host",
             "tiles:run_host_resident",
             "tiles:download"},
            {&retainPreferredKernel,
             &runPreferredResidentKernel,
             &downloadKernel,
             "tiles:retain_preferred",
             "tiles:run_preferred_resident",
             "tiles:download"});
        exportType(Reference("Device"), DeviceType::get(tensor::TensorType::Default()));
        // `available` and `device` are declared in stdlib/tiles.cml (handover H156): their
        // signatures are fixed, so the declaration is their one source and C++ exports no group
        // for them. The ten groups behind `getOperatorGroups()` compute their signature from an
        // argument and remain the module's own; the reference entries are in
        // stdlib/signatures/tiles.cml.
        for (const auto &group : getOperatorGroups())
            exportEntity(group->name(), group);
    }
    bool load() override {
        if (loaded_)
            return true;
        // The one device probe of the process, reported here because the device sources are built
        // without libcamel as well (see capability.h). The H117 plan counts this line: however
        // many calls prefer a device, there must be one.
        deviceProbeStarted = [] {
            CAMEL_LOG_INFO_S("tiles", "probe: constructing the CUDA backend");
        };
        // `std::profile` (camel/profile/profile.h): what the CUDA path of this module did. Each
        // hook asks the session itself, so a run without the pass records nothing.
        profile_hooks::kernel = [](const char *label) {
            if (camel::profile::enabled()) {
                camel::profile::kernelLaunch(label);
            }
        };
        profile_hooks::toDevice = [](uint64_t bytes) {
            if (camel::profile::enabled()) {
                camel::profile::transferToDevice(bytes);
            }
        };
        profile_hooks::toHost = [](uint64_t bytes) {
            if (camel::profile::enabled()) {
                camel::profile::transferToHost(bytes);
            }
        };
        profile_hooks::synchronise = [](const char *what) {
            if (camel::profile::enabled()) {
                camel::profile::synchronise(what);
            }
        };
        profile_hooks::deviceCompile = [](const char *what, uint64_t ns) {
            if (camel::profile::enabled()) {
                camel::profile::deviceCompile(what, ns);
            }
        };
        profile_hooks::device = [](const char *name) {
            if (camel::profile::enabled()) {
                camel::profile::setDevice(name);
            }
        };
        context_->registerExecutorFactory("tiles", [ctx = context_]() -> executor_ptr_t {
            return std::make_shared<Executor>(
                ctx,
                std::unordered_map<std::string, operator_t>{
                    {"run", &runKernel},
                    {"run_compiled", &runCompiledKernel},
                    {"run_compiled_device", &runCompiledDeviceKernel},
                    {"prefer_gpu", &preferGpuKernel},
                    {"test_call", &testNativeCall},
                    {"test_compiled_call", &testCompiledCall},
                    {"gpu", &gpuKernel},
                    {"run_device", &runDeviceKernel},
                    {"retain_host", &retainHostKernel},
                    {"retain_preferred", &retainPreferredKernel},
                    {"run_preferred_resident", &runPreferredResidentKernel},
                    {"run_host_resident", &runHostResidentKernel},
                    {"upload", &uploadKernel},
                    {"download", &downloadKernel},
                    {"source", &sourceKernel},
                    {"inspect", &inspectKernel},
                    {"regions", &regionsKernel},
                    {"benchmark", &benchmarkKernel},
                    {"available", &availableKernel},
                    {"device", &deviceKernel}});
        });
        core::OperatorTable::instance().set("tiles:retain_host", {.pure = false});
        core::OperatorTable::instance().set(
            "tiles:run_host_resident",
            {.pure = false, .callsGraphs = true, .readsGraphs = true});
        core::OperatorTable::instance().set("tiles:retain_preferred", {.pure = false});
        core::OperatorTable::instance().set(
            "tiles:run_preferred_resident",
            {.pure = false, .callsGraphs = true, .readsGraphs = true});
        // run may initialize a driver/compile code; never execute it in constant folding.
        core::OperatorTable::instance().declare(
            "tiles",
            {{"run", false},
             {"gpu", false},
             {"run_device", false},
             {"upload", false},
             {"download", false},
             {"source", false},
             {"inspect", false},
             {"regions", false},
             {"benchmark", false},
             {"available", false},
             {"device", false}});
        // These compile the graph of the function value they receive: a back end that runs a
        // private copy of the program with its loops lowered hands them the program's own
        // graph, LOOP nodes included (OperatorTraits::readsGraphs; op-semantics 11.4).
        for (const char *name :
             {"run",
              "run_device",
              "run_compiled",
              "run_compiled_device",
              "source",
              "inspect",
              "benchmark",
              "regions",
              "gpu"}) {
            core::OperatorTable::instance().set(
                std::string("tiles:") + name,
                {.pure = false, .readsGraphs = true});
        }
        core::OperatorTable::instance().set(
            "tiles:prefer_gpu",
            {.pure = false, .callsGraphs = true, .readsGraphs = true});
        for (const char *name : {"tiles:run_compiled", "tiles:run_compiled_device"})
            core::OperatorTable::instance().set(
                name,
                {.pure = false, .callsGraphs = true, .readsGraphs = true});
        // What each touches (docs/design/op-semantics 8, 14.4): device memory is a foreign
        // resource `tiles:device` (upload makes one; run and run_device read the device values
        // among their inputs, run_device returns new ones that may be inputs again); the driver
        // and the device are ambient state `tiles:driver`. Every operator is an effect outside
        // the program, as its traits say.
        {
            using namespace core::sem;
            const Resource device{.cls = ResourceClass::Foreign, .name = "tiles:device"};
            const auto readsDevice = [&] {
                return operandAccess(ResourceClass::Foreign, 0, AccessMode::Read, device.name);
            };
            const auto driver = [](AccessMode mode) { return ambientAccess("tiles:driver", mode); };
            core::OperatorTable::instance().declareResources(
                "tiles",
                {
                    {"upload",
                     ResourceSpec{
                         .accesses       = {driver(AccessMode::ReadModifyWrite)},
                         .allocates      = device,
                         .externalEffect = true}},
                    {"download",
                     ResourceSpec{
                         .accesses       = {readsDevice(), driver(AccessMode::ReadModifyWrite)},
                         .externalEffect = true}},
                    {"run",
                     ResourceSpec{
                         .accesses       = {readsDevice(), driver(AccessMode::ReadModifyWrite)},
                         .externalEffect = true}},
                    {"run_compiled",
                     ResourceSpec{
                         .accesses       = {readsDevice(), driver(AccessMode::ReadModifyWrite)},
                         .externalEffect = true}},
                    {"run_device",
                     ResourceSpec{
                         .accesses = {readsDevice(), driver(AccessMode::ReadModifyWrite)},
                         .result   = ResultAlias{.kind = ResultAlias::Kind::MayAlias, .operand = 0},
                         .allocates      = device,
                         .externalEffect = true}},
                    {"run_compiled_device",
                     ResourceSpec{
                         .accesses = {readsDevice(), driver(AccessMode::ReadModifyWrite)},
                         .result   = ResultAlias{.kind = ResultAlias::Kind::MayAlias, .operand = 0},
                         .allocates      = device,
                         .externalEffect = true}},
                    {"benchmark",
                     ResourceSpec{
                         .accesses       = {readsDevice(), driver(AccessMode::ReadModifyWrite)},
                         .externalEffect = true}},
                    {"source", externalAmbient("tiles:driver", AccessMode::Read)},
                    {"inspect", externalAmbient("tiles:driver", AccessMode::Read)},
                    {"available", externalAmbient("tiles:driver", AccessMode::ReadModifyWrite)},
                    {"device", externalAmbient("tiles:driver", AccessMode::ReadModifyWrite)},
                });
        }
        loaded_ = true;
        return true;
    }
};
} // namespace
} // namespace camel::tiles

extern "C" camel::core::module::Module *camel_module_create(camel::core::context::Context *ctx) {
    return new camel::tiles::TilesModule(ctx->shared_from_this());
}
