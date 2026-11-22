// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Inspect memory on the stopped program thread. No polling, arbitrary addresses or
// unbounded byte/object responses are exposed by the DAP memory extension.

#include "camel/core/debug_breakpoint.h"
#include "camel/core/mm/profiler.h"
#include "debug_core.h"
#include <format>

namespace debugger::core {
using json = nlohmann::json;
namespace {
#ifdef NDEBUG
constexpr bool allocationSupported = false;
#else
constexpr bool allocationSupported = true;
#endif
json failed(const std::string &message) { return {{"ok", false}, {"error", message}}; }
void addressStrings(json &object) {
    for (const char *key : {"addr", "start", "top", "end", "regionStart"}) {
        auto at = object.find(key);
        if (at != object.end() && at->is_number_unsigned())
            *at = std::format("0x{:x}", at->get<uint64_t>());
    }
}
} // namespace

json DebugCore::memoryCommand(const std::string &name, const json &args) {
    if (name == "allocationBreakpoints") {
        if (args.contains("enabled")) {
            if (!args["enabled"].is_boolean() || !args.contains("spaces") ||
                !args["spaces"].is_array())
                return failed("enabled is boolean and spaces is an array");
            if (args["enabled"].get<bool>() && !allocationSupported)
                return failed("Allocation breakpoints require a Debug Camel build.");
            std::set<std::string> spaces;
            for (const auto &v : args["spaces"]) {
                if (!v.is_string())
                    return failed("allocation spaces are strings");
                const auto space = v.get<std::string>();
                if (space != "auto.birth" && space != "auto.haven" && space != "auto.elder" &&
                    space != "auto.large" && space != "bump" && space != "meta" && space != "perm")
                    return failed("unknown allocation space: " + space);
                spaces.insert(space);
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                allocationEnabled_ = args["enabled"].get<bool>();
                allocationSpaces_  = std::move(spaces);
            }
            if (args["enabled"].get<bool>())
                camel::DebugBreakpoint::EnableType("alloc_boundary");
            else
                camel::DebugBreakpoint::DisableType("alloc_boundary");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return {
            {"ok", true},
            {"supported", allocationSupported},
            {"enabled", allocationEnabled_},
            {"spaces", allocationSpaces_}};
    }
    if (name == "memorySnapshot") {
        return runWhileStopped([this] {
            json out = json::parse(camel::core::mm::profiler::snapshotToJson(false));
            for (auto &region : out["regions"]) {
                // Overview keeps aggregate counts, not every object's address/header.
                region.erase("objects");
                region.erase("freeBlocks");
                addressStrings(region);
            }
            out["allocation"] = memoryCommand("allocationBreakpoints", json::object());
            out["ok"]         = true;
            return out;
        });
    }
    const auto region = args.find("region"), offset = args.find("offset"),
               limit      = args.find("limit");
    const int64_t maximum = name == "regionMemory" ? 4096 : 200;
    if (region == args.end() || !region->is_string() || offset == args.end() ||
        !offset->is_number_integer() || offset->get<int64_t>() < 0 || limit == args.end() ||
        !limit->is_number_integer() || limit->get<int64_t>() <= 0 ||
        limit->get<int64_t>() > maximum)
        return failed(
            "region is a name, offset is nonnegative and limit is bounded (bytes <=4096, objects "
            "<=200)");
    const auto regionName = region->get<std::string>();
    const auto start = offset->get<size_t>(), count = limit->get<size_t>();
    return runWhileStopped([name, regionName, start, count] {
        const auto raw =
            name == "regionMemory"
                ? camel::core::mm::profiler::regionMemoryRawToJson(regionName.c_str(), start, count)
                : camel::core::mm::profiler::regionObjectsToJson(regionName.c_str(), start, count);
        json out = json::parse(raw);
        if (out.contains("error")) {
            out["ok"] = false;
            return out;
        }
        addressStrings(out);
        if (out.contains("objects"))
            for (auto &object : out["objects"])
                addressStrings(object);
        out["ok"] = true;
        return out;
    });
}

bool DebugCore::allocationEvent(size_t size, const char *space, const void *ptr) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!allocationEnabled_)
            return false; // legacy REPL type hook still owns its events
        if (!ptr || state_ != State::Running || std::this_thread::get_id() != programThread_ ||
            (!allocationSpaces_.empty() && !allocationSpaces_.contains(space ? space : "")))
            return true;
    }
    // An allocation inside index maintenance must not stop while the index lock is held.
    if (indexMutex_.busy())
        return true;
    static thread_local bool inside = false;
    if (inside)
        return true;
    inside = true;
    struct Clear {
        bool &flag;
        ~Clear() { flag = false; }
    } clear{inside};
    stop(
        "pause",
        {{"allocation", {{"phase", "after"}, {"space", space ? space : ""}, {"size", size}}}});
    return true;
}
} // namespace debugger::core
