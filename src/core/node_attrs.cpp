// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/node_attrs.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <deque>
#include <format>
#include <limits>
#include <map>
#include <mutex>
#include <ostream>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "camel/utils/number_text.h"

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
    case ValueKind::Float:
        // The bits: 0.0 and -0.0, and two NaNs, are different facts.
        put(std::bit_cast<uint64_t>(v.real));
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

AttrValueId real(double v) { return internValue(Value{.kind = ValueKind::Float, .real = v}); }

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
    case ValueKind::Float:
        // A NaN is interned by its bits (two NaNs are different facts, see attr::real): a dump
        // that says only "nan" would hide the difference, so it shows the pattern.
        if (std::isnan(v.real)) {
            return std::format("nan(0x{:016x})", std::bit_cast<uint64_t>(v.real));
        }
        return camel::utils::floatText(v.real);
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

// ---- Fates in the consumers ---------------------------------------------------------------

namespace {

/// The table (Fate above): every attribute the tree can write, one row per key and value.
const std::vector<FateRow> &fateTable() {
    static const std::vector<FateRow> rows = [] {
        std::vector<FateRow> t;
        const auto add = [&t](
                             std::string_view key,
                             std::string_view value,
                             std::string_view writtenBy,
                             Fate interpreter,
                             Fate dump,
                             Fate onnx,
                             Fate tiles,
                             std::string_view note) {
            t.push_back(FateRow{key, value, writtenBy, interpreter, dump, onnx, tiles, note});
        };
        const Fate carried = Fate::Carried;
        const Fate refused = Fate::Refused;
        const Fate notNode = Fate::NotOnANode;

        // Node attributes: what a pass or a decorator writes on a node.
        add("strict",
            "",
            "#strict (a function or block)",
            carried,
            carried,
            refused,
            carried,
            "ONNX cannot promise canonical arithmetic bit for bit (design principles 7.4); "
            "Tiles' kernels are the canonical algorithm, so the tag keeps its meaning there.");
        add("independent",
            "",
            "#independent (a function or block)",
            carried,
            carried,
            carried,
            carried,
            "a scheduling permission: the executor, both dumps and the model's metadata record "
            "it, and Tiles schedules the elements independently.");
        add("tiles.placement",
            "cpu",
            "#cpu (a function or block)",
            carried,
            carried,
            carried,
            carried,
            "Tiles reads it to choose the device; the ONNX model records the request in its "
            "metadata and the session's execution providers decide where it runs.");
        add("tiles.placement",
            "gpu",
            "#gpu (a function or block)",
            carried,
            carried,
            carried,
            carried,
            "Tiles reads it to choose the device; the ONNX model records the request in its "
            "metadata and the session's execution providers decide where it runs.");
        add("tensor.kernel",
            "library",
            "tensor::fuse, and the tensor VJP (the forward matmul)",
            carried,
            carried,
            carried,
            carried,
            "ONNX has no implementation to choose: the export records the choice in the model's "
            "metadata and emits MatMul, whose kernel the target picks. `reassociate` is granted "
            "by default (design principles 7.3), which permits that; Tiles' canonical matmul is "
            "permitted too.");
        add("tensor.kernel",
            "canonical",
            "tensor::fuse safe_fp=true; a #strict function",
            carried,
            carried,
            refused,
            carried,
            "an ONNX MatMul does not promise the canonical accumulation order, so the model "
            "would compute differently from the recorded choice; Tiles' matmul is the canonical "
            "chain, so it keeps the choice.");
        add("tensor.fusion",
            "linear",
            "tensor::fuse",
            carried,
            carried,
            carried,
            carried,
            "ONNX has no group marker: the operators are exported as they are and the metadata "
            "records the group, which the target may fuse; Tiles' region analysis forms the "
            "marked group into one region.");
        add("tensor.fusion",
            "linear_relu",
            "tensor::fuse",
            carried,
            carried,
            carried,
            carried,
            "ONNX has no group marker: the operators are exported as they are and the metadata "
            "records the group, which the target may fuse; Tiles' region analysis forms the "
            "marked group into one region.");

        // Numeric permissions: the node's permission column, not its attribute set.
        const std::string_view permissions[] = {
            "reassociate",
            "contract",
            "approx_math",
            "change_precision",
            "no_nans",
            "no_infs",
            "no_signed_zeros",
            "flush_subnormals",
            "nondeterministic",
            "refine_accuracy",
            "finite_math"};
        for (std::string_view permission : permissions) {
            add(permission,
                "",
                "a permission tag on a function or block",
                carried,
                carried,
                carried,
                carried,
                "a grant, not a promise: the model's metadata records it and the target's "
                "arithmetic must stay inside what it allows; Tiles reads the effective set "
                "(empty under #strict), which is what the interpreter's passes used.");
        }

        // Declaration facts: interned as keys, but never written on a node.
        add("resource",
            "*",
            "a `resource` declaration",
            notNode,
            notNode,
            notNode,
            notNode,
            "a declaration fact (a resource name): no node carries it.");
        add("reads",
            "*",
            "#reads (a pass declaration)",
            notNode,
            notNode,
            notNode,
            notNode,
            "a declaration fact (the tags a pass reads): no node carries it.");
        add("op",
            "*",
            "#op (a declaration)",
            notNode,
            notNode,
            notNode,
            notNode,
            "a declaration fact (an operator URI): no node carries it.");
        add("macro",
            "*",
            "#macro (a declaration)",
            notNode,
            notNode,
            notNode,
            notNode,
            "a declaration fact: no node carries it.");
        add("autodiff::vjp",
            "*",
            "autodiff's `tag vjp<rule>` declaration",
            notNode,
            notNode,
            notNode,
            notNode,
            "a declaration fact (the graph of a derivative rule): no node "
            "carries it.");
        // `#impl<kernel>` (design principles 10.4) names the body-less declaration that
        // implements a function, so the key is interned in the attribute namespace but is a
        // declaration fact: the builder resolves it while the module is declared, and no node
        // ever carries it.
        add("impl",
            "*",
            "#impl<kernel> (a function declaration)",
            notNode,
            notNode,
            notNode,
            notNode,
            "a declaration fact (the kernel that implements a function): no node "
            "carries it.");
        return t;
    }();
    return rows;
}

} // namespace

std::span<const FateRow> fates() { return fateTable(); }

const FateRow *fateOf(std::string_view key, std::string_view value) {
    const FateRow *fallback = nullptr;
    for (const FateRow &row : fateTable()) {
        if (row.key != key) {
            continue;
        }
        if (row.value == value) {
            return &row;
        }
        if (row.value == "*") {
            fallback = &row;
        }
    }
    return fallback;
}

std::string fateValueText(AttrValueId id) {
    const Value v = value(id);
    switch (v.kind) {
    case ValueKind::Unit:
        return {};
    case ValueKind::Symbol:
        return v.text;
    default:
        return valueText(id);
    }
}

std::string_view fateName(Fate fate) {
    switch (fate) {
    case Fate::Carried:
        return "carried";
    case Fate::Refused:
        return "refused";
    case Fate::NotOnANode:
        return "not-on-a-node";
    }
    return "unknown";
}

std::vector<std::string> registeredKeys() {
    Registry &r = registry();
    std::shared_lock lock(r.mutex);
    std::vector<std::string> names;
    names.reserve(r.names.size());
    for (size_t i = 1; i < r.names.size(); ++i) {
        names.push_back(r.names[i]);
    }
    return names;
}

bool dumpFates(std::ostream &os) {
    const std::span<const FateRow> table = fates();
    for (const FateRow &row : table) {
        os << std::format(
            "ATTRIBUTE {} written_by=\"{}\" interpreter={} dump={} onnx={} tiles={}\n",
            row.value.empty()  ? row.key
            : row.value == "*" ? std::format("{}=*", row.key)
                               : std::format("{}={}", row.key, row.value),
            row.writtenBy,
            fateName(row.interpreter),
            fateName(row.dump),
            fateName(row.onnx),
            fateName(row.tiles));
    }
    bool complete = !table.empty();
    // A row's four columns are an enum, so the incompleteness to catch is a key with no row and
    // two rows that disagree about one key and value.
    const auto classified = [&table](std::string_view key) {
        return std::ranges::any_of(table, [key](const FateRow &row) { return row.key == key; });
    };
    for (const std::string &key : registeredKeys()) {
        if (!classified(key)) {
            os << std::format(
                "UNCLASSIFIED ATTRIBUTE \"{}\": interned as an attribute key but absent from "
                "the fate table (node_attrs.cpp, fateTable); classify it for the interpreter, "
                "the dumps, the ONNX export and Tiles\n",
                key);
            complete = false;
        }
    }
    for (size_t i = 0; i < table.size(); ++i) {
        for (size_t j = i + 1; j < table.size(); ++j) {
            const FateRow &a = table[i];
            const FateRow &b = table[j];
            if (a.key != b.key || a.value != b.value) {
                continue;
            }
            if (a.interpreter != b.interpreter || a.dump != b.dump || a.onnx != b.onnx ||
                a.tiles != b.tiles) {
                os << std::format(
                    "UNCLASSIFIED ATTRIBUTE \"{}={}\": two rows disagree about its consumers\n",
                    a.key,
                    a.value);
                complete = false;
            }
        }
    }
    return complete;
}

} // namespace camel::core::attr
