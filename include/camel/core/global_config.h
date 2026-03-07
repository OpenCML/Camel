/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 07, 2026
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
