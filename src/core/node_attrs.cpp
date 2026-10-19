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
#include <charconv>
#include <deque>
#include <format>
#include <limits>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

namespace camel::core::attr {

namespace {

/// The canonical form of a set: sorted by key id, one entry per key, no zero value.
using Canonical = std::vector<std::pair<AttrKey, uint64_t>>;

struct Registry {
    std::shared_mutex mutex;
    std::deque<std::string> names{std::string()}; // a deque: views of its strings stay valid
    std::unordered_map<std::string, AttrKey> keys;
    std::vector<Canonical> sets{Canonical{}};
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

Canonical canonical(std::span<const Attr> attrs) {
    std::map<AttrKey, uint64_t> merged;
    for (const Attr &a : attrs) {
        if (a.key != kNoKey && a.value != 0) {
            merged[a.key] |= a.value;
        }
    }
    return Canonical(merged.begin(), merged.end());
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

} // namespace

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
    r.keys.emplace(std::string(name), id);
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

AttrSetId intern(std::span<const Attr> attrs) { return internCanonical(canonical(attrs)); }

AttrSetId single(AttrKey key, uint64_t value) {
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

uint64_t valueOf(AttrSetId set, AttrKey key) {
    for (const auto &[k, v] : canonicalOf(set)) {
        if (k == key) {
            return v;
        }
    }
    return 0;
}

AttrSetId unite(AttrSetId a, AttrSetId b) {
    if (a == kNoAttrs || a == b) {
        return b;
    }
    if (b == kNoAttrs) {
        return a;
    }
    std::vector<Attr> all;
    for (const auto &[k, v] : canonicalOf(a)) {
        all.push_back({k, v});
    }
    for (const auto &[k, v] : canonicalOf(b)) {
        all.push_back({k, v});
    }
    return intern(all);
}

AttrSetId intersect(AttrSetId a, AttrSetId b) {
    if (a == b) {
        return a;
    }
    if (a == kNoAttrs || b == kNoAttrs) {
        return kNoAttrs;
    }
    std::vector<Attr> both;
    const Canonical cb = canonicalOf(b);
    for (const auto &[k, v] : canonicalOf(a)) {
        for (const auto &[kb, vb] : cb) {
            if (kb == k) {
                both.push_back({k, v & vb});
            }
        }
    }
    return intern(both);
}

std::string text(AttrSetId set) {
    std::string out;
    for (const Attr &a : entries(set)) {
        if (!out.empty()) {
            out += ", ";
        }
        out += keyName(a.key);
        if (a.value != 1) {
            out += std::format("={:#x}", a.value);
        }
    }
    return out;
}

std::optional<AttrSetId> parse(std::string_view text) {
    std::vector<Attr> attrs;
    while (!text.empty()) {
        const size_t comma  = text.find(',');
        std::string_view it = text.substr(0, comma);
        text                = comma == std::string_view::npos ? std::string_view{}
                                                              : text.substr(comma + 1);
        while (!it.empty() && it.front() == ' ') {
            it.remove_prefix(1);
        }
        while (!it.empty() && it.back() == ' ') {
            it.remove_suffix(1);
        }
        uint64_t value         = 1;
        const size_t eq        = it.find('=');
        std::string_view name  = it.substr(0, eq);
        if (eq != std::string_view::npos) {
            std::string_view digits = it.substr(eq + 1);
            int base                = 10;
            if (digits.starts_with("0x")) {
                digits.remove_prefix(2);
                base = 16;
            }
            const auto [end, ec] =
                std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
            if (digits.empty() || ec != std::errc() || end != digits.data() + digits.size()) {
                return std::nullopt;
            }
        }
        if (!validName(name)) {
            return std::nullopt;
        }
        attrs.push_back({key(name), value});
    }
    return intern(attrs);
}

} // namespace camel::core::attr
