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
 * Author: Camel Contributors
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/node_attrs.h"

#include <algorithm>
#include <bit>
#include <deque>
#include <format>
#include <limits>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace camel::core::attr {

namespace {

/// The canonical form of a set: sorted by key id, one entry per key, no absent value.
using Canonical = std::vector<std::pair<AttrKey, AttrValueId>>;

struct Registry {
    std::shared_mutex mutex;
    // Keys. A deque: views of the names stay valid.
    std::deque<std::string> names{std::string()};
    std::deque<std::optional<KeySpec>> specs{std::nullopt};
    std::unordered_map<std::string, AttrKey> keys;
    // Values, interned by an encoding of their content (process-local: Tuple items are ids).
    std::deque<Value> values{Value{}};
    std::unordered_map<std::string, AttrValueId> valueIds;
    // Sets.
    std::deque<Canonical> sets{Canonical{}};
    std::map<Canonical, AttrSetId> setIds{{Canonical{}, kNoAttrs}};
};

Registry &registry() {
    static Registry r;
    return r;
}

bool validName(std::string_view name) {
    return !name.empty() && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.' || c == ':' || c == '-';
    });
}

std::string encode(const Value &v) {
    std::string out(1, static_cast<char>(v.kind));
    const auto put = [&](uint64_t x) { out.append(reinterpret_cast<const char *>(&x), sizeof x); };
    switch (v.kind) {
    case ValueKind::Unit:
        break;
    case ValueKind::Flags:
        for (uint64_t w : v.words) {
            put(w);
        }
        break;
    case ValueKind::Int:
        put(static_cast<uint64_t>(v.integer));
        break;
    case ValueKind::Symbol:
    case ValueKind::Graph:
        out += v.text;
        break;
    case ValueKind::Tuple:
        for (AttrValueId item : v.items) {
            put(item);
        }
        break;
    }
    return out;
}

AttrValueId internValue(Value v) {
    if (v.kind == ValueKind::Flags) {
        while (!v.words.empty() && v.words.back() == 0) {
            v.words.pop_back();
        }
        if (v.words.empty()) {
            return kNoValue;
        }
    }
    std::string code = encode(v);
    Registry &r      = registry();
    {
        std::shared_lock lock(r.mutex);
        if (auto it = r.valueIds.find(code); it != r.valueIds.end()) {
            return it->second;
        }
    }
    std::unique_lock lock(r.mutex);
    if (auto it = r.valueIds.find(code); it != r.valueIds.end()) {
        return it->second;
    }
    const auto id = static_cast<AttrValueId>(r.values.size());
    r.values.push_back(std::move(v));
    r.valueIds.emplace(std::move(code), id);
    return id;
}

AttrSetId internCanonical(Canonical c) {
    Registry &r = registry();
    {
        std::shared_lock lock(r.mutex);
        if (auto it = r.setIds.find(c); it != r.setIds.end()) {
            return it->second;
        }
    }
    std::unique_lock lock(r.mutex);
    if (auto it = r.setIds.find(c); it != r.setIds.end()) {
        return it->second;
    }
    const auto id = static_cast<AttrSetId>(r.sets.size());
    r.sets.push_back(c);
    r.setIds.emplace(std::move(c), id);
    return id;
}

Canonical canonicalOf(AttrSetId set) {
    if (set == kNoAttrs) {
        return {};
    }
    Registry &r = registry();
    std::shared_lock lock(r.mutex);
    return set < r.sets.size() ? r.sets[set] : Canonical{};
}

AttrValueId combineBy(AttrKey key, AttrValueId outer, AttrValueId inner) {
    const auto spec = specOf(key);
    if (spec && spec->combine) {
        return spec->combine(outer, inner);
    }
    return inner != kNoValue ? inner : outer;
}

std::optional<AttrValueId> mergeBy(AttrKey key, AttrValueId a, AttrValueId b) {
    const auto spec = specOf(key);
    if (spec && spec->merge) {
        return spec->merge(a, b);
    }
    return a == b ? std::optional<AttrValueId>(a) : std::nullopt;
}

} // namespace

// ---- Values ------------------------------------------------------------------------------

AttrValueId unit() { return internValue(Value{.kind = ValueKind::Unit}); }

AttrValueId flags(std::span<const uint32_t> bits) {
    std::vector<uint64_t> words;
    for (uint32_t bit : bits) {
        if (words.size() <= bit / 64) {
            words.resize(bit / 64 + 1, 0);
        }
        words[bit / 64] |= uint64_t{1} << (bit % 64);
    }
    return flagWords(std::move(words));
}

AttrValueId flagWords(std::vector<uint64_t> words) {
    return internValue(Value{.kind = ValueKind::Flags, .words = std::move(words)});
}

AttrValueId integer(int64_t v) { return internValue(Value{.kind = ValueKind::Int, .integer = v}); }

AttrValueId symbol(std::string_view text) {
    return internValue(Value{.kind = ValueKind::Symbol, .text = std::string(text)});
}

AttrValueId graphRef(camel::runtime::GraphId graph) {
    return internValue(Value{.kind = ValueKind::Graph, .text = camel::runtime::toText(graph)});
}

AttrValueId tuple(std::span<const AttrValueId> items) {
    return internValue(
        Value{
            .kind  = ValueKind::Tuple,
            .items = std::vector<AttrValueId>(items.begin(), items.end())});
}

Value value(AttrValueId id) {
    Registry &r = registry();
    std::shared_lock lock(r.mutex);
    return id < r.values.size() ? r.values[id] : Value{};
}

std::string valueText(AttrValueId id) {
    if (id == kNoValue) {
        return {};
    }
    const Value v = value(id);
    switch (v.kind) {
    case ValueKind::Unit:
        return "()";
    case ValueKind::Flags: {
        std::string out = "{";
        bool first      = true;
        for (size_t w = 0; w < v.words.size(); ++w) {
            for (uint64_t bits = v.words[w]; bits != 0; bits &= bits - 1) {
                out += (first ? "" : ", ") + std::to_string(w * 64 + std::countr_zero(bits));
                first = false;
            }
        }
        return out + "}";
    }
    case ValueKind::Int:
        return std::to_string(v.integer);
    case ValueKind::Symbol:
        return "\"" + v.text + "\"";
    case ValueKind::Graph:
        return "@" + v.text;
    case ValueKind::Tuple: {
        std::string out = "(";
        for (size_t i = 0; i < v.items.size(); ++i) {
            out += (i == 0 ? "" : ", ") + valueText(v.items[i]);
        }
        return out + ")";
    }
    }
    return {};
}

// ---- Rules -------------------------------------------------------------------------------

namespace rules {

namespace {
std::vector<uint64_t> wordsOf(AttrValueId id) {
    return id == kNoValue ? std::vector<uint64_t>{} : value(id).words;
}
} // namespace

CombineRule unionFlags() {
    return [](AttrValueId outer, AttrValueId inner) {
        std::vector<uint64_t> a       = wordsOf(outer);
        const std::vector<uint64_t> b = wordsOf(inner);
        a.resize(std::max(a.size(), b.size()), 0);
        for (size_t i = 0; i < b.size(); ++i) {
            a[i] |= b[i];
        }
        return flagWords(std::move(a));
    };
}

MergeRule intersectFlags() {
    return [](AttrValueId x, AttrValueId y) -> std::optional<AttrValueId> {
        std::vector<uint64_t> a       = wordsOf(x);
        const std::vector<uint64_t> b = wordsOf(y);
        a.resize(std::min(a.size(), b.size()));
        for (size_t i = 0; i < a.size(); ++i) {
            a[i] &= b[i];
        }
        return flagWords(std::move(a));
    };
}

CombineRule inner() {
    return [](AttrValueId outer, AttrValueId in) { return in != kNoValue ? in : outer; };
}

MergeRule equalOnly() {
    return [](AttrValueId a, AttrValueId b) -> std::optional<AttrValueId> {
        return a == b ? std::optional<AttrValueId>(a) : std::nullopt;
    };
}

CombineRule chain() {
    return [](AttrValueId outer, AttrValueId in) {
        if (outer == kNoValue || in == kNoValue) {
            return in != kNoValue ? in : outer;
        }
        std::vector<AttrValueId> items;
        for (AttrValueId part : {outer, in}) {
            const Value v = value(part);
            if (v.kind == ValueKind::Tuple) {
                items.insert(items.end(), v.items.begin(), v.items.end());
            } else {
                items.push_back(part);
            }
        }
        return tuple(items);
    };
}

} // namespace rules

// ---- Keys --------------------------------------------------------------------------------

AttrKey key(std::string_view name) {
    if (!validName(name)) {
        throw std::invalid_argument(std::format("'{}' is not an attribute key name", name));
    }
    Registry &r = registry();
    {
        std::shared_lock lock(r.mutex);
        if (auto it = r.keys.find(std::string(name)); it != r.keys.end()) {
            return it->second;
        }
    }
    std::unique_lock lock(r.mutex);
    if (auto it = r.keys.find(std::string(name)); it != r.keys.end()) {
        return it->second;
    }
    if (r.names.size() > std::numeric_limits<AttrKey>::max()) {
        throw std::length_error("too many attribute keys");
    }
    const auto id = static_cast<AttrKey>(r.names.size());
    r.names.emplace_back(name);
    r.specs.emplace_back(std::nullopt);
    r.keys.emplace(std::string(name), id);
    return id;
}

AttrKey registerKey(std::string_view name, KeySpec spec) {
    const AttrKey id = key(name);
    Registry &r      = registry();
    std::unique_lock lock(r.mutex);
    r.specs[id] = std::move(spec);
    return id;
}

std::optional<AttrKey> findKey(std::string_view name) {
    Registry &r = registry();
    std::shared_lock lock(r.mutex);
    if (auto it = r.keys.find(std::string(name)); it != r.keys.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string_view keyName(AttrKey key) {
    Registry &r = registry();
    std::shared_lock lock(r.mutex);
    return key < r.names.size() ? std::string_view(r.names[key]) : std::string_view();
}

std::optional<KeySpec> specOf(AttrKey key) {
    Registry &r = registry();
    std::shared_lock lock(r.mutex);
    return key < r.specs.size() ? r.specs[key] : std::nullopt;
}

// ---- Sets --------------------------------------------------------------------------------

AttrSetId intern(std::span<const Attr> attrs) {
    std::map<AttrKey, AttrValueId> merged;
    for (const Attr &a : attrs) {
        if (a.key == kNoKey || a.value == kNoValue) {
            continue;
        }
        if (const auto spec = specOf(a.key); spec && value(a.value).kind != spec->kind) {
            throw std::invalid_argument(
                std::format(
                    "attribute '{}': the value {} is not of the key's kind",
                    keyName(a.key),
                    valueText(a.value)));
        }
        auto [it, inserted] = merged.emplace(a.key, a.value);
        if (!inserted) {
            it->second = combineBy(a.key, it->second, a.value);
        }
    }
    Canonical c;
    for (const auto &[k, v] : merged) {
        if (v != kNoValue) {
            c.emplace_back(k, v);
        }
    }
    return internCanonical(std::move(c));
}

AttrSetId single(AttrKey key, AttrValueId value) {
    const Attr a{key, value};
    return intern(std::span<const Attr>(&a, 1));
}

std::vector<Attr> entries(AttrSetId set) {
    std::vector<std::pair<std::string, Attr>> named;
    for (const auto &[k, v] : canonicalOf(set)) {
        named.emplace_back(std::string(keyName(k)), Attr{k, v});
    }
    std::ranges::sort(named, {}, &std::pair<std::string, Attr>::first);
    std::vector<Attr> result;
    result.reserve(named.size());
    for (auto &[name, a] : named) {
        result.push_back(a);
    }
    return result;
}

AttrValueId valueOf(AttrSetId set, AttrKey key) {
    for (const auto &[k, v] : canonicalOf(set)) {
        if (k == key) {
            return v;
        }
    }
    return kNoValue;
}

AttrSetId unite(AttrSetId outer, AttrSetId inner) {
    if (outer == kNoAttrs) {
        return inner;
    }
    if (inner == kNoAttrs) {
        return outer;
    }
    std::vector<Attr> all;
    for (const auto &[k, v] : canonicalOf(outer)) {
        all.push_back({k, v});
    }
    for (const auto &[k, v] : canonicalOf(inner)) {
        all.push_back({k, v});
    }
    return intern(all);
}

std::optional<AttrSetId> merge(AttrSetId a, AttrSetId b) {
    if (a == b) {
        return a;
    }
    std::map<AttrKey, std::pair<AttrValueId, AttrValueId>> both;
    for (const auto &[k, v] : canonicalOf(a)) {
        both[k].first = v;
    }
    for (const auto &[k, v] : canonicalOf(b)) {
        both[k].second = v;
    }
    std::vector<Attr> kept;
    for (const auto &[k, values] : both) {
        const auto merged = mergeBy(k, values.first, values.second);
        if (!merged.has_value()) {
            return std::nullopt;
        }
        kept.push_back({k, *merged});
    }
    return intern(kept);
}

std::string text(AttrSetId set) {
    std::string out;
    for (const Attr &a : entries(set)) {
        if (!out.empty()) {
            out += ", ";
        }
        out += keyName(a.key);
        if (value(a.value).kind != ValueKind::Unit) {
            out += "=" + valueText(a.value);
        }
    }
    return out;
}

} // namespace camel::core::attr
