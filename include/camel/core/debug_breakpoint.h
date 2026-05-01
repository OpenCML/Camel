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
 * Created: Feb. 22, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * Generic debug breakpoint API: optional pause points by type (e.g. "alloc", "gc").
 * - No cost in NDEBUG: Hit() is inline no-op; state not referenced.
 * - No global/static init: state is function-local static (lazy on first use).
 * - Call Hit("type", &context) from any module; set handler and enable types from debugger/Web UI.
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace camel {

class DebugBreakpoint {
  public:
    using HandlerFn = void (*)(const char *type, const void *context);

    /// Set global handler; called when an enabled type is hit. (No-op in NDEBUG.)
    static void SetHandler(HandlerFn fn);

    /// Enable/disable a breakpoint type by name (e.g. "alloc", "gc").
    static void EnableType(const char *type);
    static void DisableType(const char *type);
    static bool IsEnabled(const char *type);

    /// Register a known type (so UI can list it). Idempotent. (No-op in NDEBUG.)
    static void RegisterType(const char *type);

    /// Return list of enabled type names; for API/UI.
    static std::vector<std::string> GetEnabledTypes();
    /// Return list of registered known type names; for API/UI.
    static std::vector<std::string> GetKnownTypes();

    /// Call from instrumentation sites: if the type is enabled and a handler
    /// is set, invoke handler(type, context). In NDEBUG this is a no-op with
    /// zero cost.
    static void Hit(const char *type, const void *context);
};

} // namespace camel

#ifdef NDEBUG
inline void camel::DebugBreakpoint::Hit(const char * /*type*/, const void * /*context*/) {}
#endif
