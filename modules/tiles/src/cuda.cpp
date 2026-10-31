// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Scoped execution, resident allocation ownership and GPU benchmark measurements.

#include "tiles/dtype.h"
#include "tiles/device.h"
#include "tiles/internal/capability.h"
#include "tiles/internal/mixed_gemm.h"
#include "tiles/internal/profile_hooks.h"
#include "tiles/internal/submission.h"
#include "tiles/program.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef CAMEL_TILES_CUDA
#include "tiles/internal/cuda_backend.h"
#include "tiles/internal/cuda_resources.h"
#include "tiles/internal/cuda_submission.h"

namespace camel::tiles {
using namespace cuda_detail;
namespace {
// Loop divisions report through the outer program's fault slot and node identity.
// Allocation and the host-only shortcut must inspect owned bodies as well.
bool programMayFault(const Program &program) {
    return std::any_of(program.nodes.begin(), program.nodes.end(), [](const Node &node) {
        return node.op == "scalar_div" || (node.loop && programMayFault(node.loop->body));
    });
}

struct ValueDependencies {
    std::vector<size_t> inputs;
    bool relaxed = false;
};
ValueDependencies valueDependencies(const Program &program, size_t output);

ValueDependencies loopDependencies(const Node &producer, size_t ordinal) {
    const auto &region        = *producer.loop;
    const size_t states       = region.stateInputs.size();
    std::vector<size_t> ports = region.stateInputs;
    ports.insert(ports.end(), region.captureInputs.begin(), region.captureInputs.end());
    struct Transition {
        std::vector<bool> inputs;
        bool relaxed = false;
    };
    std::vector<Transition> transitions;
    for (auto next : region.nextStates) {
        const auto dependencies = valueDependencies(region.body, next);
        Transition transition{std::vector<bool>(ports.size()), dependencies.relaxed};
        for (auto input : dependencies.inputs) {
            const auto port = std::find(ports.begin(), ports.end(), input);
            // Embedded constants have no corresponding invocation dependency.
            if (port != ports.end())
                transition.inputs[port - ports.begin()] = true;
        }
        transitions.push_back(std::move(transition));
    }
    // Compose exact simultaneous transitions. Captures and arithmetic permissions
    // are absorbing dependencies; states advance by exactly the selected trip count.
    // Repeated squaring avoids walking a potentially very large static domain.
    const auto advance = [&](const Transition &from) {
        Transition result{std::vector<bool>(ports.size()), from.relaxed};
        for (size_t input = 0; input < ports.size(); ++input) {
            if (!from.inputs[input])
                continue;
            if (input >= states) {
                result.inputs[input] = true;
                continue;
            }
            const auto &step = transitions[input];
            result.relaxed   = result.relaxed || step.relaxed;
            for (size_t port = 0; port < ports.size(); ++port)
                result.inputs[port] = result.inputs[port] || step.inputs[port];
        }
        return result;
    };
    Transition selected{std::vector<bool>(ports.size())};
    selected.inputs[ordinal] = true;
    for (size_t remaining = region.domain.size(); remaining;) {
        if (remaining & 1)
            selected = advance(selected);
        remaining >>= 1;
        if (remaining) {
            std::vector<Transition> squared;
            for (const auto &transition : transitions)
                squared.push_back(advance(transition));
            transitions = std::move(squared);
        }
    }
    ValueDependencies result{.relaxed = selected.relaxed};
    for (size_t port = 0; port < ports.size(); ++port)
        if (selected.inputs[port])
            result.inputs.push_back(producer.inputs[port]);
    return result;
}

ValueDependencies valueDependencies(const Program &program, size_t output) {
    constexpr auto relaxed = core::sem::kReassociate | core::sem::kContract |
                             core::sem::kApproxMath | core::sem::kChangePrecision;
    std::vector<bool> visited(program.nodes.size());
    std::vector<size_t> pending{output};
    ValueDependencies result;
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (visited[id])
            continue;
        visited[id]      = true;
        const auto &node = program.nodes[id];
        if (node.op == "result") {
            // A projection selects one state, not every sibling result of its
            // producer. The recurrence container grants no numeric permission.
            const auto dependencies = loopDependencies(
                program.nodes[node.inputs[0]],
                static_cast<size_t>(node.attributes[0]));
            result.relaxed = result.relaxed || dependencies.relaxed;
            pending.insert(pending.end(), dependencies.inputs.begin(), dependencies.inputs.end());
            continue;
        }
        result.relaxed = result.relaxed || bool(node.permissions & relaxed);
        if (node.op == "input")
            result.inputs.push_back(id);
        pending.insert(pending.end(), node.inputs.begin(), node.inputs.end());
    }
    return result;
}

bool outputHasRelaxedDependency(const Program &program, size_t output) {
    return valueDependencies(program, output).relaxed;
}

// Exported resident storage: a share of one pooled device lease plus the logical
// byte limit recorded when the value was published. The rounded pool capacity is
// an implementation detail and must never widen what metadata may read.
struct OwnedAllocation final : DeviceStorage {
    std::shared_ptr<AllocationLease> lease;
    size_t logicalBytes = 0;
    CUdeviceptr pointer() const { return lease ? lease->devicePointer() : 0; }
    std::vector<std::byte> read(uint64_t address, size_t bytes) const override {
        if (!lease || !lease->valid() || pointer() != address || bytes > logicalBytes)
            throw std::runtime_error("resident CUDA storage metadata mismatch");
        const auto &owner = lease->backend();
        Current current(owner->api, owner->context);
        std::vector<std::byte> result(bytes);
        if (!result.empty()) {
            owner->api.check(
                owner->api.cuMemcpyDtoH(result.data(), pointer(), result.size()),
                "download resident tensor");
            if (profile_hooks::toHost) {
                profile_hooks::toHost(result.size());
            }
        }
        return result;
    }
    // Ownership is shared: the lease returns its block exactly when the last
    // share drops, which covers this storage and the Resources that drained the
    // pending stream. No Camel references or allocator locks participate.
    ~OwnedAllocation() override = default;
};

// Production submissions and benchmarks share allocation, uploads and launches.
// Benchmarks explicitly complete upload before timing; production records one
// completion after all work. Allocation reuse requires restoring every input.
class DeviceRun {
    ModuleLease moduleLease; // Outlives stream draining and all execution resources.
    std::shared_ptr<const Program> programOwner; // Includes borrowed device input owners.
    const Program &program;
    Api &api;
    std::shared_ptr<Submission> submission;
    // Before Resources in member order: benchmark storage drains before handle
    // destruction. Native submissions additionally retain it through completion.
    std::shared_ptr<MixedGemm> mixedGemm;
    std::unique_ptr<Resources> ownedResources;
    Resources &resources;
    std::vector<CUdeviceptr> pointers;
    std::vector<CUfunction> kernels;
    std::vector<void *> hostInputs;
    CUdeviceptr table;
    CUdeviceptr fault = 0;
    void *clearFault  = nullptr;

  public:
    DeviceRun(
        std::shared_ptr<const Program> snapshot, Api &a, ModuleLease module,
        std::shared_ptr<Submission> pending = {})
        : moduleLease(std::move(module)), programOwner(std::move(snapshot)), program(*programOwner),
          api(a), submission(std::move(pending)),
          ownedResources(submission ? nullptr : std::make_unique<Resources>(moduleLease.backend)),
          resources(submission ? submission->resources() : *ownedResources) {
        const auto &p = program;
        std::vector<CUdeviceptr> allocations;
        for (auto bytes : p.storage.bytes)
            allocations.push_back(resources.allocate(bytes));
        pointers.assign(p.nodes.size(), 0);
        for (size_t id = 0; id < p.nodes.size(); ++id)
            if (p.nodes[id].resident) {
                auto allocation =
                    std::dynamic_pointer_cast<OwnedAllocation>(p.nodes[id].resident->owner);
                if (!allocation || !allocation->lease || !allocation->lease->valid() ||
                    &allocation->lease->backend()->api != &a ||
                    allocation->pointer() != p.nodes[id].resident->address ||
                    bytes(p.nodes[id]) > allocation->logicalBytes)
                    throw std::runtime_error("resident input belongs to another CUDA backend");
                if (p.nodes[id].resident->completion) {
                    if (submission)
                        submission->depend(p.nodes[id].resident->completion);
                    else
                        p.nodes[id].resident->completion->wait();
                }
                pointers[id] = allocation->pointer();
            } else if (p.storage.slots[id] != SIZE_MAX)
                pointers[id] = allocations[p.storage.slots[id]];
        if (programMayFault(p)) {
            fault                                = resources.allocate(sizeof(unsigned));
            clearFault                           = resources.allocateHost(sizeof(unsigned));
            *static_cast<unsigned *>(clearFault) = UINT_MAX;
            pointers.push_back(fault);
        }
        table                   = resources.allocate(pointers.size() * sizeof(CUdeviceptr));
        const size_t tableBytes = pointers.size() * sizeof(CUdeviceptr);
        void *hostTable         = resources.allocateHost(tableBytes);
        std::memcpy(hostTable, pointers.data(), tableBytes);
        api.check(
            api.cuMemcpyHtoDAsync(table, hostTable, tableBytes, resources.stream),
            "upload argument table");
        // All host inputs of one submission share a single pinned staging region:
        // the forest is copied into it once and every input reaches its own device
        // block on the same stream.
        size_t hostTotal = 0;
        for (const auto &node : p.nodes) {
            if (hostData(node)) {
                if (bytes(node) > SIZE_MAX - hostTotal)
                    throw std::overflow_error("Tiles host input staging overflows");
                hostTotal += bytes(node);
            }
        }
        hostInputs.assign(p.nodes.size(), nullptr);
        if (hostTotal) {
            void *hostStaging = resources.allocateHost(hostTotal);
            size_t hostOffset = 0;
            for (size_t id = 0; id < p.nodes.size(); ++id) {
                const auto &node = p.nodes[id];
                if (!hostData(node))
                    continue;
                // The bytes are the node's own element width: an f32 node copies its bits, a bf16
                // or f16 node the value rounded once into the element type.
                const std::vector<std::byte> storage = hostStorage(node);
                hostInputs[id] = static_cast<std::byte *>(hostStaging) + hostOffset;
                std::memcpy(hostInputs[id], storage.data(), storage.size());
                hostOffset += storage.size();
            }
        }
        for (const auto &launch : p.launches) {
            if (launch.implementation == "cublas_bf16") {
                if (!mixedGemm) {
                    mixedGemm = std::make_shared<MixedGemm>(moduleLease.backend, resources.stream);
                    if (submission)
                        submission->keepAlive(mixedGemm);
                }
                kernels.push_back(nullptr);
                continue;
            }
            CUfunction kernel;
            api.check(
                api.cuModuleGetFunction(&kernel, moduleLease.compiled->module, launch.name.c_str()),
                "resolve kernel");
            kernels.push_back(kernel);
        }
    }
    CUstream stream() const { return resources.stream; }
    CUdeviceptr faultSlot() const { return fault; }
    void upload(bool complete = true) {
        if (fault)
            api.check(
                api.cuMemcpyHtoDAsync(fault, clearFault, sizeof(unsigned), resources.stream),
                "clear scalar fault");
        for (size_t id = 0; id < program.nodes.size(); ++id) {
            const auto &n = program.nodes[id];
            if (hostInputs[id]) {
                api.check(
                    api.cuMemcpyHtoDAsync(pointers[id], hostInputs[id], bytes(n), resources.stream),
                    "upload input");
                // `std::profile`: host->device traffic and the kernel count of this region.
                if (profile_hooks::toDevice) {
                    profile_hooks::toDevice(bytes(n));
                }
            }
        }
        // Benchmark upload timing includes DMA completion. Native submissions keep
        // the same pinned staging alive and record completion after their kernels.
        if (complete)
            synchronize();
    }
    void launch() {
        void *args[] = {&table};
        for (size_t i = 0; i < program.launches.size(); ++i) {
            const auto &s = program.launches[i];
            if (!s.elements)
                continue;
            if (s.implementation == "cublas_bf16") {
                mixedGemm->run(program, program.nodes[s.node], pointers);
                continue;
            }
            api.check(
                api.cuLaunchKernel(
                    kernels[i],
                    s.gridX,
                    s.gridY,
                    1,
                    s.blockX,
                    s.blockY,
                    1,
                    0,
                    resources.stream,
                    args,
                    nullptr),
                "launch kernel");
            if (profile_hooks::kernel) {
                profile_hooks::kernel(s.name.c_str());
            }
        }
    }
    void synchronize() {
        api.check(api.cuStreamSynchronize(resources.stream), "complete GPU computation");
        if (profile_hooks::synchronise) {
            profile_hooks::synchronise("tiles stream");
        }
        if (fault) {
            unsigned node;
            api.check(api.cuMemcpyDtoH(&node, fault, sizeof(node)), "read scalar fault");
            if (node != UINT_MAX)
                throw std::runtime_error("division by zero at Tiles node " + std::to_string(node));
        }
    }
    std::vector<std::byte> download() {
        size_t total = 0;
        for (auto output : program.outputs) {
            if (bytes(program.nodes[output]) > SIZE_MAX - total)
                throw std::overflow_error("Tiles result download overflows");
            total += bytes(program.nodes[output]);
        }
        // One pinned region takes every result; the copies are queued before the
        // single synchronisation, so the host waits once for the whole forest.
        void *pinned  = total ? resources.allocateHost(total) : nullptr;
        size_t offset = 0;
        for (auto output : program.outputs) {
            const size_t count = bytes(program.nodes[output]);
            if (count)
                api.check(
                    api.cuMemcpyDtoHAsync(
                        static_cast<std::byte *>(pinned) + offset,
                        pointers[output],
                        count,
                        resources.stream),
                    "download result");
            offset += count;
        }
        synchronize();
        std::vector<std::byte> result(total);
        offset = 0;
        for (auto output : program.outputs) {
            const size_t count = bytes(program.nodes[output]);
            if (count) {
                std::memcpy(
                    result.data() + offset,
                    static_cast<const std::byte *>(pinned) + offset,
                    count);
                if (profile_hooks::toHost)
                    profile_hooks::toHost(count);
            }
            offset += count;
        }
        return result;
    }
    std::vector<std::shared_ptr<DeviceValue>> retainOutputs(bool complete = true) {
        if (complete)
            synchronize();
        std::vector<std::shared_ptr<DeviceValue>> result;
        std::unordered_map<size_t, std::shared_ptr<DeviceValue>> retained;
        for (auto id : program.outputs) {
            if (auto it = retained.find(id); it != retained.end()) {
                result.push_back(it->second);
                continue;
            }
            const auto &node = program.nodes[id];
            if (node.resident) {
                // A returned resident input keeps its own lease and owner.
                auto value = std::make_shared<DeviceValue>(*node.resident);
                if (submission)
                    submission->keepAlive(value->owner);
                retained.emplace(id, value);
                result.push_back(std::move(value));
                continue;
            }
            // Distinct output nodes can resolve to the same allocation. Each
            // keeps its own shape, dtype and logical byte bound and shares only
            // the underlying lease, so one node's metadata can never widen
            // another node's read. Only an identical node reuses its whole value.
            auto owner     = std::make_shared<OwnedAllocation>();
            auto value     = std::make_shared<DeviceValue>();
            value->shape   = node.shape;
            value->dtype   = node.dtype;
            value->address = pointers[id];
            value->owner   = owner;
            // The submission must retain the exported owner before anything can
            // throw; Resources keeps its own share for the whole drain, so a
            // pending DMA can never see this block reused underneath it.
            if (submission)
                submission->keepAlive(owner);
            owner->lease = resources.shareDevice(pointers[id]);
            if (!owner->lease)
                throw std::runtime_error("resident output allocation is not owned");
            owner->logicalBytes = bytes(node);
            retained.emplace(id, value);
            result.push_back(std::move(value));
        }
        return result;
    }
};
struct Event {
    Api &api;
    CUevent handle = nullptr;
    explicit Event(Api &a) : api(a) {
        api.check(api.cuEventCreate(&handle, 0), "create timing event");
    }
    ~Event() {
        if (handle)
            api.cuEventDestroy(handle);
    }
};
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
} // namespace

std::string deviceInfo() { return backend().description; }
std::shared_ptr<const DeviceValue> placeOnDevice(std::shared_ptr<const DeviceValue> value) {
    if (!value)
        throw std::invalid_argument("missing resident value");
    const auto allocation = std::dynamic_pointer_cast<OwnedAllocation>(value->owner);
    const auto target     = sharedBackend();
    if (allocation && allocation->lease && allocation->lease->backend() == target) {
        if (allocation->pointer() != value->address ||
            bytes(Node{.shape = value->shape, .dtype = value->dtype}) > allocation->logicalBytes)
            throw std::runtime_error("resident CUDA storage metadata mismatch");
        if (value->completion)
            value->completion->wait();
        return value;
    }
    return uploadValue(value->shape, value->dtype, downloadValue(*value));
}
std::vector<std::shared_ptr<DeviceValue>> uploadValues(std::span<const HostLeaf> values) {
    size_t total = 0;
    for (const auto &leaf : values) {
        if (bytes(Node{.shape = leaf.shape, .dtype = leaf.dtype}) != leaf.bytes.size() ||
            (leaf.dtype == DType::F64 && !leaf.shape.empty()))
            throw std::runtime_error("device upload shape mismatch");
        if (leaf.bytes.size() > SIZE_MAX - total)
            throw std::overflow_error("device upload batch is too large");
        total += leaf.bytes.size();
    }
    if (values.empty())
        return {};
    auto b = sharedBackend();
    Current current(b->api, b->context);
    // Staging owns every destination block as well as the one pinned source, so
    // an exception anywhere below drains the queued copies before a block can
    // return to the pool. The leases are shared, not moved: each returned value
    // keeps its own block alive after this scope ends. Only the physical capacity
    // is rounded; the logical payload stays exactly the uploaded size.
    Resources staging(b);
    std::vector<std::shared_ptr<DeviceValue>> result;
    std::vector<CUdeviceptr> pointers;
    result.reserve(values.size());
    pointers.reserve(values.size());
    for (const auto &leaf : values) {
        const CUdeviceptr pointer = staging.allocate(leaf.bytes.size());
        auto allocation           = std::make_shared<OwnedAllocation>();
        allocation->lease         = staging.shareDevice(pointer);
        if (!allocation->lease)
            throw std::runtime_error("upload destination allocation is not owned");
        allocation->logicalBytes = leaf.bytes.size();
        auto value               = std::make_shared<DeviceValue>();
        value->shape             = leaf.shape;
        value->dtype             = leaf.dtype;
        value->address           = pointer;
        value->owner             = std::move(allocation);
        pointers.push_back(pointer);
        result.push_back(std::move(value));
    }
    if (total) {
        auto *pinned  = static_cast<std::byte *>(staging.allocateHost(total));
        size_t offset = 0;
        for (size_t i = 0; i < values.size(); ++i) {
            const auto &leaf = values[i];
            if (leaf.bytes.empty())
                continue;
            std::memcpy(pinned + offset, leaf.bytes.data(), leaf.bytes.size());
            b->api.check(
                b->api.cuMemcpyHtoDAsync(
                    pointers[i],
                    pinned + offset,
                    leaf.bytes.size(),
                    staging.stream),
                "upload resident tensor");
            if (profile_hooks::toDevice)
                profile_hooks::toDevice(leaf.bytes.size());
            offset += leaf.bytes.size();
        }
        b->api.check(b->api.cuStreamSynchronize(staging.stream), "complete resident upload");
        if (profile_hooks::synchronise)
            profile_hooks::synchronise("resident upload");
    }
    return result;
}
std::shared_ptr<DeviceValue>
uploadValue(const Shape &shape, DType dtype, std::span<const std::byte> data) {
    const HostLeaf leaf{shape, dtype, data};
    auto values = uploadValues(std::span<const HostLeaf>(&leaf, 1));
    return std::move(values.front());
}
std::vector<std::vector<std::byte>> downloadValues(std::span<const DeviceValue *const> values) {
    std::vector<std::vector<std::byte>> result(values.size());
    struct Copy {
        size_t index;
        CUdeviceptr source;
        size_t bytes;
    };
    std::vector<Copy> copies;
    std::vector<const DeviceCompletion *> waited;
    std::shared_ptr<Backend> batchBackend;
    for (size_t i = 0; i < values.size(); ++i) {
        const auto &value = *values[i];
        // Distinct leaves published by one submission share one completion, so
        // the whole forest waits for it once.
        if (value.completion &&
            std::find(waited.begin(), waited.end(), value.completion.get()) == waited.end()) {
            value.completion->wait();
            waited.push_back(value.completion.get());
        }
        if (!value.owner || !value.address)
            throw std::runtime_error("invalid resident tensor");
        const size_t count    = bytes(Node{.shape = value.shape, .dtype = value.dtype});
        const auto allocation = std::dynamic_pointer_cast<OwnedAllocation>(value.owner);
        if (!allocation || !allocation->lease || !allocation->lease->valid() ||
            allocation->pointer() != value.address || count > allocation->logicalBytes ||
            (batchBackend && allocation->lease->backend() != batchBackend)) {
            // Host storage and any foreign or inconsistent CUDA storage keep their
            // own checked read; they cannot share this backend's stream. A foreign
            // CUDA block still needs its own context for the synchronous copy.
            if (allocation && allocation->lease && allocation->lease->valid()) {
                auto foreign = allocation->lease->backend();
                Current current(foreign->api, foreign->context);
                result[i] = value.owner->read(value.address, count);
            } else {
                result[i] = value.owner->read(value.address, count);
            }
            continue;
        }
        if (!batchBackend)
            batchBackend = allocation->lease->backend();
        if (count)
            copies.push_back({i, allocation->pointer(), count});
    }
    if (copies.empty())
        return result;
    auto b = std::move(batchBackend);
    Current current(b->api, b->context);
    Resources staging(b);
    size_t total = 0;
    for (const auto &copy : copies) {
        if (copy.bytes > SIZE_MAX - total)
            throw std::overflow_error("device download batch is too large");
        total += copy.bytes;
    }
    auto *pinned  = static_cast<std::byte *>(staging.allocateHost(total));
    size_t offset = 0;
    for (const auto &copy : copies) {
        b->api.check(
            b->api.cuMemcpyDtoHAsync(pinned + offset, copy.source, copy.bytes, staging.stream),
            "download resident tensor");
        offset += copy.bytes;
    }
    b->api.check(b->api.cuStreamSynchronize(staging.stream), "complete resident download");
    if (profile_hooks::synchronise)
        profile_hooks::synchronise("resident download");
    offset = 0;
    for (const auto &copy : copies) {
        result[copy.index].resize(copy.bytes);
        std::memcpy(result[copy.index].data(), pinned + offset, copy.bytes);
        offset += copy.bytes;
        if (profile_hooks::toHost)
            profile_hooks::toHost(copy.bytes);
    }
    return result;
}
bool available() {
    try {
        (void)backend();
        return true;
    } catch (const DeviceUnavailable &) {
        // A device the run named is missing: that is the run's configuration, not a capability
        // of this machine, so the caller reports it instead of treating the host as the answer.
        throw;
    } catch (const std::exception &) {
        return false;
    }
}

std::vector<std::byte> execute(std::shared_ptr<const Program> snapshot) {
    if (!snapshot)
        throw std::invalid_argument("missing Tiles program snapshot");
    const auto &p = *snapshot;
    validatePlan(p);
    (void)backend();
    if (!programMayFault(p) &&
        std::none_of(
            p.nodes.begin(),
            p.nodes.end(),
            [](const Node &n) { return bool(n.resident); }) &&
        std::all_of(p.outputs.begin(), p.outputs.end(), [&](size_t id) {
            return !elements(p.nodes[id].shape) ||
                   (p.nodes[id].op == "input" && !p.nodes[id].resident);
        })) {
        std::vector<std::byte> result;
        for (auto output : p.outputs) {
            const auto &node = p.nodes[output];
            if (!hostData(node))
                continue;
            const std::vector<std::byte> storage = hostStorage(node);
            result.insert(result.end(), storage.begin(), storage.end());
        }
        return result;
    }
    auto submitted = submitProgram(std::move(snapshot));
    submitted.completion->wait();
    std::vector<const DeviceValue *> outputs;
    outputs.reserve(submitted.outputs.size());
    for (const auto &output : submitted.outputs)
        outputs.push_back(output.get());
    // All result leaves come back on one stream with one pinned region and one
    // synchronisation, not one per leaf.
    auto leaves = downloadValues(outputs);
    std::vector<std::byte> result;
    for (auto &leaf : leaves)
        result.insert(result.end(), leaf.begin(), leaf.end());
    return result;
}

SubmittedProgram submitProgram(std::shared_ptr<const Program> snapshot) {
    if (!snapshot)
        throw std::invalid_argument("missing Tiles program snapshot");
    const auto &p = *snapshot;
    validatePlan(p);
    auto b = sharedBackend();
    Current current(b->api, b->context);
    auto module = b->module(p.source);
    auto completion =
        std::make_shared<Submission>(b, std::make_unique<Resources>(b), snapshot, module);
    DeviceRun run(std::move(snapshot), b->api, std::move(module), completion);
    run.upload(false);
    run.launch();
    SubmittedProgram submitted{run.retainOutputs(false), completion};
    for (const auto &output : submitted.outputs)
        output->completion = completion;
    completion->finish(run.faultSlot());
    return submitted;
}

std::vector<std::shared_ptr<DeviceValue>> executeResident(std::shared_ptr<const Program> snapshot) {
    auto submitted = submitProgram(std::move(snapshot));
    submitted.completion->wait();
    return std::move(submitted.outputs);
}

std::string benchmark(std::shared_ptr<const Program> snapshot) {
    if (!snapshot)
        throw std::invalid_argument("missing Tiles program snapshot");
    const auto &program = *snapshot;
    using nlohmann::json;
    validatePlan(program);
    if (std::none_of(program.outputs.begin(), program.outputs.end(), [&](size_t id) {
            return elements(program.nodes[id].shape) && program.nodes[id].op != "input";
        }))
        throw std::runtime_error("benchmark requires a nonempty computed output");
    Program reference = program;
    generate(reference, {.fuseElementwise = false, .parallelMax = false});
    auto start              = Clock::now();
    Backend &b              = backend();
    double initializationMs = elapsed(start);
    Current current(b.api, b.context);
    int driver = 0, major = 0, minor = 0;
    b.api.check(b.api.cuDriverGetVersion(&driver), "query driver version");
    b.api.check(b.api.nvrtcVersion(&major, &minor), "query compiler version");
    json report = {
        {"schema", 1},
        {"device", b.description},
        {"driver_version", driver},
        {"nvrtc_version", std::to_string(major) + "." + std::to_string(minor)},
        {"target", b.architecture},
        {"dtype", "f32"},
        {"fmad", false},
        {"compile_options", b.compileOptions()},
        {"initialization_host_ms", initializationMs},
        {"warmups", 5},
        {"repetitions", 21},
        {"timing", "CUDA event span including launch gaps; inputs restored before every sample"},
        {"includes_graph_specialization", false},
        {"reference_policy", {{"fuse_elementwise", false}, {"parallel_max", false}}},
        {"identical_device_code", program.source == reference.source}};
    report["output_shapes"] = json::array();
    report["output_dtypes"] = json::array();
    for (auto output : program.outputs)
        report["output_dtypes"].push_back(dtypeName(program.nodes[output].dtype));
    if (std::any_of(program.nodes.begin(), program.nodes.end(), [](const auto &n) {
            return n.dtype == DType::F64;
        }))
        report["dtype"] = "mixed";
    for (auto output : program.outputs)
        report["output_shapes"].push_back(program.nodes[output].shape);
    if (program.outputs.size() == 1)
        report["output_shape"] = program.nodes[program.outputs.front()].shape;
    report["input_shapes"] = json::array();
    for (const auto &n : program.nodes)
        if (n.op == "input")
            report["input_shapes"].push_back(n.shape);
    const std::shared_ptr<const Program> plans[] = {
        std::move(snapshot),
        std::make_shared<const Program>(std::move(reference))};
    const char *names[] = {"optimized", "unfused"};
    std::unique_ptr<DeviceRun> runs[2];
    for (size_t i = 0; i < 2; ++i) {
        const auto &p                 = *plans[i];
        auto &r                       = report[names[i]];
        r["plan"]                     = json::parse(inspect(p));
        r["compile_cache_hit"]        = b.cached(p.source);
        start                         = Clock::now();
        auto module                   = b.module(p.source);
        r["compile_load_host_ms"]     = elapsed(start);
        start                         = Clock::now();
        runs[i]                       = std::make_unique<DeviceRun>(plans[i], b.api, module);
        r["allocation_setup_host_ms"] = elapsed(start);
        r["device_ms"]                = json::array();
        r["upload_host_ms"]           = json::array();
    }
    Event begin(b.api), end(b.api);
    std::vector<std::byte> results[2];
    for (size_t i = 0; i < 2; ++i) {
        start = Clock::now();
        runs[i]->upload();
        runs[i]->launch();
        results[i]                                                = runs[i]->download();
        report[names[i]]["first_upload_execute_download_host_ms"] = elapsed(start);
    }
    auto compare = [&] {
        auto decode = [&](const std::vector<std::byte> &raw) {
            std::vector<double> values;
            size_t offset = 0;
            for (auto output : program.outputs) {
                const auto &node = program.nodes[output];
                for (size_t i = 0; i < elements(node.shape); ++i) {
                    if (node.dtype == DType::F64) {
                        double value;
                        std::memcpy(&value, raw.data() + offset, sizeof(value));
                        values.push_back(value);
                        offset += sizeof(value);
                    } else {
                        float value;
                        std::memcpy(&value, raw.data() + offset, sizeof(value));
                        values.push_back(value);
                        offset += sizeof(value);
                    }
                }
            }
            return values;
        };
        const auto actual = decode(results[0]), expected = decode(results[1]);
        std::vector<bool> exact;
        std::vector<size_t> offsets, widths;
        size_t offset = 0;
        for (auto output : program.outputs) {
            // Permission is local to each operation. Only dependencies of this
            // output can justify a relaxed optimized/unfused comparison.
            const bool bitwise = !outputHasRelaxedDependency(program, output);
            const auto &node   = program.nodes[output];
            const size_t width = dtypeWidth(node.dtype);
            for (size_t i = 0; i < elements(node.shape); ++i) {
                exact.push_back(bitwise);
                offsets.push_back(offset);
                widths.push_back(width);
                offset += width;
            }
        }
        double maxAbs = 0, maxRel = 0;
        size_t mismatches = 0;
        for (size_t i = 0; i < actual.size(); ++i) {
            double a = actual[i], r = expected[i];
            const bool equalBits = std::memcmp(
                                       results[0].data() + offsets[i],
                                       results[1].data() + offsets[i],
                                       widths[i]) == 0;
            if (exact[i] && !equalBits)
                ++mismatches;
            if (!std::isfinite(a) || !std::isfinite(r)) {
                if (!exact[i] && !(a == r || (std::isnan(a) && std::isnan(r))))
                    ++mismatches;
                continue;
            }
            double diff = std::abs(a - r);
            maxAbs      = std::max(maxAbs, diff);
            maxRel      = std::max(maxRel, diff / std::max(1e-30, std::abs(r)));
            if (!exact[i] && diff > 1e-6 + 1e-5 * std::abs(r))
                ++mismatches;
        }
        if (mismatches)
            throw std::runtime_error("benchmark correctness gate failed against unfused execution");
        return json{
            {"bitwise_elements", std::count(exact.begin(), exact.end(), true)},
            {"relaxed_elements", std::count(exact.begin(), exact.end(), false)},
            {"reference", "same-device unfused execution"},
            {"max_abs_error", maxAbs},
            {"max_rel_error", maxRel},
            {"mismatches", mismatches}};
    };
    report["correctness_before"] = compare();
    // Alternate ordering to reduce systematic clock/cache bias. Restoring inputs is
    // essential: the production storage plan may reuse their allocation for results.
    for (int sample = -5; sample < 21; ++sample) {
        for (size_t order = 0; order < 2; ++order) {
            size_t i  = (order + (sample + 5) % 2) % 2;
            auto &run = *runs[i];
            start     = Clock::now();
            run.upload();
            double uploadMs = elapsed(start);
            b.api.check(b.api.cuEventRecord(begin.handle, run.stream()), "record timing start");
            run.launch();
            b.api.check(b.api.cuEventRecord(end.handle, run.stream()), "record timing end");
            b.api.check(b.api.cuEventSynchronize(end.handle), "wait for timing end");
            float ms = 0;
            b.api.check(
                b.api.cuEventElapsedTime(&ms, begin.handle, end.handle),
                "read device time");
            if (sample >= 0) {
                report[names[i]]["device_ms"].push_back(ms);
                report[names[i]]["upload_host_ms"].push_back(uploadMs);
            }
        }
    }
    for (size_t i = 0; i < 2; ++i) {
        start                 = Clock::now();
        results[i]            = runs[i]->download();
        auto &r               = report[names[i]];
        r["download_host_ms"] = elapsed(start);
        auto samples          = r["device_ms"].get<std::vector<double>>();
        std::sort(samples.begin(), samples.end());
        r["device_min_ms"]    = samples.front();
        r["device_median_ms"] = samples[samples.size() / 2];
        r["device_p90_ms"]    = samples[(samples.size() - 1) * 9 / 10];
    }
    report["correctness_after"] = compare();
    double optimized            = report["optimized"]["device_median_ms"];
    if (optimized > 0)
        report["median_speedup"] = report["unfused"]["device_median_ms"].get<double>() / optimized;
    else
        report["median_speedup"] = nullptr;
    return report.dump();
}
} // namespace camel::tiles
#else
namespace camel::tiles {
std::string deviceInfo() {
    throw BackendUnavailable(
        "tiles was built without CUDA Toolkit headers; install CUDA and rebuild");
}
bool available() { return false; }
std::shared_ptr<const DeviceValue> placeOnDevice(std::shared_ptr<const DeviceValue>) {
    (void)deviceInfo();
    return {};
}
std::vector<std::shared_ptr<DeviceValue>> executeResident(std::shared_ptr<const Program>) {
    (void)deviceInfo();
    return {};
}
SubmittedProgram submitProgram(std::shared_ptr<const Program>) {
    (void)deviceInfo();
    return {};
}
std::shared_ptr<DeviceValue> uploadValue(const Shape &, DType, std::span<const std::byte>) {
    (void)deviceInfo();
    return {};
}
std::vector<std::shared_ptr<DeviceValue>> uploadValues(std::span<const HostLeaf>) {
    (void)deviceInfo();
    return {};
}
std::vector<std::vector<std::byte>> downloadValues(std::span<const DeviceValue *const> values) {
    std::vector<std::vector<std::byte>> result;
    result.reserve(values.size());
    for (const auto *value : values)
        result.push_back(downloadValue(*value));
    return result;
}
std::vector<std::byte> execute(std::shared_ptr<const Program>) {
    (void)deviceInfo();
    return {};
}
std::string benchmark(std::shared_ptr<const Program>) {
    (void)deviceInfo();
    return {};
}
} // namespace camel::tiles
#endif
