// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#if defined(_WIN32) && !defined(CAMEL_DLL_EXPORTS)
#define CAMEL_GLOBAL_CONFIG_API __declspec(dllimport)
#elif defined(_WIN32)
#define CAMEL_GLOBAL_CONFIG_API __declspec(dllexport)
#else
#define CAMEL_GLOBAL_CONFIG_API
#endif

class GlobalConfig {
  public:
    static CAMEL_GLOBAL_CONFIG_API void SetInspectionMode(bool enabled);
    static CAMEL_GLOBAL_CONFIG_API bool IsInspectionMode();
};

#ifdef CAMEL_DLL_EXPORTS
extern CAMEL_GLOBAL_CONFIG_API bool gCamelInspectionMode;

class InternalGlobalConfig {
  public:
    static void SetInspectionMode(bool enabled) { gCamelInspectionMode = enabled; }
    static bool IsInspectionMode() { return gCamelInspectionMode; }
};
#endif
