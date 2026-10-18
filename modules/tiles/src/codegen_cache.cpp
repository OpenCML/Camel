/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Exact structural keys before CUDA printing, bounded LRU and per-call storage plans.
#include "tiles/internal/codegen_cache.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace camel::tiles {
namespace {
constexpr size_t maxEntries = 32, maxRetainedBytes = 16 * 1024 * 1024;

class Key {
    std::string value;
    std::vector<const LoopRegion *> activeLoops;

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
    void predicate(const core::sem::Predicate &v) {
        number(v.size());
        for (const auto &c : v) {
            number(c.lhs);
            number(static_cast<uint64_t>(c.rel));
            number(c.rhs);
        }
    }
    void indirect(const std::optional<core::sem::Indirect> &v) {
        number(v.has_value());
        if (v) {
            number(v->indexOperand);
            numbers(v->indexCoords);
            number(v->axis);
        }
    }
    void accesses(const std::vector<core::sem::Access> &v) {
        number(v.size());
        for (const auto &a : v) {
            number(static_cast<uint64_t>(a.kind));
            numbers(a.coords);
            predicate(a.valid);
            number(static_cast<uint64_t>(a.onInvalid));
            number(a.padding);
            indirect(a.indirect);
        }
    }
    void indexed(const core::sem::IndexedInstance &v) {
        number(v.exprs.size());
        for (size_t i = 0; i < v.exprs.size(); ++i) {
            const auto &e = v.exprs[static_cast<core::sem::ExprId>(i)];
            number(static_cast<uint64_t>(e.op));
            number(e.value);
            number(e.lhs);
            number(e.rhs);
        }
        number(v.domain.dims.size());
        for (const auto &d : v.domain.dims) {
            text(d.name);
            number(d.extent);
            number(static_cast<uint64_t>(d.role));
        }
        number(v.domain.params.size());
        for (const auto &p : v.domain.params) {
            text(p.name);
            number(static_cast<uint64_t>(p.source));
            number(p.operand);
            number(p.axis);
            number(p.value.has_value());
            if (p.value)
                number(*p.value);
            number(p.definition);
        }
        predicate(v.domain.constraints);
        predicate(v.domain.guard);
        accesses(v.norm);
        accesses(v.with);
        numbers(v.output.coords);
        predicate(v.output.valid);
        number(static_cast<uint64_t>(v.output.mode));
        indirect(v.output.indirect);
        number(v.law);
    }
    void program(const Program &p, bool embeddedConstants = false) {
        number(p.contracts.size());
        for (const auto &contract : p.contracts) {
            text(contract.uri);
            number(contract.key.has_value());
            number(reinterpret_cast<uintptr_t>(contract.declaration));
            if (contract.key)
                text(contract.key->toString());
        }
        number(p.nodes.size());
        for (const auto &n : p.nodes) {
            text(n.op);
            number(static_cast<uint64_t>(n.dtype));
            number(n.permissions);
            number(n.semantic);
            numbers(n.shape);
            numbers(n.inputs);
            numbers(n.attributes); // Includes graph constant64 literal bits.
            number(n.results.size());
            for (const auto &result : n.results) {
                numbers(result.shape);
                number(static_cast<uint64_t>(result.dtype));
            }
            number(n.offset);
            numbers(n.strides);
            number(n.indexing.has_value());
            if (n.indexing)
                indexed(*n.indexing);
            // Outer inputs are invocation buffers and remain outside the key.
            // A loop body's nonempty input payload is embedded in CUDA, whereas
            // its deferred state/capture ports have no payload to serialize.
            if (embeddedConstants) {
                number(n.data.size());
                for (const auto &scalar : n.data)
                    number(std::bit_cast<uint32_t>(scalar));
                number(n.scalarBits.has_value());
                if (n.scalarBits)
                    number(*n.scalarBits);
            }
            number(bool(n.loop));
            if (n.loop) {
                if (std::find(activeLoops.begin(), activeLoops.end(), n.loop.get()) !=
                    activeLoops.end())
                    throw std::runtime_error("tiles source cache: cyclic loop body");
                activeLoops.push_back(n.loop.get());
                numbers(n.loop->domain);
                numbers(n.loop->stateInputs);
                numbers(n.loop->captureInputs);
                numbers(n.loop->nextStates);
                program(n.loop->body, true);
                activeLoops.pop_back();
            }
        }
        numbers(p.outputs);
    }
    std::string finish() { return std::move(value); }
};
std::string planKey(const Program &p, ScheduleOptions options) {
    Key key;
    key.number(8); // Process-local key schema; not a persistent semantic fingerprint.
    key.number(options.fuseElementwise);
    key.number(options.parallelMax);
    key.program(p);
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
                     (s.members.capacity() + s.outputs.capacity()) * sizeof(size_t);
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

void generate(Program &p, ScheduleOptions options, InputBindings bindings) {
    validate(p, bindings);
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
            planStorage(p, bindings);
            return;
        }
        ++c.misses;
    }
    // Expensive printing does not hold the shared cache lock. Duplicate racing
    // misses are harmless; only one copy will be retained below.
    schedule(p, options, bindings);
    kernel::emitCUDA(p);
    validatePlan(p, bindings);
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
