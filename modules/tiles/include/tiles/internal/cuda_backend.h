/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Internal CUDA loader and compiled-module cache. Not a public module API.
#pragma once
#ifdef CAMEL_TILES_CUDA
#include <cuda.h>
#include <deque>
#include <memory>
#include <mutex>
#include <nvrtc.h>
#include <string>
#include <vector>

namespace camel::tiles::cuda_detail {
class Library {
    void *handle_   = nullptr;
    void *builtins_ = nullptr;
    void *findSymbol(const char *name);

  public:
    explicit Library(const std::vector<std::string> &paths);
    ~Library();
    Library(const Library &) = delete;
    template <class T> T symbol(const char *name) { return reinterpret_cast<T>(findSymbol(name)); }
};
struct Api {
    Library driver, rtc;
    Api();
    decltype(&::cuInit) cuInit;
    decltype(&::cuDriverGetVersion) cuDriverGetVersion;
    decltype(&::cuDeviceGet) cuDeviceGet;
    decltype(&::cuDeviceGetName) cuDeviceGetName;
    decltype(&::cuDeviceGetAttribute) cuDeviceGetAttribute;
    decltype(&::cuDevicePrimaryCtxRetain) cuDevicePrimaryCtxRetain;
    decltype(&::cuDevicePrimaryCtxRelease) cuDevicePrimaryCtxRelease;
    decltype(&::cuCtxPushCurrent) cuCtxPushCurrent;
    decltype(&::cuCtxPopCurrent) cuCtxPopCurrent;
    decltype(&::cuGetErrorName) cuGetErrorName;
    decltype(&::cuGetErrorString) cuGetErrorString;
    decltype(&::cuModuleLoadData) cuModuleLoadData;
    decltype(&::cuModuleUnload) cuModuleUnload;
    decltype(&::cuModuleGetFunction) cuModuleGetFunction;
    decltype(&::cuMemAlloc) cuMemAlloc;
    decltype(&::cuMemFree) cuMemFree;
    decltype(&::cuMemHostAlloc) cuMemHostAlloc;
    decltype(&::cuMemFreeHost) cuMemFreeHost;
    decltype(&::cuMemcpyHtoD) cuMemcpyHtoD;
    decltype(&::cuMemcpyHtoDAsync) cuMemcpyHtoDAsync;
    decltype(&::cuMemcpyDtoH) cuMemcpyDtoH;
    decltype(&::cuStreamCreate) cuStreamCreate;
    decltype(&::cuStreamSynchronize) cuStreamSynchronize;
    decltype(&::cuStreamDestroy) cuStreamDestroy;
    decltype(&::cuLaunchKernel) cuLaunchKernel;
    decltype(&::cuEventCreate) cuEventCreate;
    decltype(&::cuEventDestroy) cuEventDestroy;
    decltype(&::cuEventRecord) cuEventRecord;
    decltype(&::cuEventSynchronize) cuEventSynchronize;
    decltype(&::cuEventElapsedTime) cuEventElapsedTime;
    decltype(&::nvrtcCreateProgram) nvrtcCreateProgram;
    decltype(&::nvrtcVersion) nvrtcVersion;
    decltype(&::nvrtcCompileProgram) nvrtcCompileProgram;
    decltype(&::nvrtcDestroyProgram) nvrtcDestroyProgram;
    decltype(&::nvrtcGetPTXSize) nvrtcGetPTXSize;
    decltype(&::nvrtcGetPTX) nvrtcGetPTX;
    decltype(&::nvrtcGetProgramLogSize) nvrtcGetProgramLogSize;
    decltype(&::nvrtcGetProgramLog) nvrtcGetProgramLog;
    decltype(&::nvrtcGetErrorString) nvrtcGetErrorString;
    decltype(&::nvrtcGetNumSupportedArchs) nvrtcGetNumSupportedArchs;
    decltype(&::nvrtcGetSupportedArchs) nvrtcGetSupportedArchs;
    void check(CUresult result, const char *action);
    void check(nvrtcResult result, const char *action);
};
struct Current {
    Api &api;
    Current(Api &a, CUcontext context);
    ~Current();
};
struct Compiled {
    Api &api;
    CUcontext context;
    std::string source;
    std::vector<std::string> options;
    CUmodule module = nullptr;
    Compiled(
        Api &api, CUcontext context, const std::string &source,
        const std::vector<std::string> &options);
    ~Compiled();
    Compiled(const Compiled &) = delete;
};
class Backend;
// Destruction releases the module before its backend. Assignment is forbidden
// because memberwise assignment could release the old backend too early.
struct ModuleLease {
    std::shared_ptr<Backend> backend;
    std::shared_ptr<Compiled> compiled;
    ModuleLease(std::shared_ptr<Backend> backend, std::shared_ptr<Compiled> compiled);
    ModuleLease(const ModuleLease &)            = default;
    ModuleLease(ModuleLease &&)                 = default;
    ModuleLease &operator=(const ModuleLease &) = delete;
    ModuleLease &operator=(ModuleLease &&)      = delete;
};
class Backend : public std::enable_shared_from_this<Backend> {
  public:
    Api api;
    CUdevice device;
    CUcontext context = nullptr;
    std::string description, architecture;
    std::deque<std::shared_ptr<Compiled>> cache;
    std::mutex mutex;
    Backend();
    ~Backend();
    std::vector<std::string> compileOptions() const;
    ModuleLease module(const std::string &source);
};
std::shared_ptr<Backend> sharedBackend();
Backend &backend();
} // namespace camel::tiles::cuda_detail
#endif
