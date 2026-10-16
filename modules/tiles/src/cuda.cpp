/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Scoped execution, resident allocation ownership and GPU benchmark measurements.
#include "tiles/device.h"
#include "tiles/internal/capability.h"
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

namespace camel::tiles {
using namespace cuda_detail;
namespace {
struct OwnedAllocation final : DeviceStorage {
    std::shared_ptr<Backend> backend;
    CUdeviceptr pointer = 0;
    size_t capacity     = 0;
    std::vector<std::byte> read(uint64_t address, size_t bytes) const override {
        if (!backend || pointer != address || bytes > capacity)
            throw std::runtime_error("resident CUDA storage metadata mismatch");
        std::lock_guard lock(backend->mutex);
        Current current(backend->api, backend->context);
        std::vector<std::byte> result(bytes);
        if (!result.empty())
            backend->api.check(
                backend->api.cuMemcpyDtoH(result.data(), pointer, result.size()),
                "download resident tensor");
        return result;
    }
    ~OwnedAllocation() override {
        // No Camel references or allocator locks. The owner outlives every use;
        // transfers are synchronous before publishing the value.
        if (pointer && backend->api.cuCtxPushCurrent(backend->context) == CUDA_SUCCESS) {
            backend->api.cuMemFree(pointer);
            CUcontext popped;
            backend->api.cuCtxPopCurrent(&popped);
        }
    }
};

// Both normal execution and benchmarking use this exact upload/launch/download path.
// Allocation reuse can overwrite inputs, so every repetition must restore them.
class DeviceRun {
    ModuleLease moduleLease; // Outlives stream draining and all execution resources.
    std::shared_ptr<const Program> programOwner; // Includes borrowed device input owners.
    const Program &program;
    Api &api;
    Resources resources;
    std::vector<CUdeviceptr> pointers;
    std::vector<CUfunction> kernels;
    std::vector<void *> hostInputs;
    CUdeviceptr table;
    CUdeviceptr fault = 0;
    void *clearFault  = nullptr;

  public:
    DeviceRun(std::shared_ptr<const Program> snapshot, Api &a, ModuleLease module)
        : moduleLease(std::move(module)), programOwner(std::move(snapshot)), program(*programOwner),
          api(a), resources(moduleLease.backend) {
        const auto &p = program;
        std::vector<CUdeviceptr> allocations;
        for (auto bytes : p.storage.bytes)
            allocations.push_back(resources.allocate(bytes));
        pointers.assign(p.nodes.size(), 0);
        for (size_t id = 0; id < p.nodes.size(); ++id)
            if (p.nodes[id].resident) {
                auto allocation =
                    std::dynamic_pointer_cast<OwnedAllocation>(p.nodes[id].resident->owner);
                if (!allocation || !allocation->backend || &allocation->backend->api != &a ||
                    allocation->pointer != p.nodes[id].resident->address)
                    throw std::runtime_error("resident input belongs to another CUDA backend");
                pointers[id] = allocation->pointer;
            } else if (p.storage.slots[id] != SIZE_MAX)
                pointers[id] = allocations[p.storage.slots[id]];
        if (std::any_of(p.nodes.begin(), p.nodes.end(), [](const auto &n) {
                return n.op == "scalar_div";
            })) {
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
        hostInputs.assign(p.nodes.size(), nullptr);
        for (size_t id = 0; id < p.nodes.size(); ++id) {
            const auto &node = p.nodes[id];
            const auto *data = hostData(node);
            if (!data)
                continue;
            hostInputs[id] = resources.allocateHost(bytes(node));
            std::memcpy(hostInputs[id], data, bytes(node));
        }
        for (const auto &launch : p.launches) {
            CUfunction kernel;
            api.check(
                api.cuModuleGetFunction(&kernel, moduleLease.compiled->module, launch.name.c_str()),
                "resolve kernel");
            kernels.push_back(kernel);
        }
    }
    CUstream stream() const { return resources.stream; }
    void upload() {
        if (fault)
            api.check(
                api.cuMemcpyHtoDAsync(fault, clearFault, sizeof(unsigned), resources.stream),
                "clear scalar fault");
        for (size_t id = 0; id < program.nodes.size(); ++id) {
            const auto &n = program.nodes[id];
            if (hostInputs[id])
                api.check(
                    api.cuMemcpyHtoDAsync(pointers[id], hostInputs[id], bytes(n), resources.stream),
                    "upload input");
        }
        // Pageable synchronous copies can return after staging, before DMA completes.
        // All transfers use our stream and stable pinned buffers; completion here makes
        // the public synchronous upload contract and benchmark phase timing explicit.
        synchronize();
    }
    void launch() {
        void *args[] = {&table};
        for (size_t i = 0; i < program.launches.size(); ++i) {
            const auto &s = program.launches[i];
            if (!s.elements)
                continue;
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
        }
    }
    void synchronize() {
        api.check(api.cuStreamSynchronize(resources.stream), "complete GPU computation");
        if (fault) {
            unsigned node;
            api.check(api.cuMemcpyDtoH(&node, fault, sizeof(node)), "read scalar fault");
            if (node != UINT_MAX)
                throw std::runtime_error("division by zero at Tiles node " + std::to_string(node));
        }
    }
    std::vector<std::byte> download() {
        synchronize();
        size_t total = 0;
        for (auto output : program.outputs)
            total += bytes(program.nodes[output]);
        std::vector<std::byte> result(total);
        size_t offset = 0;
        for (auto output : program.outputs) {
            const size_t count = bytes(program.nodes[output]);
            if (count)
                api.check(
                    api.cuMemcpyDtoH(result.data() + offset, pointers[output], count),
                    "download result");
            offset += count;
        }
        return result;
    }
    std::vector<std::shared_ptr<DeviceValue>> retainOutputs(std::shared_ptr<Backend> backend) {
        synchronize();
        std::vector<std::shared_ptr<DeviceValue>> result;
        std::unordered_map<size_t, std::shared_ptr<DeviceValue>> retained;
        for (auto id : program.outputs) {
            if (auto it = retained.find(id); it != retained.end()) {
                result.push_back(it->second);
                continue;
            }
            const auto &node = program.nodes[id];
            auto value       = node.resident ? std::make_shared<DeviceValue>(*node.resident)
                                             : std::make_shared<DeviceValue>();
            if (!node.resident) {
                auto owner     = std::make_shared<OwnedAllocation>();
                owner->backend = backend;
                auto allocation =
                    std::find(resources.buffers.begin(), resources.buffers.end(), pointers[id]);
                if (allocation == resources.buffers.end())
                    throw std::runtime_error("resident output allocation is not owned");
                value->shape   = node.shape;
                value->dtype   = node.dtype;
                value->address = pointers[id];
                value->owner   = owner;
                // Transfer only after every potentially throwing field assignment.
                owner->capacity = bytes(node);
                owner->pointer  = *allocation;
                *allocation     = 0;
            }
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
    if (allocation && allocation->backend == target) {
        if (allocation->pointer != value->address ||
            bytes(Node{.shape = value->shape, .dtype = value->dtype}) > allocation->capacity)
            throw std::runtime_error("resident CUDA storage metadata mismatch");
        return value;
    }
    return uploadValue(value->shape, value->dtype, downloadValue(*value));
}
std::shared_ptr<DeviceValue>
uploadValue(const Shape &shape, DType dtype, std::span<const std::byte> data) {
    if (bytes(Node{.shape = shape, .dtype = dtype}) != data.size() ||
        (dtype == DType::F64 && !shape.empty()))
        throw std::runtime_error("device upload shape mismatch");
    auto b = sharedBackend();
    std::lock_guard lock(b->mutex);
    Current current(b->api, b->context);
    auto allocation      = std::make_shared<OwnedAllocation>();
    allocation->backend  = b;
    allocation->capacity = data.size();
    b->api.check(
        b->api.cuMemAlloc(&allocation->pointer, std::max(size_t(1), data.size())),
        "allocate resident tensor");
    Resources staging(b);
    if (!data.empty()) {
        const size_t size = data.size();
        void *pinned      = staging.allocateHost(size);
        std::memcpy(pinned, data.data(), size);
        b->api.check(
            b->api.cuMemcpyHtoDAsync(allocation->pointer, pinned, size, staging.stream),
            "upload resident tensor");
        b->api.check(b->api.cuStreamSynchronize(staging.stream), "complete resident upload");
    }
    auto result     = std::make_shared<DeviceValue>();
    result->shape   = shape;
    result->dtype   = dtype;
    result->address = allocation->pointer;
    result->owner   = std::move(allocation);
    return result;
}
bool available() {
    try {
        (void)backend();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

std::vector<std::byte> execute(std::shared_ptr<const Program> snapshot) {
    if (!snapshot)
        throw std::invalid_argument("missing Tiles program snapshot");
    const auto &p = *snapshot;
    validatePlan(p);
    Backend &b = backend();
    std::lock_guard lock(b.mutex);
    Current current(b.api, b.context);
    if (std::none_of(
            p.nodes.begin(),
            p.nodes.end(),
            [](const auto &n) { return n.op == "scalar_div"; }) &&
        std::all_of(p.outputs.begin(), p.outputs.end(), [&](size_t id) {
            return !elements(p.nodes[id].shape) ||
                   (p.nodes[id].op == "input" && !p.nodes[id].resident);
        })) {
        std::vector<std::byte> result;
        for (auto output : p.outputs) {
            const auto &node = p.nodes[output];
            if (const auto *data = hostData(node)) {
                auto *begin = static_cast<const std::byte *>(data);
                result.insert(result.end(), begin, begin + bytes(node));
            }
        }
        return result;
    }
    auto module = b.module(p.source);
    DeviceRun run(std::move(snapshot), b.api, module);
    run.upload();
    run.launch();
    return run.download();
}

std::vector<std::shared_ptr<DeviceValue>> executeResident(std::shared_ptr<const Program> snapshot) {
    if (!snapshot)
        throw std::invalid_argument("missing Tiles program snapshot");
    const auto &p = *snapshot;
    validatePlan(p);
    auto b = sharedBackend();
    std::lock_guard lock(b->mutex);
    Current current(b->api, b->context);
    auto module = b->module(p.source);
    DeviceRun run(std::move(snapshot), b->api, std::move(module));
    run.upload();
    run.launch();
    return run.retainOutputs(b);
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
    std::lock_guard lock(b.mutex);
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
        report["output_dtypes"].push_back(
            program.nodes[output].dtype == DType::F64 ? "f64" : "f32");
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
        const auto &p             = *plans[i];
        auto &r                   = report[names[i]];
        r["plan"]                 = json::parse(inspect(p));
        r["compile_cache_hit"]    = std::any_of(b.cache.begin(), b.cache.end(), [&](const auto &c) {
            return c->source == p.source && c->options == b.compileOptions();
        });
        start                     = Clock::now();
        auto module               = b.module(p.source);
        r["compile_load_host_ms"] = elapsed(start);
        start                     = Clock::now();
        runs[i]                   = std::make_unique<DeviceRun>(plans[i], b.api, module);
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
        size_t offset          = 0;
        constexpr auto relaxed = core::sem::kReassociate | core::sem::kContract |
                                 core::sem::kApproxMath | core::sem::kChangePrecision;
        for (auto output : program.outputs) {
            // Permission is local to each operation. Only dependencies of this
            // output can justify a relaxed optimized/unfused comparison.
            std::vector<bool> visited(program.nodes.size());
            std::vector<size_t> pending{output};
            bool bitwise = true;
            while (!pending.empty()) {
                const auto id = pending.back();
                pending.pop_back();
                if (visited[id])
                    continue;
                visited[id]      = true;
                const auto &node = program.nodes[id];
                bitwise &= !(node.permissions & relaxed);
                pending.insert(pending.end(), node.inputs.begin(), node.inputs.end());
            }
            const auto &node   = program.nodes[output];
            const size_t width = node.dtype == DType::F64 ? sizeof(double) : sizeof(float);
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
std::shared_ptr<DeviceValue> uploadValue(const Shape &, DType, std::span<const std::byte>) {
    (void)deviceInfo();
    return {};
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
