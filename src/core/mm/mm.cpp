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
 * Created: Dec. 10, 2025
 * Updated: May. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/mm.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace camel::core::mm {

namespace detail {
std::atomic_bool autoSpaceSafepointSlowPath{false};
}

namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string envString(const char *name) {
#ifdef _WIN32
    char *buffer = nullptr;
    size_t size  = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
        return {};
    }
    std::string value(buffer, size > 0 ? size - 1 : 0);
    std::free(buffer);
    return value;
#else
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string{};
#endif
}

bool envFlag(const char *name) {
    std::string value = envString(name);
    if (value.empty()) {
        return false;
    }
    value = lowerAscii(std::move(value));
    return value != "0" && value != "false" && value != "off" && value != "no";
}

size_t envSize(const char *name) {
    std::string raw = envString(name);
    if (raw.empty()) {
        return 0;
    }
    char *end                = nullptr;
    unsigned long long value = std::strtoull(raw.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<size_t>(value) : 0;
}

GenerationalAllocatorWithGC::CollectionKind envCollectionKind() {
    std::string value = envString("CAMEL_GC_STRESS_MODE");
    if (value.empty()) {
        return GenerationalAllocatorWithGC::CollectionKind::Major;
    }
    value = lowerAscii(std::move(value));
    if (value == "minor") {
        return GenerationalAllocatorWithGC::CollectionKind::Minor;
    }
    if (value == "both" || value == "minor+major" || value == "all") {
        return GenerationalAllocatorWithGC::CollectionKind::MinorAndMajor;
    }
    return GenerationalAllocatorWithGC::CollectionKind::Major;
}

GenerationalAllocatorWithGC::DebugConfig debugConfigFromEnv() {
    GenerationalAllocatorWithGC::DebugConfig config{};
    config.stressEveryNAllocations = envSize("CAMEL_GC_STRESS_ALLOC");
    config.stressEveryNSafepoints  = envSize("CAMEL_GC_STRESS_SAFEPOINT");
    config.stressCollection        = envCollectionKind();
    config.logMovements            = envFlag("CAMEL_GC_LOG_MOVES");

    std::string verifyRaw = envString("CAMEL_GC_VERIFY");
    if (!verifyRaw.empty()) {
        std::string value = lowerAscii(std::move(verifyRaw));
        if (value == "before" || value == "pre") {
            config.verifyBeforeGC = true;
        } else if (value == "after" || value == "post") {
            config.verifyAfterGC = true;
        } else if (value != "0" && value != "false" && value != "off" && value != "no") {
            config.verifyBeforeGC = true;
            config.verifyAfterGC  = true;
        }
    }
    return config;
}

} // namespace

// Managed automatically by the GC system and must remain reachable from roots.
GenerationalAllocatorWithGC &autoSpace() {
    static auto *allocator = [] {
        auto *instance = new GenerationalAllocatorWithGC(
            GenerationalAllocatorWithGC::Config{
                // Keep the default process-start footprint modest. The current runtime disables
                // young-generation copying, so large preallocated semispaces only add startup cost.
                .birthSize             = 4 * MB,
                .havenSize             = 1 * MB,
                .elderGenSize          = 32 * MB,
                .promotionAgeThreshold = 4,      // Promotion threshold.
                .largeObjThreshold     = 4 * KB, // Large-object threshold.
                .minorGCTriggerRatio   = 0.9f,   // Minor GC trigger ratio.
                .majorGCTriggerRatio   = 0.8f,   // Major GC trigger ratio.
                // Production remains address-stable by default; Phase 3 tests can opt into the
                // copying path explicitly to exercise remembered-set behavior.
                .enableYoungGenCopying = envFlag("CAMEL_GC_ENABLE_YOUNG_COPYING")});
        instance->configureDebug(debugConfigFromEnv());
        return instance;
    }();
    return *allocator;
}

// Metadata space: manually managed allocation and release.
FreeListAllocator &metaSpace() {
    // The process-level meta space uses an intentionally-leaked singleton to avoid static
    // destruction order problems: some global objects may still call metaSpace().free() during
    // shutdown.
    static auto *allocator = new FreeListAllocator(16 * MB, "meta"); // 16 MB
    return *allocator;
}

// Permanent generation: allocate only, never free.

FreeListAllocator &graphSpace() {
    static auto *allocator = new FreeListAllocator(64 * MB, "graph"); // 64 MB
    return *allocator;
}
BumpPointerAllocator &permSpace() {
    static auto *allocator = new BumpPointerAllocator(32 * MB, "perm"); // 32 MB
    return *allocator;
}

} // namespace camel::core::mm
