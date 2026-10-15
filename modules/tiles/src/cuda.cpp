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
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
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
    DRIVER(cuMemcpyHtoD);
    DRIVER(cuMemcpyDtoH);
    DRIVER(cuStreamCreate);
    DRIVER(cuStreamSynchronize);
    DRIVER(cuStreamDestroy);
    DRIVER(cuLaunchKernel);
#undef DRIVER
#define RTC(fn) decltype(&::fn) fn = rtc.symbol<decltype(&::fn)>(#fn)
    RTC(nvrtcCreateProgram);
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
        for (const auto &item : cache)
            if (item.source == source)
                return item.module;
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
    explicit Resources(Api &a) : api(a) {
        api.check(api.cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING), "create stream");
    }
    ~Resources() {
        if (stream)
            api.cuStreamSynchronize(stream);
        for (auto ptr : buffers)
            if (ptr)
                api.cuMemFree(ptr);
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
};
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
    Resources resources(b.api);
    std::vector<CUdeviceptr> allocations;
    for (auto bytes : p.storage.bytes)
        allocations.push_back(resources.allocate(bytes));
    std::vector<CUdeviceptr> pointers(p.nodes.size(), 0);
    for (size_t id = 0; id < p.nodes.size(); ++id) {
        const auto &n = p.nodes[id];
        if (p.storage.slots[id] == SIZE_MAX)
            continue;
        size_t bytes = elements(n.shape) * sizeof(float);
        auto ptr     = allocations[p.storage.slots[id]];
        pointers[id] = ptr;
        if (!n.data.empty())
            b.api.check(b.api.cuMemcpyHtoD(ptr, n.data.data(), bytes), "upload input");
    }
    CUdeviceptr table = resources.allocate(pointers.size() * sizeof(CUdeviceptr));
    b.api.check(
        b.api.cuMemcpyHtoD(table, pointers.data(), pointers.size() * sizeof(CUdeviceptr)),
        "upload argument table");
    void *args[] = {&table};
    for (const auto &launch : p.launches) {
        if (!launch.elements)
            continue;
        CUfunction kernel;
        b.api.check(
            b.api.cuModuleGetFunction(&kernel, module, launch.name.c_str()),
            "resolve kernel");
        b.api.check(
            b.api.cuLaunchKernel(
                kernel,
                launch.gridX,
                launch.gridY,
                1,
                launch.blockX,
                launch.blockY,
                1,
                0,
                resources.stream,
                args,
                nullptr),
            "launch kernel");
    }
    b.api.check(b.api.cuStreamSynchronize(resources.stream), "complete GPU computation");
    b.api.check(
        b.api.cuMemcpyDtoH(result.data(), pointers[p.output], result.size() * sizeof(float)),
        "download result");
    return result;
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
} // namespace camel::tiles
#endif
