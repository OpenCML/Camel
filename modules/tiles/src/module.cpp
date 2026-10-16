/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Public module boundary: function graphs are inspected, never traced by executing the body.
#include "camel/core/error/runtime.h"
#include "camel/core/mm/gen.h"
#include "camel/core/module/builtin.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "modules/tensor/ops/registry.h"
#include "modules/tensor/tensor.h"
#include "modules/tensor/type.h"
#include "tiles/device.h"
#include "tiles/internal/dispatch.h"
#include "tiles/internal/regions.h"
#include "tiles/program.h"
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
    } catch (const std::exception &e) {
        core::error::throwRuntimeFault(
            core::error::RuntimeDiag::RuntimeError,
            std::string("tiles: ") + e.what());
    }
}

Program prepare(ArgsView &with, ArgsView &norm, Context &ctx) {
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
    return compile(fn, args, ctx.facts());
}

slot_t regionsKernel(ArgsView &with, ArgsView &, Context &ctx) {
    return checked([&] {
        core::mm::NoCollectionScope noCollection;
        auto text = inspectRegions(*with.get<::Function *>(0)->graph(), ctx.facts());
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
slot_t runKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    return checked([&] {
        auto program = std::make_shared<const Program>(prepare(with, norm, ctx));
        auto data    = execute(program);
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
                auto *out =
                    tensor::TensorObject::create(TypeCode::Float32, shape, core::mm::autoSpace());
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
    });
}
slot_t availableKernel(ArgsView &, ArgsView &, Context &) { return td::toSlot(available()); }
slot_t gpuKernel(ArgsView &, ArgsView &norm, Context &) {
    return checked([&] { return td::toSlot(gpuFunction(norm.get<::Function *>(0), &runKernel)); });
}
slot_t wrapDevice(std::shared_ptr<DeviceValue> resident) {
    auto *owner = new std::shared_ptr<DeviceValue>(std::move(resident));
    td::ForeignResourceDescriptor descriptor;
    descriptor.kind    = "tiles.Device";
    descriptor.deleter = [](void *p) { delete static_cast<std::shared_ptr<DeviceValue> *>(p); };
    return td::toSlot(
        td::ForeignResourceObject::create(core::mm::autoSpace(), std::move(descriptor), owner));
}
Type *buildResidentType(Type *type) {
    if (tensor::asTensorType(type) || type->code() == TypeCode::Float64)
        return DeviceType::get(type);
    if (type->code() == TypeCode::Tuple) {
        auto *tuple = static_cast<TupleType *>(type);
        type_vec_t fields;
        for (size_t i = 0; i < tuple->size(); ++i) {
            auto *field = buildResidentType(tuple->typeAt(i));
            if (!field)
                return nullptr;
            fields.push_back(field);
        }
        return TupleType::create(fields);
    }
    if (type->code() == TypeCode::Struct) {
        auto *object = static_cast<StructType *>(type);
        StructTypeFactory fields;
        for (size_t i = 0; i < object->size(); ++i) {
            auto *field = buildResidentType(object->typeAt(i));
            if (!field)
                return nullptr;
            fields.add(std::string(object->fieldName(i)), field);
        }
        return fields.build();
    }
    return nullptr;
}
Type *residentType(Type *type) {
    static std::mutex mutex;
    static std::unordered_map<Type *, Type *> types;
    std::lock_guard lock(mutex);
    if (auto it = types.find(type); it != types.end())
        return it->second;
    auto *result = buildResidentType(type);
    types.emplace(type, result);
    return result;
}
slot_t runDeviceKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    return checked([&] {
        auto values = executeResident(std::make_shared<const Program>(prepare(with, norm, ctx)));
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
slot_t uploadKernel(ArgsView &, ArgsView &norm, Context &) {
    return checked([&] {
        if (norm.type(0)->code() == TypeCode::Float64) {
            double value = norm.get<double>(0);
            return wrapDevice(uploadValue({}, DType::F64, std::as_bytes(std::span(&value, 1))));
        }
        Shape shape;
        std::vector<float> data;
        {
            core::mm::NoCollectionScope noCollection;
            auto *tensor = norm.get<tensor::TensorObject *>(0);
            if (tensor->dtype() != TypeCode::Float32)
                throw std::runtime_error("upload currently requires f32 tensors");
            shape              = tensor->shapeVector();
            const size_t count = elements(shape);
            if (count)
                data.assign(tensor->dataAs<float>(), tensor->dataAs<float>() + count);
        }
        auto resident = uploadValue(shape, DType::F32, std::as_bytes(std::span(data)));
        return wrapDevice(std::move(resident));
    });
}
slot_t downloadKernel(ArgsView &, ArgsView &norm, Context &) {
    return checked([&] {
        auto *wrapper = norm.get<td::ForeignResourceObject *>(0);
        auto resident = *wrapper->resourceAs<std::shared_ptr<DeviceValue>>();
        auto data     = downloadValue(*resident);
        if (resident->dtype == DType::F64) {
            double value;
            std::memcpy(&value, data.data(), sizeof(value));
            return td::toSlot(value);
        }
        auto *out =
            tensor::TensorObject::create(TypeCode::Float32, resident->shape, core::mm::autoSpace());
        if (!data.empty())
            std::memcpy(out->rawData(), data.data(), data.size());
        return td::toSlot(out);
    });
}
slot_t deviceKernel(ArgsView &, ArgsView &, Context &) {
    return checked([] { return td::toSlot(::String::from(deviceInfo(), core::mm::autoSpace())); });
}

bool supportedLeaves(Type *type) {
    if (type->code() == TypeCode::Float64)
        return true;
    if (tensor::asTensorType(type))
        return true;
    if (type->code() == TypeCode::Tuple) {
        auto *tuple = static_cast<TupleType *>(type);
        if (!tuple->size())
            return false;
        for (size_t i = 0; i < tuple->size(); ++i)
            if (!supportedLeaves(tuple->typeAt(i)))
                return false;
        return true;
    }
    if (type->code() == TypeCode::Struct) {
        auto *object = static_cast<StructType *>(type);
        if (!object->size())
            return false;
        for (size_t i = 0; i < object->size(); ++i)
            if (!supportedLeaves(object->typeAt(i)))
                return false;
        return true;
    }
    return false;
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

class TilesModule final : public BuiltinModule {
  public:
    explicit TilesModule(context_ptr_t ctx) : BuiltinModule("tiles", ctx) {
        tensor::ops::registerTensorOps();
        registerRegionTestPasses();
        exportType(Reference("Device"), DeviceType::get(tensor::TensorType::Default()));
        exportEntity(
            "gpu",
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
                      })}}));
        for (const std::string name : {"upload", "download"}) {
            exportEntity(
                name,
                OperatorGroup::create(
                    name,
                    {{"tiles:" + name,
                      DynamicFuncTypeResolver::create(
                          {{0, {}}, {1, {false}}},
                          name == "upload" ? "(T: Tensor or float) => Device<T>"
                                           : "(Device<T>) => T",
                          [upload = name == "upload"](
                              const type_vec_t &with,
                              const type_vec_t &norm,
                              const ModifierSet &) -> std::optional<Type *> {
                              if (!with.empty() || norm.size() != 1)
                                  return std::nullopt;
                              if (upload) {
                                  if (norm[0]->code() == TypeCode::Float64)
                                      return DeviceType::get(norm[0]);
                                  const auto *tensor = tensor::asTensorType(norm[0]);
                                  if (!tensor ||
                                      (tensor->dtype() && *tensor->dtype() != TypeCode::Float32))
                                      return std::nullopt;
                                  return DeviceType::get(norm[0]);
                              }
                              const auto *device = asDeviceType(norm[0]);
                              return device ? std::optional<Type *>(device->hostType())
                                            : std::nullopt;
                          })}}));
        }
        for (const std::string name : {"run", "run_device", "source", "inspect", "benchmark"}) {
            exportEntity(
                name,
                OperatorGroup::create(
                    name,
                    {{"tiles:" + name,
                      DynamicFuncTypeResolver::create(
                          {{1, {false}}, {1, {false}}},
                          "<fn: (...) => NumericAggregate>(inputs: NumericAggregate) => " +
                              std::string(
                                  name == "run_device" ? "DeviceAggregate"
                                  : name != "run"      ? "string"
                                                       : "NumericAggregate"),
                          [source   = name != "run" && name != "run_device",
                           resident = name == "run_device"](
                              const type_vec_t &with,
                              const type_vec_t &norm,
                              const ModifierSet &) -> std::optional<Type *> {
                              auto result = resolve(with, norm, source);
                              if (!result || !resident)
                                  return result;
                              auto *type = residentType(*result);
                              return type ? std::optional<Type *>(type) : std::nullopt;
                          })}}));
        }
        exportEntity(
            "regions",
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
                      })}}));
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
                    {"gpu", &gpuKernel},
                    {"run_device", &runDeviceKernel},
                    {"upload", &uploadKernel},
                    {"download", &downloadKernel},
                    {"source", &sourceKernel},
                    {"inspect", &inspectKernel},
                    {"regions", &regionsKernel},
                    {"benchmark", &benchmarkKernel},
                    {"available", &availableKernel},
                    {"device", &deviceKernel}});
        });
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
        // graph, LOOP nodes included (OperatorTraits::inspectsGraphs; op-semantics 11.4).
        for (const char *name :
             {"run", "run_device", "source", "inspect", "benchmark", "regions", "gpu"}) {
            core::OperatorTable::instance().set(
                std::string("tiles:") + name,
                {.pure = false, .inspectsGraphs = true});
        }
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
                    {"run_device",
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
