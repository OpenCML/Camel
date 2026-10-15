/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Exact structural keys before CUDA printing, bounded LRU and per-call storage plans.
#include "tiles/internal/codegen_cache.h"
#include <algorithm>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace camel::tiles {
namespace {
constexpr size_t maxEntries = 32, maxRetainedBytes = 16 * 1024 * 1024;

class Key {
    std::string value;

  public:
    void number(uint64_t n) {
        for (unsigned i = 0; i < 8; ++i)
            value.push_back(static_cast<char>((n >> (i * 8)) & 255));
    }
    void text(const std::string &s) {
        number(s.size());
        value.append(s);
    }
    template <class T> void numbers(const std::vector<T> &v) {
        number(v.size());
        for (auto n : v)
            number(static_cast<uint64_t>(n));
    }
    std::string finish() { return std::move(value); }
};
std::string planKey(const Program &p, ScheduleOptions options) {
    Key key;
    key.number(1); // Process-local key schema; not a persistent semantic fingerprint.
    key.number(options.fuseElementwise);
    key.number(options.parallelMax);
    key.number(p.nodes.size());
    for (const auto &n : p.nodes) {
        key.text(n.op);
        key.number(static_cast<uint64_t>(n.dtype));
        key.numbers(n.shape);
        key.numbers(n.inputs);
        key.numbers(n.attributes); // Includes graph constant64 literal bits.
        key.number(n.offset);
        key.numbers(n.strides);
        // data/scalarBits and resident ownership only supply runtime buffer
        // contents. Physical allocation slots are recomputed on every call.
    }
    key.numbers(p.outputs);
    key.number(p.launches.size());
    for (const auto &s : p.launches) {
        key.number(s.node);
        key.numbers(s.members);
        key.number(s.elements);
        key.text(s.name);
        key.text(s.implementation);
        key.number(s.blockX);
        key.number(s.blockY);
        key.number(s.gridX);
        key.number(s.gridY);
    }
    return key.finish();
}
struct Entry {
    std::string key, source;
    std::vector<Launch> launches;
    size_t retainedBytes() const {
        size_t total = sizeof(Entry) + key.capacity() + source.capacity() +
                       launches.capacity() * sizeof(Launch);
        for (const auto &s : launches)
            total += s.name.capacity() + s.implementation.capacity() +
                     s.members.capacity() * sizeof(size_t);
        return total;
    }
};
struct Cache {
    std::mutex mutex;
    std::list<Entry> entries;
    size_t hits = 0, misses = 0, retainedBytes = 0;
};
Cache &cache() {
    static Cache instance;
    return instance;
}
} // namespace

kernel::GenerationCacheStats kernel::generationCacheStats() {
    auto &c = cache();
    std::lock_guard lock(c.mutex);
    return {c.hits, c.misses, c.entries.size(), c.retainedBytes};
}

void generate(Program &p, ScheduleOptions options) {
    schedule(p, options);
    auto key = planKey(p, options);
    auto &c  = cache();
    {
        std::lock_guard lock(c.mutex);
        const auto found = std::find_if(c.entries.begin(), c.entries.end(), [&](const auto &e) {
            return e.key == key;
        });
        if (found != c.entries.end()) {
            p.source   = found->source;
            p.launches = found->launches;
            c.entries.splice(c.entries.begin(), c.entries, found);
            ++c.hits;
            validatePlan(p);
            return;
        }
        ++c.misses;
    }
    // Expensive printing does not hold the shared cache lock. Duplicate racing
    // misses are harmless; only one copy will be retained below.
    kernel::emitCUDA(p);
    validatePlan(p);
    Entry entry{std::move(key), p.source, p.launches};
    const auto retained = entry.retainedBytes();
    if (retained > maxRetainedBytes)
        return;
    std::lock_guard lock(c.mutex);
    if (std::any_of(c.entries.begin(), c.entries.end(), [&](const auto &e) {
            return e.key == entry.key;
        }))
        return;
    while (!c.entries.empty() &&
           (c.entries.size() >= maxEntries || c.retainedBytes > maxRetainedBytes - retained)) {
        c.retainedBytes -= c.entries.back().retainedBytes();
        c.entries.pop_back();
    }
    c.entries.push_front(std::move(entry));
    c.retainedBytes += retained;
}
} // namespace camel::tiles
