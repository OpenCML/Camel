/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Load the NVIDIA APIs, select the device and cache compiled kernel modules.
#include "tiles/internal/cuda_backend.h"
#include "camel/execute/devices.h"
#include "tiles/internal/capability.h"
#include "tiles/internal/profile_hooks.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#ifdef CAMEL_TILES_CUDA
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace camel::tiles::cuda_detail {
Library::Library(const std::vector<std::string> &paths) {
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
    throw BackendUnavailable(
        "cannot load " + paths.front() + "; install the NVIDIA driver/CUDA NVRTC runtime");
}
Library::~Library() {
#ifdef _WIN32
    if (handle_)
        FreeLibrary(reinterpret_cast<HMODULE>(handle_));
    if (builtins_)
        FreeLibrary(reinterpret_cast<HMODULE>(builtins_));
#else
    if (handle_)
        dlclose(handle_);
#endif
}
void *Library::findSymbol(const char *name) {
#ifdef _WIN32
    auto p = GetProcAddress(reinterpret_cast<HMODULE>(handle_), name);
#else
    auto p = dlsym(handle_, name);
#endif
    if (!p)
        throw std::runtime_error(std::string("missing CUDA runtime symbol ") + name);
    return reinterpret_cast<void *>(p);
}
namespace {
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
} // namespace
Api::Api()
#ifdef _WIN32
    : driver({"nvcuda.dll"}), rtc(nvrtcPaths()) {
#else
    : driver({"libcuda.so.1"}), rtc(nvrtcPaths()) {
#endif
#define TILES_STR_INNER(x) #x
#define TILES_STR(x) TILES_STR_INNER(x)
    cuInit = driver.symbol<decltype(&::cuInit)>(TILES_STR(cuInit));
    cuDriverGetVersion =
        driver.symbol<decltype(&::cuDriverGetVersion)>(TILES_STR(cuDriverGetVersion));
    cuDeviceGet      = driver.symbol<decltype(&::cuDeviceGet)>(TILES_STR(cuDeviceGet));
    cuDeviceGetCount = driver.symbol<decltype(&::cuDeviceGetCount)>(TILES_STR(cuDeviceGetCount));
    cuDeviceGetName  = driver.symbol<decltype(&::cuDeviceGetName)>(TILES_STR(cuDeviceGetName));
    cuDeviceGetAttribute =
        driver.symbol<decltype(&::cuDeviceGetAttribute)>(TILES_STR(cuDeviceGetAttribute));
    cuDevicePrimaryCtxRetain =
        driver.symbol<decltype(&::cuDevicePrimaryCtxRetain)>(TILES_STR(cuDevicePrimaryCtxRetain));
    cuDevicePrimaryCtxRelease =
        driver.symbol<decltype(&::cuDevicePrimaryCtxRelease)>(TILES_STR(cuDevicePrimaryCtxRelease));
    cuCtxPushCurrent = driver.symbol<decltype(&::cuCtxPushCurrent)>(TILES_STR(cuCtxPushCurrent));
    cuCtxPopCurrent  = driver.symbol<decltype(&::cuCtxPopCurrent)>(TILES_STR(cuCtxPopCurrent));
    cuGetErrorName   = driver.symbol<decltype(&::cuGetErrorName)>(TILES_STR(cuGetErrorName));
    cuGetErrorString = driver.symbol<decltype(&::cuGetErrorString)>(TILES_STR(cuGetErrorString));
    cuModuleLoadData = driver.symbol<decltype(&::cuModuleLoadData)>(TILES_STR(cuModuleLoadData));
    cuModuleUnload   = driver.symbol<decltype(&::cuModuleUnload)>(TILES_STR(cuModuleUnload));
    cuModuleGetFunction =
        driver.symbol<decltype(&::cuModuleGetFunction)>(TILES_STR(cuModuleGetFunction));
    cuMemAlloc        = driver.symbol<decltype(&::cuMemAlloc)>(TILES_STR(cuMemAlloc));
    cuMemFree         = driver.symbol<decltype(&::cuMemFree)>(TILES_STR(cuMemFree));
    cuMemHostAlloc    = driver.symbol<decltype(&::cuMemHostAlloc)>(TILES_STR(cuMemHostAlloc));
    cuMemFreeHost     = driver.symbol<decltype(&::cuMemFreeHost)>(TILES_STR(cuMemFreeHost));
    cuMemcpyHtoD      = driver.symbol<decltype(&::cuMemcpyHtoD)>(TILES_STR(cuMemcpyHtoD));
    cuMemcpyHtoDAsync = driver.symbol<decltype(&::cuMemcpyHtoDAsync)>(TILES_STR(cuMemcpyHtoDAsync));
    cuMemcpyDtoH      = driver.symbol<decltype(&::cuMemcpyDtoH)>(TILES_STR(cuMemcpyDtoH));
    cuMemcpyDtoHAsync = driver.symbol<decltype(&::cuMemcpyDtoHAsync)>(TILES_STR(cuMemcpyDtoHAsync));
    cuStreamCreate    = driver.symbol<decltype(&::cuStreamCreate)>(TILES_STR(cuStreamCreate));
    cuStreamSynchronize =
        driver.symbol<decltype(&::cuStreamSynchronize)>(TILES_STR(cuStreamSynchronize));
    cuStreamDestroy   = driver.symbol<decltype(&::cuStreamDestroy)>(TILES_STR(cuStreamDestroy));
    cuStreamWaitEvent = driver.symbol<decltype(&::cuStreamWaitEvent)>(TILES_STR(cuStreamWaitEvent));
    cuLaunchKernel    = driver.symbol<decltype(&::cuLaunchKernel)>(TILES_STR(cuLaunchKernel));
    cuEventCreate     = driver.symbol<decltype(&::cuEventCreate)>(TILES_STR(cuEventCreate));
    cuEventDestroy    = driver.symbol<decltype(&::cuEventDestroy)>(TILES_STR(cuEventDestroy));
    cuEventRecord     = driver.symbol<decltype(&::cuEventRecord)>(TILES_STR(cuEventRecord));
    cuEventSynchronize =
        driver.symbol<decltype(&::cuEventSynchronize)>(TILES_STR(cuEventSynchronize));
    cuEventQuery = driver.symbol<decltype(&::cuEventQuery)>(TILES_STR(cuEventQuery));
    cuEventElapsedTime =
        driver.symbol<decltype(&::cuEventElapsedTime)>(TILES_STR(cuEventElapsedTime));
    nvrtcCreateProgram  = rtc.symbol<decltype(&::nvrtcCreateProgram)>("nvrtcCreateProgram");
    nvrtcVersion        = rtc.symbol<decltype(&::nvrtcVersion)>("nvrtcVersion");
    nvrtcCompileProgram = rtc.symbol<decltype(&::nvrtcCompileProgram)>("nvrtcCompileProgram");
    nvrtcDestroyProgram = rtc.symbol<decltype(&::nvrtcDestroyProgram)>("nvrtcDestroyProgram");
    nvrtcGetPTXSize     = rtc.symbol<decltype(&::nvrtcGetPTXSize)>("nvrtcGetPTXSize");
    nvrtcGetPTX         = rtc.symbol<decltype(&::nvrtcGetPTX)>("nvrtcGetPTX");
    nvrtcGetProgramLogSize =
        rtc.symbol<decltype(&::nvrtcGetProgramLogSize)>("nvrtcGetProgramLogSize");
    nvrtcGetProgramLog  = rtc.symbol<decltype(&::nvrtcGetProgramLog)>("nvrtcGetProgramLog");
    nvrtcGetErrorString = rtc.symbol<decltype(&::nvrtcGetErrorString)>("nvrtcGetErrorString");
    nvrtcGetNumSupportedArchs =
        rtc.symbol<decltype(&::nvrtcGetNumSupportedArchs)>("nvrtcGetNumSupportedArchs");
    nvrtcGetSupportedArchs =
        rtc.symbol<decltype(&::nvrtcGetSupportedArchs)>("nvrtcGetSupportedArchs");
#undef TILES_STR
#undef TILES_STR_INNER
}
void Api::check(CUresult r, const char *action) {
    if (r == CUDA_SUCCESS)
        return;
    const char *name = "CUDA error", *message = "unknown error";
    cuGetErrorName(r, &name);
    cuGetErrorString(r, &message);
    throw std::runtime_error(std::string(action) + ": " + name + " (" + message + ")");
}
void Api::check(nvrtcResult r, const char *action) {
    if (r != NVRTC_SUCCESS)
        throw std::runtime_error(std::string(action) + ": " + nvrtcGetErrorString(r));
}
namespace {
/// What the driver says about `result`, for a diagnostic that names a device.
std::string cudaFailureText(Api &api, CUresult result) {
    const char *name    = nullptr;
    const char *message = nullptr;
    if (api.cuGetErrorName(result, &name) != CUDA_SUCCESS || !name)
        name = "CUDA error";
    if (api.cuGetErrorString(result, &message) != CUDA_SUCCESS || !message)
        message = "unknown error";
    return std::string(name) + " (" + message + ")";
}
} // namespace
Current::Current(Api &a, CUcontext ctx) : api(a) {
    api.check(api.cuCtxPushCurrent(ctx), "activate CUDA context");
}
Current::~Current() {
    CUcontext popped;
    api.cuCtxPopCurrent(&popped);
}
Backend::Backend(MemoryPolicy policy)
    : devicePool_(api, PoolKind::Device, policy.device),
      hostPool_(api, PoolKind::Host, policy.host) {
    // The selection is the run's configuration (design principles 13.4): without `--devices` a
    // missing device is a capability miss and the host runs the program; with it every named
    // device must be there, and a missing one is a diagnostic naming it, never a fallback.
    const auto &selection        = camel::execute::selectedDevices();
    const bool explicitSelection = selection.given && !selection.cuda.empty();
    const auto initialized       = api.cuInit(0);
    if (initialized == CUDA_ERROR_NO_DEVICE) {
        if (explicitSelection)
            throw DeviceUnavailable(selection.cuda.front(), "this machine has no CUDA device");
        throw BackendUnavailable("no CUDA device is available");
    }
    if (initialized != CUDA_SUCCESS) {
        if (explicitSelection)
            throw DeviceUnavailable(
                selection.cuda.front(),
                "initializing CUDA failed: " + cudaFailureText(api, initialized));
        api.check(initialized, "initialize CUDA");
    }
    if (explicitSelection) {
        int count = 0;
        api.check(api.cuDeviceGetCount(&count), "count CUDA devices");
        for (const int selected : selection.cuda) {
            CUdevice probe        = 0;
            const CUresult result = api.cuDeviceGet(&probe, selected);
            if (result != CUDA_SUCCESS)
                throw DeviceUnavailable(
                    selected,
                    "this machine has " + std::to_string(count) + " CUDA device(s); " +
                        cudaFailureText(api, result));
        }
    }
    const int ordinal = configuredCudaOrdinal().value_or(0);
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
    // What `std::profile` reports as the device: the ordinal this backend chose.
    if (profile_hooks::device) {
        profile_hooks::device(("cuda:" + std::to_string(ordinal)).c_str());
    }
    int count = 0;
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
    devicePool_.bindContext(context);
    hostPool_.bindContext(context);
}
Compiled::Compiled(
    Api &api, CUcontext context, const std::string &source, const std::vector<std::string> &options)
    : api(api), context(context), source(source), options(options) {}
Compiled::~Compiled() {
    if (module && api.cuCtxPushCurrent(context) == CUDA_SUCCESS) {
        api.cuModuleUnload(module);
        CUcontext popped;
        api.cuCtxPopCurrent(&popped);
    }
}
ModuleLease::ModuleLease(std::shared_ptr<Backend> backend, std::shared_ptr<Compiled> compiled)
    : backend(std::move(backend)), compiled(std::move(compiled)) {}
Backend::~Backend() {
    // Free pooled idle blocks while the primary context is still valid. Pool
    // slots hold no Backend reference, so nothing here can extend this object's
    // lifetime; active leases have already dropped their references.
    devicePool_.trim();
    hostPool_.trim();
    if (context) {
        cache.clear();
        api.cuDevicePrimaryCtxRelease(device);
    }
}
MemoryPool &Backend::pool(PoolKind kind) noexcept {
    return kind == PoolKind::Device ? devicePool_ : hostPool_;
}
void Backend::disableReuse() noexcept {
    // Stop reuse on both pools before any owner drops a lease share, so a block
    // that may still be the target of pending work is abandoned rather than
    // cached or freed.
    devicePool_.disableReuse();
    hostPool_.disableReuse();
}
std::vector<std::string> Backend::compileOptions() const {
    // These are language defaults, not permissions to relax numerical semantics.
    return {
        architecture,
        "--std=c++14",
        "--fmad=false",
        "--prec-div=true",
        "--prec-sqrt=true",
        "--ftz=false"};
}
bool Backend::cached(const std::string &source) {
    const auto options = compileOptions();
    std::lock_guard lock(mutex);
    return std::any_of(cache.begin(), cache.end(), [&](const auto &entry) {
        return entry->source == source && entry->options == options;
    });
}
ModuleLease Backend::module(const std::string &source) {
    const auto options = compileOptions();
    // Device, context and NVRTC library are fixed for this backend's lifetime.
    // Hold the cache lock only for lookup/publication, never for compilation or
    // execution. Each caller activates its context on its own thread.
    auto find = [&]() -> std::shared_ptr<Compiled> {
        for (size_t i = 0; i < cache.size(); ++i) {
            if (cache[i]->source != source || cache[i]->options != options)
                continue;
            auto found = cache[i];
            std::rotate(cache.begin() + i, cache.begin() + i + 1, cache.end());
            return found;
        }
        return {};
    };
    {
        std::lock_guard lock(mutex);
        if (auto found = find())
            return {shared_from_this(), std::move(found)};
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
    std::vector<const char *> arguments;
    for (const auto &option : options)
        arguments.push_back(option.c_str());
    // The NVRTC compile of one region: `std::profile` reports it as device compilation, whose
    // cost is the region's first use and is cached afterwards.
    const auto compileStart = std::chrono::steady_clock::now();
    auto status =
        api.nvrtcCompileProgram(program, static_cast<int>(arguments.size()), arguments.data());
    if (profile_hooks::deviceCompile) {
        profile_hooks::deviceCompile(
            "nvrtc",
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now() - compileStart)
                                      .count()));
    }
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
    auto compiled = std::make_shared<Compiled>(api, context, source, options);
    Current current(api, context);
    api.check(api.cuModuleLoadData(&compiled->module, ptx.data()), "load PTX");
    std::shared_ptr<Compiled> evicted;
    {
        std::lock_guard lock(mutex);
        // Concurrent misses may compile the same source. Publish only one entry;
        // release duplicate/evicted modules after dropping the cache mutex.
        if (auto found = find())
            return {shared_from_this(), std::move(found)};
        cache.push_back(compiled);
        if (cache.size() > 32) {
            evicted = std::move(cache.front());
            cache.pop_front();
        }
    }
    return {shared_from_this(), std::move(compiled)};
}
namespace {
/// Build the one back end of the process, turning "the run named a device and there is none"
/// into the diagnostic that names it. Everything else is a capability miss as before.
std::shared_ptr<Backend> createBackend() {
    const auto &selection = camel::execute::selectedDevices();
    if (selection.given && (selection.cpu || selection.cuda.empty()))
        throw BackendUnavailable(
            "the run selected the host only (--devices cpu) and does not use a CUDA device");
    try {
        return std::make_shared<Backend>();
    } catch (const BackendUnavailable &e) {
        // The driver or NVRTC is not there. Without `--devices` that is a capability miss (the
        // host runs the program); with it the run named a device, so the report names it.
        if (selection.given && !selection.cuda.empty())
            throw DeviceUnavailable(selection.cuda.front(), e.what());
        throw;
    }
}
} // namespace
std::shared_ptr<Backend> sharedBackend() {
    static auto instance = createBackend();
    return instance;
}
Backend &backend() { return *sharedBackend(); }
} // namespace camel::tiles::cuda_detail
#endif
