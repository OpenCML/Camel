/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Lazy Driver/NVRTC loading, bounded compiled-module cache, and scoped device ownership.
#include "tiles/program.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

#ifdef CAMEL_TILES_CUDA
#include <cuda.h>
#include <nvrtc.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace camel::tiles {
namespace {
class Library {
    void *handle_ = nullptr;
#ifdef _WIN32
    HMODULE builtins_ = nullptr;
#endif
  public:
    explicit Library(const std::vector<std::string> &paths) {
        for (const auto &p : paths) {
#ifdef _WIN32
            handle_ = reinterpret_cast<void *>(LoadLibraryW(std::filesystem::path(p).c_str()));
#else
            handle_ = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
            if (handle_) {
#ifdef _WIN32
                // NVRTC loads its builtins lazily by basename. Keep the matching sibling
                // loaded without modifying the process-wide DLL search path or PATH.
                auto version = reinterpret_cast<int (*)(int *, int *)>(
                    GetProcAddress(reinterpret_cast<HMODULE>(handle_), "nvrtcVersion"));
                if (version) {
                    int major = 0, minor = 0;
                    wchar_t location[32768]{};
                    if (version(&major, &minor) == 0 &&
                        GetModuleFileNameW(reinterpret_cast<HMODULE>(handle_), location, 32768)) {
                        auto sibling = std::filesystem::path(location).parent_path() /
                                       ("nvrtc-builtins64_" + std::to_string(major) +
                                        std::to_string(minor) + ".dll");
                        builtins_    = LoadLibraryW(sibling.c_str());
                    }
                }
#endif
                return;
            }
        }
        throw std::runtime_error(
            "cannot load " + paths.front() + "; install the NVIDIA driver/CUDA NVRTC runtime");
    }
    ~Library() {
#ifdef _WIN32
        if (handle_)
            FreeLibrary(reinterpret_cast<HMODULE>(handle_));
        if (builtins_)
            FreeLibrary(builtins_);
#else
        if (handle_)
            dlclose(handle_);
#endif
    }
    Library(const Library &) = delete;
    template <class T> T symbol(const char *name) {
#ifdef _WIN32
        auto p = GetProcAddress(reinterpret_cast<HMODULE>(handle_), name);
#else
        auto p = dlsym(handle_, name);
#endif
        if (!p)
            throw std::runtime_error(std::string("missing CUDA runtime symbol ") + name);
        return reinterpret_cast<T>(p);
    }
};

std::vector<std::string> nvrtcPaths() {
    std::vector<std::string> paths;
    if (auto *overridePath = std::getenv("CAMEL_TILES_NVRTC"))
        paths.emplace_back(overridePath);
#ifdef _WIN32
    std::vector<std::filesystem::path> dirs;
    if (auto *root = std::getenv("CUDA_PATH"))
        dirs.emplace_back(std::filesystem::path(root) / "bin");
    dirs.emplace_back(CAMEL_TILES_CUDA_BIN);
    for (const auto &dir : dirs) {
        std::error_code ec;
        std::vector<std::string> found;
        for (const auto &e : std::filesystem::directory_iterator(dir, ec)) {
            const auto name = e.path().filename().string();
            if (name.starts_with("nvrtc64_") && name.ends_with(".dll"))
                found.push_back(e.path().string());
        }
        std::sort(found.rbegin(), found.rend());
        paths.insert(paths.end(), found.begin(), found.end());
    }
    paths.insert(paths.end(), {"nvrtc64_130_0.dll", "nvrtc64_120_0.dll", "nvrtc64_112_0.dll"});
#else
    paths.insert(paths.end(), {"libnvrtc.so", "libnvrtc.so.13", "libnvrtc.so.12"});
    paths.push_back((std::filesystem::path(CAMEL_TILES_CUDA_LIB) / "libnvrtc.so").string());
#endif
    return paths;
}

#define TILES_STR_INNER(x) #x
#define TILES_STR(x) TILES_STR_INNER(x)
struct Api {
#ifdef _WIN32
    Library driver{{"nvcuda.dll"}};
#else
    Library driver{{"libcuda.so.1"}};
#endif
    Library rtc{nvrtcPaths()};
#define DRIVER(fn) decltype(&::fn) fn = driver.symbol<decltype(&::fn)>(TILES_STR(fn))
    DRIVER(cuInit);
    DRIVER(cuDriverGetVersion);
    DRIVER(cuDeviceGet);
    DRIVER(cuDeviceGetName);
    DRIVER(cuDeviceGetAttribute);
    DRIVER(cuDevicePrimaryCtxRetain);
    DRIVER(cuDevicePrimaryCtxRelease);
    DRIVER(cuCtxPushCurrent);
    DRIVER(cuCtxPopCurrent);
    DRIVER(cuGetErrorName);
    DRIVER(cuGetErrorString);
    DRIVER(cuModuleLoadData);
    DRIVER(cuModuleUnload);
    DRIVER(cuModuleGetFunction);
    DRIVER(cuMemAlloc);
    DRIVER(cuMemFree);
    DRIVER(cuMemHostAlloc);
    DRIVER(cuMemFreeHost);
    DRIVER(cuMemcpyHtoD);
    DRIVER(cuMemcpyHtoDAsync);
    DRIVER(cuMemcpyDtoH);
    DRIVER(cuStreamCreate);
    DRIVER(cuStreamSynchronize);
    DRIVER(cuStreamDestroy);
    DRIVER(cuLaunchKernel);
    DRIVER(cuEventCreate);
    DRIVER(cuEventDestroy);
    DRIVER(cuEventRecord);
    DRIVER(cuEventSynchronize);
    DRIVER(cuEventElapsedTime);
#undef DRIVER
#define RTC(fn) decltype(&::fn) fn = rtc.symbol<decltype(&::fn)>(#fn)
    RTC(nvrtcCreateProgram);
    RTC(nvrtcVersion);
    RTC(nvrtcCompileProgram);
    RTC(nvrtcDestroyProgram);
    RTC(nvrtcGetPTXSize);
    RTC(nvrtcGetPTX);
    RTC(nvrtcGetProgramLogSize);
    RTC(nvrtcGetProgramLog);
    RTC(nvrtcGetErrorString);
    RTC(nvrtcGetNumSupportedArchs);
    RTC(nvrtcGetSupportedArchs);
#undef RTC
    void check(CUresult r, const char *action) {
        if (r == CUDA_SUCCESS)
            return;
        const char *name = "CUDA error", *message = "unknown error";
        cuGetErrorName(r, &name);
        cuGetErrorString(r, &message);
        throw std::runtime_error(std::string(action) + ": " + name + " (" + message + ")");
    }
    void check(nvrtcResult r, const char *action) {
        if (r != NVRTC_SUCCESS)
            throw std::runtime_error(std::string(action) + ": " + nvrtcGetErrorString(r));
    }
};

struct Current {
    Api &api;
    Current(Api &a, CUcontext ctx) : api(a) {
        api.check(api.cuCtxPushCurrent(ctx), "activate CUDA context");
    }
    ~Current() {
        CUcontext popped;
        api.cuCtxPopCurrent(&popped);
    }
};
struct Compiled {
    std::string source;
    CUmodule module;
};
class Backend {
  public:
    Api api;
    CUdevice device;
    CUcontext context = nullptr;
    std::string description, architecture;
    std::deque<Compiled> cache;
    std::mutex mutex;

    Backend() {
        api.check(api.cuInit(0), "initialize CUDA");
        int ordinal = 0;
        if (auto *value = std::getenv("CAMEL_TILES_DEVICE")) {
            char *end     = nullptr;
            long selected = std::strtol(value, &end, 10);
            if (!*value || *end || selected < 0 || selected > 65535)
                throw std::runtime_error("CAMEL_TILES_DEVICE must be a nonnegative device ordinal");
            ordinal = static_cast<int>(selected);
        }
        api.check(api.cuDeviceGet(&device, ordinal), "select CUDA device");
        char name[256]{};
        api.check(api.cuDeviceGetName(name, sizeof(name), device), "query CUDA device");
        int major = 0, minor = 0;
        api.check(
            api.cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device),
            "query compute capability");
        api.check(
            api.cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device),
            "query compute capability");
        description = std::string(name) + " (sm_" + std::to_string(major * 10 + minor) + ")";
        int count   = 0;
        api.check(api.nvrtcGetNumSupportedArchs(&count), "query NVRTC architectures");
        std::vector<int> archs(count);
        api.check(api.nvrtcGetSupportedArchs(archs.data()), "query NVRTC architectures");
        int target = 0;
        for (int a : archs)
            if (a <= major * 10 + minor)
                target = std::max(target, a);
        if (!target)
            throw std::runtime_error("NVRTC does not support this GPU architecture");
        architecture = "--gpu-architecture=compute_" + std::to_string(target);
        api.check(api.cuDevicePrimaryCtxRetain(&context, device), "retain CUDA context");
    }
    ~Backend() {
        if (context) {
            if (api.cuCtxPushCurrent(context) == CUDA_SUCCESS) {
                for (const auto &item : cache)
                    api.cuModuleUnload(item.module);
                CUcontext popped;
                api.cuCtxPopCurrent(&popped);
            }
            api.cuDevicePrimaryCtxRelease(device);
        }
    }
    CUmodule module(const std::string &source) {
        for (size_t i = 0; i < cache.size(); ++i) {
            if (cache[i].source != source)
                continue;
            CUmodule found = cache[i].module;
            // Refresh recency before loading another variant. A benchmark keeps two
            // variants live under the same mutex; the second load must not evict the first.
            std::rotate(cache.begin() + i, cache.begin() + i + 1, cache.end());
            return found;
        }
        nvrtcProgram program = nullptr;
        api.check(
            api.nvrtcCreateProgram(&program, source.c_str(), "camel_tiles.cu", 0, nullptr, nullptr),
            "create NVRTC program");
        struct Guard {
            Api &a;
            nvrtcProgram &p;
            ~Guard() { a.nvrtcDestroyProgram(&p); }
        } guard{api, program};
        const char *options[] = {architecture.c_str(), "--std=c++14", "--fmad=false"};
        auto status           = api.nvrtcCompileProgram(program, 3, options);
        if (status != NVRTC_SUCCESS) {
            size_t size = 0;
            api.check(api.nvrtcGetProgramLogSize(program, &size), "read NVRTC log");
            std::string log(size, '\0');
            if (size)
                api.check(api.nvrtcGetProgramLog(program, log.data()), "read NVRTC log");
            throw std::runtime_error("CUDA compilation failed: " + log);
        }
        size_t size = 0;
        api.check(api.nvrtcGetPTXSize(program, &size), "get PTX size");
        std::string ptx(size, '\0');
        api.check(api.nvrtcGetPTX(program, ptx.data()), "get PTX");
        CUmodule module;
        api.check(api.cuModuleLoadData(&module, ptx.data()), "load PTX");
        try {
            cache.push_back({source, module});
        } catch (...) {
            api.cuModuleUnload(module);
            throw;
        }
        if (cache.size() > 32) {
            api.cuModuleUnload(cache.front().module);
            cache.pop_front();
        }
        return module;
    }
};

Backend &backend() {
    static Backend instance;
    return instance;
}

// The stream is drained before buffers are freed, including on a failed launch.
struct Resources {
    Api &api;
    CUstream stream = nullptr;
    std::vector<CUdeviceptr> buffers;
    std::vector<void *> hostBuffers;
    explicit Resources(Api &a) : api(a) {
        api.check(api.cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING), "create stream");
    }
    ~Resources() {
        if (stream)
            api.cuStreamSynchronize(stream);
        for (auto ptr : buffers)
            if (ptr)
                api.cuMemFree(ptr);
        for (auto ptr : hostBuffers)
            api.cuMemFreeHost(ptr);
        if (stream)
            api.cuStreamDestroy(stream);
    }
    CUdeviceptr allocate(size_t size) {
        CUdeviceptr ptr = 0;
        api.check(api.cuMemAlloc(&ptr, std::max(size, size_t(1))), "allocate device memory");
        try {
            buffers.push_back(ptr);
        } catch (...) {
            api.cuMemFree(ptr);
            throw;
        }
        return ptr;
    }
    void *allocateHost(size_t size) {
        void *ptr = nullptr;
        api.check(api.cuMemHostAlloc(&ptr, size, 0), "allocate pinned host memory");
        try {
            hostBuffers.push_back(ptr);
        } catch (...) {
            api.cuMemFreeHost(ptr);
            throw;
        }
        return ptr;
    }
};

// Both normal execution and benchmarking use this exact upload/launch/download path.
// Allocation reuse can overwrite inputs, so every repetition must restore them.
class DeviceRun {
    const Program &program;
    Api &api;
    Resources resources;
    std::vector<CUdeviceptr> pointers;
    std::vector<CUfunction> kernels;
    std::vector<void *> hostInputs;
    CUdeviceptr table;

  public:
    DeviceRun(const Program &p, Api &a, CUmodule module) : program(p), api(a), resources(a) {
        std::vector<CUdeviceptr> allocations;
        for (auto bytes : p.storage.bytes)
            allocations.push_back(resources.allocate(bytes));
        pointers.assign(p.nodes.size(), 0);
        for (size_t id = 0; id < p.nodes.size(); ++id)
            if (p.storage.slots[id] != SIZE_MAX)
                pointers[id] = allocations[p.storage.slots[id]];
        table                   = resources.allocate(pointers.size() * sizeof(CUdeviceptr));
        const size_t tableBytes = pointers.size() * sizeof(CUdeviceptr);
        void *hostTable         = resources.allocateHost(tableBytes);
        std::memcpy(hostTable, pointers.data(), tableBytes);
        api.check(
            api.cuMemcpyHtoDAsync(table, hostTable, tableBytes, resources.stream),
            "upload argument table");
        hostInputs.assign(p.nodes.size(), nullptr);
        for (size_t id = 0; id < p.nodes.size(); ++id) {
            const auto &data = p.nodes[id].data;
            if (data.empty())
                continue;
            hostInputs[id] = resources.allocateHost(data.size() * sizeof(float));
            std::memcpy(hostInputs[id], data.data(), data.size() * sizeof(float));
        }
        for (const auto &launch : p.launches) {
            CUfunction kernel;
            api.check(
                api.cuModuleGetFunction(&kernel, module, launch.name.c_str()),
                "resolve kernel");
            kernels.push_back(kernel);
        }
    }
    CUstream stream() const { return resources.stream; }
    void upload() {
        for (size_t id = 0; id < program.nodes.size(); ++id) {
            const auto &n = program.nodes[id];
            if (!n.data.empty())
                api.check(
                    api.cuMemcpyHtoDAsync(
                        pointers[id],
                        hostInputs[id],
                        n.data.size() * sizeof(float),
                        resources.stream),
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
    }
    std::vector<float> download() {
        synchronize();
        std::vector<float> result(elements(program.nodes[program.output].shape));
        if (!result.empty())
            api.check(
                api.cuMemcpyDtoH(
                    result.data(),
                    pointers[program.output],
                    result.size() * sizeof(float)),
                "download result");
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
bool available() {
    try {
        (void)backend();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

std::vector<float> execute(const Program &p) {
    validatePlan(p);
    Backend &b = backend();
    std::lock_guard lock(b.mutex);
    Current current(b.api, b.context);
    const size_t count = elements(p.nodes.at(p.output).shape);
    std::vector<float> result(count);
    if (!count)
        return result;
    if (p.nodes[p.output].op == "input")
        return p.nodes[p.output].data;
    CUmodule module = b.module(p.source);
    DeviceRun run(p, b.api, module);
    run.upload();
    run.launch();
    return run.download();
}

std::string benchmark(const Program &program) {
    using nlohmann::json;
    validatePlan(program);
    if (!elements(program.nodes[program.output].shape) ||
        program.nodes[program.output].op == "input")
        throw std::runtime_error("benchmark requires a nonempty computed output");
    Program reference = program;
    generate(reference, false);
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
        {"initialization_host_ms", initializationMs},
        {"warmups", 5},
        {"repetitions", 21},
        {"timing", "CUDA event span including launch gaps; inputs restored before every sample"},
        {"includes_graph_specialization", false},
        {"identical_device_code", program.source == reference.source},
        {"output_shape", program.nodes[program.output].shape}};
    report["input_shapes"] = json::array();
    for (const auto &n : program.nodes)
        if (n.op == "input")
            report["input_shapes"].push_back(n.shape);
    const Program *plans[] = {&program, &reference};
    const char *names[]    = {"optimized", "unfused"};
    std::unique_ptr<DeviceRun> runs[2];
    for (size_t i = 0; i < 2; ++i) {
        const auto &p = *plans[i];
        auto &r       = report[names[i]];
        r["plan"]     = json::parse(inspect(p));
        r["compile_cache_hit"] =
            std::any_of(b.cache.begin(), b.cache.end(), [&](const Compiled &c) {
                return c.source == p.source;
            });
        start                         = Clock::now();
        CUmodule module               = b.module(p.source);
        r["compile_load_host_ms"]     = elapsed(start);
        start                         = Clock::now();
        runs[i]                       = std::make_unique<DeviceRun>(p, b.api, module);
        r["allocation_setup_host_ms"] = elapsed(start);
        r["device_ms"]                = json::array();
        r["upload_host_ms"]           = json::array();
    }
    Event begin(b.api), end(b.api);
    std::vector<float> results[2];
    for (size_t i = 0; i < 2; ++i) {
        start = Clock::now();
        runs[i]->upload();
        runs[i]->launch();
        results[i]                                                = runs[i]->download();
        report[names[i]]["first_upload_execute_download_host_ms"] = elapsed(start);
    }
    auto compare = [&] {
        double maxAbs = 0, maxRel = 0;
        size_t mismatches = 0;
        for (size_t i = 0; i < results[0].size(); ++i) {
            double a = results[0][i], r = results[1][i];
            if (!std::isfinite(a) || !std::isfinite(r)) {
                if (!(a == r || (std::isnan(a) && std::isnan(r))))
                    ++mismatches;
                continue;
            }
            double diff = std::abs(a - r);
            maxAbs      = std::max(maxAbs, diff);
            maxRel      = std::max(maxRel, diff / std::max(1e-30, std::abs(r)));
            if (diff > 1e-6 + 1e-5 * std::abs(r))
                ++mismatches;
        }
        if (mismatches)
            throw std::runtime_error("benchmark correctness gate failed against unfused execution");
        return json{
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
    throw std::runtime_error(
        "tiles was built without CUDA Toolkit headers; install CUDA and rebuild");
}
bool available() { return false; }
std::vector<float> execute(const Program &) {
    (void)deviceInfo();
    return {};
}
std::string benchmark(const Program &) {
    (void)deviceInfo();
    return {};
}
} // namespace camel::tiles
#endif
