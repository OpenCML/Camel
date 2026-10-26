// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Public module boundary: function graphs are inspected, never traced by executing the body.

#include "camel/core/error/runtime.h"
#include "camel/core/mm/gen.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/module/builtin.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/core/type/tree.h"
#include "camel/execute/devices.h"
#include "camel/execute/executor.h"
#include "camel/execute/function_call.h"
#include "camel/profile/profile.h"
#include "camel/utils/log.h"
#include "modules/tensor/ops/registry.h"
#include "modules/tensor/tensor.h"
#include "modules/tensor/type.h"
#include "tiles/device.h"
#include "tiles/internal/capability.h"
#include "tiles/internal/compiled_scope.h"
#include "tiles/internal/graph_values.h"
#include "tiles/internal/placement.h"
#include "tiles/internal/profile_hooks.h"
#include "tiles/internal/regions.h"
#include "tiles/internal/tree_transfer.h"
#include "tiles/program.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace camel::tiles {
namespace {
using namespace core::type;
using namespace core::context;
using namespace core::module;
namespace td = core::rtdata;

// What a kernel asks the device for, so a miss can name the operator the user called and the
// graph it was given. `unit` is that graph, when the ask was about one.
struct Ask {
    const char *oper;
    std::string unit;
};

template <class F> slot_t checked(Ask ask, F &&body) {
    try {
        return body();
    } catch (const DeviceUnavailable &e) {
        // The run named this device (`--devices`); it is missing, so the report names it rather
        // than moving the work to the host.
        core::error::throwRuntimeFault(
            core::error::RuntimeDiag::DeviceUnavailable,
            std::to_string(e.ordinal()),
            e.what());
    } catch (const UnsupportedComputation &e) {
        // A capability miss is a dispatch decision, not a fault (tiles/internal/capability.h):
        // the back end has no program for this graph, and whether a shape or the operator itself
        // is missing is what the miss says. `RuntimeError` would tell the user the runtime broke.
        // No `#gpu` scope is claimed here: the graph may be one no tag covers, or one segment of
        // a scope the pass has cut. The diagnostic a scope's own promise gets at compile time is
        // `GpuScopeNoDeviceProgram`.
        core::error::throwRuntimeFault(
            core::error::RuntimeDiag::GraphNoDeviceProgram,
            ask.oper,
            ask.unit.empty() ? std::string("this graph") : ask.unit,
            e.what());
    } catch (const BackendUnavailable &e) {
        // Also a capability miss: this run has no device to serve the operator with. `--devices
        // cpu` says so on purpose, and a run the driver refuses says why. `DeviceUnavailable` is
        // the other shape of this: there the run named a device that must exist.
        core::error::throwRuntimeFault(
            core::error::RuntimeDiag::DeviceNotSelected, ask.oper, e.what());
    } catch (const std::exception &e) {
        core::error::throwRuntimeFault(
            core::error::RuntimeDiag::RuntimeError,
            std::string("tiles: ") + e.what());
    }
}
template <class F> slot_t checked(F &&body) {
    return checked(Ask{"tiles", std::string{}}, std::forward<F>(body));
}

/// The startup check of `--devices` (GPU plan 5.3): every ordinal the run named must be one this
/// machine has. It runs before any graph executes, whether or not the program reaches a device
/// operation, so a device that is not there is reported where it was asked for rather than at
/// the first device call or not at all. The reason names what the machine has (how many devices,
/// or why none can be asked for) and every other named ordinal that is missing.
std::optional<execute::DeviceValidationFailure> validateCudaDevices() {
    const auto &selection = execute::selectedDevices();
    if (selection.cuda.empty()) {
        // The core answers `--devices cpu` itself; this is the ordinary guard for a validator
        // that is asked without a device list.
        return std::nullopt;
    }
    const CudaDeviceAvailability devices = cudaDeviceAvailability(selection.cuda);
    if (devices.missing.empty()) {
        return std::nullopt;
    }
    std::string reason;
    if (devices.count == 0) {
        reason = devices.reason.empty() ? "this machine has no CUDA device" : devices.reason;
    } else if (devices.count == 1) {
        reason = "this machine has 1 CUDA device (ordinal 0)";
    } else {
        reason = "this machine has " + std::to_string(devices.count) +
                 " CUDA devices (ordinals 0 to " + std::to_string(devices.count - 1) + ")";
    }
    if (devices.missing.size() > 1) {
        reason += "; missing: ";
        std::string separator;
        for (const int ordinal : devices.missing) {
            reason += separator + std::to_string(ordinal);
            separator = ", ";
        }
    }
    return execute::DeviceValidationFailure{devices.missing.front(), std::move(reason)};
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

// A scope a pass has cut is a wrapper graph: its statements are the exec calls of
// `regionSegments`, each carrying the graph of one segment. The wrapper is not a program of its
// own (an operator with context arguments has no plan), so a report operator asked about such a
// graph reports the segments: one plan each, which is the unit `tiles::compile` made.
std::string jsonString(const std::string &text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + '"';
}
std::string segmentEntry(const RegionSegment &segment, const std::string &field, Context &ctx) {
    // The plan of a type-only template: `Deferred` reading, because the values its inputs take
    // are the ones the call that runs the segment binds, and a report made before that call has
    // none. Everything else is the plan the outline generated for the segment.
    return "{\"call\":" + std::to_string(segment.call) + ",\"graph\":" +
           jsonString(segment.graph->graph()->name()) + ",\"" + field + "\":" +
           inspect(compileTypes(segment.graph, ctx.facts()), InputBindings::Deferred) + "}";
}
// What a segment report does not have: the values of the segment's inputs, which the call that
// runs it binds while the scope runs. Saying it keeps the reading from being taken for a run.
constexpr const char *unboundInputs =
    "\"inputs\":\"unbound: the call that runs a segment binds its inputs\"";
std::string segmentPlanReport(const std::vector<RegionSegment> &segments, Context &ctx) {
    std::string text = std::string("{\"schema\":1,") + unboundInputs + ",\"segments\":[";
    for (size_t i = 0; i < segments.size(); ++i)
        text += (i ? "," : "") + segmentEntry(segments[i], "plan", ctx);
    return text + "]}";
}
std::string segmentSourceReport(const std::vector<RegionSegment> &segments, Context &ctx) {
    std::string text;
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto program = compileTypes(segments[i].graph, ctx.facts());
        text += "// segment " + std::to_string(i) + ": " + segments[i].graph->graph()->name() +
                " (call " + std::to_string(segments[i].call) +
                "), inputs unbound: the call that runs it binds them\n" + program.source + "\n";
    }
    return text;
}
std::string segmentBenchmarkReport(const std::vector<RegionSegment> &segments, Context &ctx) {
    // What a segment can be timed on is its plan: the values its inputs take are bound by the
    // call that runs it, while the scope runs, and a report made before that call has none. The
    // samples of a plan with no inputs would measure values nothing wrote, so they are not taken.
    std::string text = std::string("{\"schema\":1,") + unboundInputs +
                       ",\"timing\":\"not measured: the call that runs a segment binds its "
                       "inputs, and this report has none\",\"segments\":[";
    for (size_t i = 0; i < segments.size(); ++i)
        text += (i ? "," : "") + segmentEntry(segments[i], "plan", ctx);
    return text + "]}";
}
slot_t regionsKernel(ArgsView &with, ArgsView &, Context &ctx) {
    auto *fn = with.get<::Function *>(0);
    return checked(Ask{"tiles:regions", std::string(fn->graph()->name())}, [&] {
        core::mm::NoCollectionScope noCollection;
        auto text = inspectRegions(
            *fn->program(), *fn->graph(), ctx.facts(), regionSegments(*fn->graph()));
        return td::toSlot(::String::from(text, core::mm::autoSpace()));
    });
}

slot_t sourceKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    auto *fn = with.get<::Function *>(0);
    return checked(Ask{"tiles:source", std::string(fn->graph()->name())}, [&] {
        core::mm::NoCollectionScope noCollection;
        const auto segments = regionSegments(*fn->graph());
        if (!segments.empty())
            return td::toSlot(
                ::String::from(segmentSourceReport(segments, ctx), core::mm::autoSpace()));
        auto program = prepare(with, norm, ctx);
        return td::toSlot(::String::from(program.source, core::mm::autoSpace()));
    });
}
slot_t inspectKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    auto *fn = with.get<::Function *>(0);
    return checked(Ask{"tiles:inspect", std::string(fn->graph()->name())}, [&] {
        core::mm::NoCollectionScope noCollection;
        const auto segments = regionSegments(*fn->graph());
        if (!segments.empty())
            return td::toSlot(
                ::String::from(segmentPlanReport(segments, ctx), core::mm::autoSpace()));
        return td::toSlot(::String::from(inspect(prepare(with, norm, ctx)), core::mm::autoSpace()));
    });
}
slot_t benchmarkKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    auto *fn = with.get<::Function *>(0);
    return checked(Ask{"tiles:benchmark", std::string(fn->graph()->name())}, [&] {
        core::mm::NoCollectionScope noCollection;
        const auto segments = regionSegments(*fn->graph());
        if (!segments.empty())
            return td::toSlot(
                ::String::from(segmentBenchmarkReport(segments, ctx), core::mm::autoSpace()));
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
        if (const auto scalar = deviceScalarDType(type)) {
            // A scalar leaf comes back as its element type: 4 bytes for f32, 8 for float.
            ++leaf;
            if (*scalar == DType::F32) {
                float value;
                std::memcpy(&value, data.data() + offset, sizeof(value));
                offset += sizeof(value);
                return td::toSlot(value);
            }
            double value;
            std::memcpy(&value, data.data() + offset, sizeof(value));
            offset += sizeof(value);
            return td::toSlot(value);
        }
        if (tensor::isTensorType(type)) {
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
slot_t runCompiledKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    auto *fn = with.get<::Function *>(0);
    return checked(Ask{"tiles:run_compiled", std::string(fn->graph()->name())}, [&] {
        return runPrepared(with, std::make_shared<const Program>(prepare(with, norm, ctx, true)));
    });
}
slot_t availableKernel(ArgsView &, ArgsView &, Context &) {
    // checked, so that a device the run named and that is missing is a diagnostic rather than
    // "no device here": `available()` answers a capability question, not a configuration one.
    return checked([] { return td::toSlot(available()); });
}
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
slot_t runCompiledDeviceKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::shared_ptr<const Program> program;
    auto *fn = with.get<::Function *>(0);
    checked(Ask{"tiles:run_compiled_device", std::string(fn->graph()->name())}, [&] {
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
        if (const auto scalar = deviceScalarDType(hostType)) {
            if (*scalar == DType::F32) {
                const float value32 = td::fromSlot<float>(value);
                return wrapDevice(
                    retainHostValue({}, DType::F32, std::as_bytes(std::span(&value32, 1))));
            }
            const double value64 = td::fromSlot<double>(value);
            return wrapDevice(
                retainHostValue({}, DType::F64, std::as_bytes(std::span(&value64, 1))));
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
    if (const auto scalar = deviceScalarDType(hostType)) {
        if (resident->dtype != *scalar)
            throw std::runtime_error("resident scalar type mismatch");
        if (*scalar == DType::F32) {
            if (data.size() != sizeof(float))
                throw std::runtime_error("resident scalar type mismatch");
            float value32;
            std::memcpy(&value32, data.data(), sizeof(value32));
            return td::toSlot(value32);
        }
        if (data.size() != sizeof(double))
            throw std::runtime_error("resident scalar type mismatch");
        double value64;
        std::memcpy(&value64, data.data(), sizeof(value64));
        return td::toSlot(value64);
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
// One argument of a resident execution call, as the host value the callee takes: a leaf that
// arrived resident is downloaded, an ordinary host leaf is passed through. `run_host_resident`
// and `run_preferred_resident` take either form — the region pass hands them resident inputs,
// while a direct call usually has host arguments — and both declare the host aggregate as their
// ordinary parameter.
slot_t hostArgument(Type *hostType, Type *givenType, slot_t value) {
    if (asDeviceType(givenType))
        return hostResidentValue(hostType, value, false);
    if (hostType->code() == TypeCode::Tuple) {
        auto *host  = static_cast<TupleType *>(hostType);
        auto *given = static_cast<TupleType *>(givenType);
        auto *input = td::fromSlot<::Tuple *>(value);
        auto *out   = ::Tuple::create(host->size(), core::mm::autoSpace());
        for (size_t i = 0; i < host->size(); ++i)
            out->set(
                i,
                hostArgument(host->typeAt(i), given->typeAt(i), input->get<slot_t>(i)),
                host);
        return td::toSlot(out);
    }
    if (hostType->code() == TypeCode::Struct) {
        auto *host  = static_cast<StructType *>(hostType);
        auto *given = static_cast<StructType *>(givenType);
        auto *input = td::fromSlot<::Struct *>(value);
        auto *out   = ::Struct::create(host->size(), core::mm::autoSpace());
        for (size_t i = 0; i < host->size(); ++i)
            out->set(
                i,
                hostArgument(host->typeAt(i), given->typeAt(i), input->get<slot_t>(i)),
                host);
        return td::toSlot(out);
    }
    return value;
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
        if (fields.size() == 1) {
            const auto host = hostArgument(fields[0], norm.type(0), norm.slot(0));
            tuple->set(0, host, tupleType);
            arguments.push_back(host);
        } else {
            auto *packed = norm.get<::Tuple *>(0);
            auto *given  = static_cast<TupleType *>(norm.type(0));
            for (size_t i = 0; i < fields.size(); ++i) {
                const auto host =
                    hostArgument(fields[i], given->typeAt(i), packed->get<slot_t>(i));
                tuple->set(i, host, tupleType);
                arguments.push_back(host);
            }
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
    std::string refusalOper;
    bool refusalShape = false;
    // Whether the run has a device at all. A run without one is not a missing program: the host
    // runs the program, and `--devices cpu` names that on purpose.
    const bool device = checked([&] {
        if (!configuredCudaOrdinal())
            return false;
        try {
            (void)deviceInfo();
            return true;
        } catch (const BackendUnavailable &) {
            return false;
        }
    });
    if (device) {
        auto *segment = with.get<::Function *>(0);
        checked(Ask{"tiles:run_preferred_resident", std::string(segment->graph()->name())}, [&] {
            try {
                program = std::make_shared<Program>(prepare(with, norm, ctx, true));
            } catch (const UnsupportedComputation &) {
                // The graph-bound template is the reusable form, but it needs the parameter
                // types to fix the shapes. When the call site is the only place that knows
                // them, specialize from the actual arguments instead of sending the work to
                // the host: the host path is a different execution of the same graph, and it
                // neither raises a Tile's own fault nor uses a granted contraction.
                try {
                    program = std::make_shared<Program>(prepare(with, norm, ctx));
                } catch (const UnsupportedComputation &e) {
                    refusalOper  = e.oper();
                    refusalShape = e.reason() == UnsupportedComputation::Reason::Shape;
                }
            }
            return slot_t{};
        });
    }
    if (!program) {
        // A `#gpu` graph is a device unit (GPU design 2.3): a run that cannot make its device
        // program must report it, not replay the graph on the host (GPU design 6.1). Outside a
        // `#gpu` scope the host is this operator's own contract — the device when it can serve
        // the graph, the host otherwise — and stays.
        auto *fn = with.get<::Function *>(0);
        if (device && bodyPlacementOf(*fn->graph()).placement == Placement::GpuRequired) {
            // A refusal with no operator names the class the compiler could not build, which is
            // always about the use's shapes or types; one with an operator says which one.
            const bool shape = refusalOper.empty() || refusalShape;
            const std::string who =
                refusalOper.empty() ? std::string(fn->graph()->name()) : refusalOper;
            core::error::throwRuntimeFault(
                core::error::RuntimeDiag::GpuScopeNoDeviceProgram,
                std::string(fn->graph()->name()),
                // The message takes the whole predicate, as the compile-time report does.
                "operator '" + who + "' has no device program",
                shape ? "no device program for this shape" : "no device program for this operator");
        }
        return runHostResidentKernel(with, norm, ctx);
    }
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
slot_t deviceKernel(ArgsView &, ArgsView &, Context &) {
    try {
        return td::toSlot(::String::from(deviceInfo(), core::mm::autoSpace()));
    } catch (const BackendUnavailable &e) {
        // This build or this machine having no device is not a broken run: the call answers an
        // error value of kind `DeviceUnavailable` and the caller decides how to report it. An
        // ordinal the run named with `--devices` and that is not there is the other shape of
        // this: it is refused at start-up, before any graph runs, and stays a fault.
        return td::makeError("DeviceUnavailable", e.what(), "<tiles::device>");
    }
}

bool supportedLeaves(Type *type) {
    return !core::type::tree::firstRejectedLeaf(type, [](Type *leaf) {
        return deviceScalarDType(leaf) || tensor::isTensorType(leaf);
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
// write any of them (standard library conventions 1-2). `upload` and `download` map
// every leaf of their argument into or out of the `Device<...>` handle; the resident/query groups
// take the function's parameter list as their ordinary parameter; `regions` accepts any function
// value. `stdlib/signatures/tiles.cml` records their reference entries, and the two fixed
// signatures (`available`, `device`) are declared in `stdlib/tiles.cml`, so no group here is a
// declaration's duplicate.
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups{
        OperatorGroup::create(
            "upload",
            {{"tiles:upload",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(T: Tensor, float, struct or tuple) => Device<T>",
                  [](const type_vec_t &with, const type_vec_t &norm)
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
                  [](const type_vec_t &with, const type_vec_t &norm)
                      -> std::optional<Type *> {
                      if (!with.empty() || norm.size() != 1)
                          return std::nullopt;
                      auto *result = transferTreeType(norm[0], false);
                      return result ? std::optional<Type *>(result) : std::nullopt;
                  })}}),
        OperatorGroup::create(
            "run_preferred_resident",
            {{"tiles:run_preferred_resident",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {1, {false}}},
                  "<fn: (...) => NumericAggregate>(inputs: NumericAggregate) => DeviceAggregate",
                  [](const type_vec_t &with, const type_vec_t &norm)
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
                  [](const type_vec_t &with, const type_vec_t &norm)
                      -> std::optional<Type *> { return resolve(with, norm, true); })}}),
        OperatorGroup::create(
            "inspect",
            {{"tiles:inspect",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {1, {false}}},
                  "<fn: (...) => NumericAggregate>(inputs: NumericAggregate) => string",
                  [](const type_vec_t &with, const type_vec_t &norm)
                      -> std::optional<Type *> { return resolve(with, norm, true); })}}),
        OperatorGroup::create(
            "benchmark",
            {{"tiles:benchmark",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {1, {false}}},
                  "<fn: (...) => NumericAggregate>(inputs: NumericAggregate) => string",
                  [](const type_vec_t &with, const type_vec_t &norm)
                      -> std::optional<Type *> { return resolve(with, norm, true); })}}),
        OperatorGroup::create(
            "regions",
            {{"tiles:regions",
              DynamicFuncTypeResolver::create(
                  {{1, {false}}, {0, {}}},
                  "<fn: Function>() => string",
                  [](const type_vec_t &with, const type_vec_t &norm)
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
        // The exported `Device` is the name a program applies its own host type to
        // (`Device<f32[3]>`), so the host type of the object the name is bound to is never a
        // value's type: it is one tensor, and the spelling's argument replaces it.
        exportType(
            Reference("Device"),
            DeviceType::get(camel::core::type::ArrayType::openRank(
                camel::core::type::Type::create(camel::core::type::TypeCode::Float32))));
        // `#gpu` is a standard tag of the core (design principles 13.4); `tiles::compile` reads
        // it and refuses a scope the device cannot run (GPU design 6.1).
        // `available` and `device` are declared in stdlib/tiles.cml: their
        // signatures are fixed, so the declaration is their one source and C++ exports no group
        // for them. The seven groups behind `getOperatorGroups()` compute their signature from an
        // argument and remain the module's own; the reference entries are in
        // stdlib/signatures/tiles.cml.
        for (const auto &group : getOperatorGroups())
            exportEntity(group->name(), group);
    }
    bool load() override {
        if (loaded_)
            return true;
        // The one device probe of the process, reported here because the device sources are
        // built without libcamel as well (see capability.h). `gpu_probe.once_per_process`
        // (test/plans/feat/modules/tiles.plan.toml) counts this line: however many calls
        // prefer a device, there must be one.
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
                    {"run_compiled", &runCompiledKernel},
                    {"run_compiled_device", &runCompiledDeviceKernel},
                    {"test_call", &testNativeCall},
                    {"test_compiled_call", &testCompiledCall},
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
        // The operators that initialize a driver or compile code are never constant-folded.
        core::OperatorTable::instance().declare(
            "tiles",
            {{"upload", false},
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
             {"run_compiled", "run_compiled_device", "source", "inspect", "benchmark", "regions"}) {
            core::OperatorTable::instance().set(
                std::string("tiles:") + name,
                {.pure = false, .readsGraphs = true});
        }
        for (const char *name : {"tiles:run_compiled", "tiles:run_compiled_device"})
            core::OperatorTable::instance().set(
                name,
                {.pure = false, .callsGraphs = true, .readsGraphs = true});
        // What each touches (docs/design/op-semantics 8, 14.4): device memory is a foreign
        // resource `tiles:device` (upload makes one; the compiled-scope runners read the device
        // values among their inputs, the resident ones return new ones that may be inputs
        // again); the driver and the device are ambient state `tiles:driver`. Every operator is
        // an effect outside the program, as its traits say.
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
                    {"run_compiled",
                     ResourceSpec{
                         .accesses       = {readsDevice(), driver(AccessMode::ReadModifyWrite)},
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
        // The driver asks this before the first graph runs; the selection is the run's, and this
        // module is the one that owns the CUDA ordinals it names.
        execute::registerDeviceValidator("tiles", validateCudaDevices);
        loaded_ = true;
        return true;
    }
};
} // namespace
} // namespace camel::tiles

extern "C" camel::core::module::Module *camel_module_create(camel::core::context::Context *ctx) {
    return new camel::tiles::TilesModule(ctx->shared_from_this());
}
