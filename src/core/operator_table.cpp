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
 * Created: Oct. 13, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The operator table (see operator_table.h).
 */

#include "camel/core/operator_table.h"
#include "camel/core/node_attrs.h"

#include "camel/utils/assert.h"

#include <format>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace camel::core {

OperatorTable &OperatorTable::instance() {
    static OperatorTable table;
    return table;
}

OperatorTable::OperatorTable() {
    // Index 0 is kNoOper: the empty URI, never declared.
    const OperId id = entries_.append([](Entry &entry) { entry.uri = kEmptySym; });
    ids_.emplace(kEmptySym, id);
    // The operators with compile-time ids come next, in the order of their ids.
    for (OperId known = 1; known < oper::kWellKnownCount; ++known) {
        const OperId interned = internLocked(kWellKnownOperatorUris[known]);
        ASSERT(interned == known, "The well-known operators must be interned first, in order.");
        (void)interned;
    }
}

uint8_t OperatorTable::encode(OperatorTraits traits) {
    return static_cast<uint8_t>(
        kDeclared | (traits.pure ? kPure : 0) | (traits.elementwise ? kElementwise : 0) |
        (traits.stateOnly ? kStateOnly : 0) | (traits.mayFault ? 0 : kNoFault) |
        (traits.callsGraphs ? kCallsGraphs : 0) | (traits.readsGraphs ? kReadsGraphs : 0));
}

OperatorTraits OperatorTable::decode(uint8_t bits) {
    return OperatorTraits{
        .pure           = (bits & kPure) != 0,
        .elementwise    = (bits & kElementwise) != 0,
        .stateOnly      = (bits & kStateOnly) != 0,
        .mayFault       = (bits & kNoFault) == 0,
        .callsGraphs    = (bits & kCallsGraphs) != 0,
        .readsGraphs    = (bits & kReadsGraphs) != 0,
    };
}

OperId OperatorTable::internLocked(std::string_view uri) {
    const SymId symbol = internSymbol(uri);
    if (auto it = ids_.find(symbol); it != ids_.end()) {
        return it->second;
    }
    uint8_t family = 0;
    if (uri.starts_with(':')) {
        family |= kFamilyBuiltin;
    }
    if (uri.starts_with(":op/assn")) {
        family |= kFamilyAssignment;
    }
    const OperId id = entries_.append([&](Entry &entry) {
        entry.uri    = symbol;
        entry.family = family;
    });
    ids_.emplace(symbol, id);
    return id;
}

OperId OperatorTable::intern(std::string_view uri) {
    if (const auto known = lookup(uri)) {
        return *known;
    }
    std::unique_lock lock(mutex_);
    return internLocked(uri);
}

std::optional<OperId> OperatorTable::lookup(std::string_view uri) const {
    // A URI that was never interned as a symbol cannot have an entry.
    const auto symbol = SymbolTable::instance().find(uri);
    if (!symbol) {
        return std::nullopt;
    }
    std::shared_lock lock(mutex_);
    if (auto it = ids_.find(*symbol); it != ids_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void OperatorTable::set(std::string_view uri, OperatorTraits traits) {
    const OperId id = intern(uri);
    entries_[id].traits.store(encode(traits), std::memory_order_relaxed);
}

void OperatorTable::declare(
    std::string_view protocol, std::initializer_list<std::pair<std::string_view, bool>> operators) {
    for (const auto &[name, pure] : operators) {
        set(std::string(protocol) + ":" + std::string(name), {.pure = pure});
    }
}

std::optional<OperatorTraits> OperatorTable::find(OperId id) const {
    const uint8_t bits = traitBits(id);
    if ((bits & kDeclared) == 0) {
        return std::nullopt;
    }
    return decode(bits);
}

std::optional<OperatorTraits> OperatorTable::find(std::string_view uri) const {
    const auto id = lookup(uri);
    return id ? find(*id) : std::nullopt;
}

bool OperatorTable::isPure(OperId id) const { return (traitBits(id) & kPure) != 0; }

bool OperatorTable::isPure(std::string_view uri) const {
    const auto id = lookup(uri);
    return id && isPure(*id);
}

bool OperatorTable::hasExternalEffect(OperId id) const {
    return (traitBits(id) & (kPure | kStateOnly)) == 0;
}

bool OperatorTable::hasExternalEffect(std::string_view uri) const {
    const auto id = lookup(uri);
    return !id || hasExternalEffect(*id);
}

namespace {

/// The resource declaration against the `var` parameters of the signature (design principles
/// 3.3); an empty string when they agree or the signature does not say.
std::string checkVarAgainstResources(
    std::string_view uri, const sem::ResourceSpec &resources,
    const type::FuncTypeResolver *resolver) {
    std::vector<bool> with, norm;
    if (resolver == nullptr || !resolver->varParameters(with, norm)) {
        return {};
    }
    if (auto problem = sem::checkVarParameters(resources, with, norm)) {
        return std::format(
            "The resources declared for the operator '{}' disagree with its signature {}: {}.",
            uri,
            resolver->signature(),
            *problem);
    }
    return {};
}

} // namespace

void OperatorTable::setResolver(std::string_view uri, type::resolver_ptr_t resolver) {
    std::unique_lock lock(mutex_);
    Entry &entry = entries_[internLocked(uri)];
    if (const sem::OperatorSemantics *declared = entry.semantics.load(std::memory_order_relaxed);
        declared != nullptr && declared->resources) {
        const std::string problem =
            checkVarAgainstResources(uri, *declared->resources, resolver.get());
        if (!problem.empty()) {
            throw std::invalid_argument(problem);
        }
    }
    entry.resolver = std::move(resolver);
}

bool OperatorTable::writesOperand(OperId id, bool isWith, size_t index) const {
    if (const sem::OperatorSemantics *declared = semantics(id); declared && declared->resources) {
        return sem::writesOperand(*declared->resources, isWith, index);
    }
    const auto traits = find(id);
    if (!traits) {
        return true;
    }
    // Not pure and undeclared: it may write every operand, except that the operands of one that
    // runs or reads the graphs of function values (a higher-order operator, a macro operator, an
    // exporter) are those functions, which are no resource; what a callee does is the callee's.
    return !traits->pure && !traits->callsGraphs && !traits->readsGraphs;
}

bool OperatorTable::touchesClass(OperId id, sem::ResourceClass cls, bool writesOnly) const {
    const sem::OperatorSemantics *declared = semantics(id);
    return declared && declared->resources &&
           sem::touchesClass(*declared->resources, cls, writesOnly);
}

type::resolver_ptr_t OperatorTable::resolver(OperId id) const {
    std::shared_lock lock(mutex_);
    return entries_[id].resolver;
}

type::resolver_ptr_t OperatorTable::resolver(std::string_view uri) const {
    const auto id = lookup(uri);
    return id ? resolver(*id) : nullptr;
}

void OperatorTable::setInPlaceForm(std::string_view uri, std::string_view inPlaceUri) {
    std::unique_lock lock(mutex_);
    const OperId form                   = internLocked(inPlaceUri);
    entries_[internLocked(uri)].inPlace = entries_[form].uri;
}

std::optional<std::string> OperatorTable::inPlaceForm(std::string_view uri) const {
    const auto id = lookup(uri);
    if (!id) {
        return std::nullopt;
    }
    std::shared_lock lock(mutex_);
    const SymId form = entries_[*id].inPlace;
    if (form == kEmptySym) {
        return std::nullopt;
    }
    return std::string(symbolText(form));
}

void OperatorTable::declareReusesOperand(std::string_view uri, bool reuses) {
    std::unique_lock lock(mutex_);
    entries_[internLocked(uri)].reusesOperand = reuses;
}

bool OperatorTable::reusesOperand(std::string_view uri) const {
    const auto id = lookup(uri);
    if (!id) {
        return false;
    }
    std::shared_lock lock(mutex_);
    return entries_[*id].reusesOperand;
}

void OperatorTable::declareGrant(std::string_view uri, AttrGrant grant) {
    (void)attr::key(grant.key); // validates the name
    auto shared = std::make_shared<const AttrGrant>(std::move(grant));
    std::unique_lock lock(mutex_);
    entries_[internLocked(uri)].grant = std::move(shared);
}

std::shared_ptr<const AttrGrant> OperatorTable::grantOf(std::string_view uri) const {
    const auto id = lookup(uri);
    if (!id) {
        return nullptr;
    }
    std::shared_lock lock(mutex_);
    return entries_[*id].grant;
}

void OperatorTable::declareCallSpec(std::string_view uri, CallSpec spec) {
    std::unique_lock lock(mutex_);
    entries_[internLocked(uri)].callSpec = std::make_unique<CallSpec>(std::move(spec));
}

const CallSpec *OperatorTable::callSpec(std::string_view uri) const {
    const auto id = lookup(uri);
    if (!id) {
        return nullptr;
    }
    std::shared_lock lock(mutex_);
    return entries_[*id].callSpec.get();
}

void OperatorTable::setTypeFolder(std::string_view uri, TypeFolder folder) {
    std::unique_lock lock(mutex_);
    entries_[internLocked(uri)].folder = std::make_unique<TypeFolder>(std::move(folder));
}

const TypeFolder *OperatorTable::typeFolder(OperId id) const {
    std::shared_lock lock(mutex_);
    return entries_[id].folder.get();
}

const TypeFolder *OperatorTable::typeFolder(std::string_view uri) const {
    const auto id = lookup(uri);
    return id ? typeFolder(*id) : nullptr;
}

// ---- Semantics -----------------------------------------------------------------------------

namespace {

/// Why `record`, declared for `uri` with the declared traits `traits` (nullopt when the traits
/// are not declared), cannot be published; empty when it can. The checks of design section 4.4
/// that apply to the parts declared so far (operator_table.h, declareSemantics).
std::string checkSemantics(
    std::string_view uri, const sem::OperatorSemantics &record,
    const std::optional<OperatorTraits> &traits) {
    using sem::NumericClass;
    // 1. The key names this operator.
    if (record.key.uri() != uri) {
        return std::format("its key {} names another operator", record.key.toString());
    }
    // 2. Not contradicting the traits.
    if (traits && !traits->mayFault && record.faults && !record.faults->empty()) {
        return "it declares faults, but its traits say it never faults";
    }
    // 3. The numeric part.
    const sem::NumericSemantics &n = record.numeric;
    const bool floatCompute =
        n.compute == type::TypeCode::Float32 || n.compute == type::TypeCode::Float64;
    switch (n.cls) {
    case NumericClass::Opaque:
        if (n.sensitiveTo != sem::kAllPermissions) {
            return "an opaque operator is sensitive to every permission";
        }
        break;
    case NumericClass::Exact:
        if (n.ulpBound != 0) {
            return "an exact operator has no ulp bound";
        }
        if ((n.sensitiveTo & ~sem::kFiniteMath) != 0) {
            return std::format(
                "an exact operator has no rounding for {} to change",
                sem::permissionsText(n.sensitiveTo & ~sem::kFiniteMath));
        }
        break;
    case NumericClass::CorrectlyRounded:
        if (n.ulpBound != 0) {
            return "a correctly rounded operator has no ulp bound";
        }
        if (!floatCompute) {
            return "a correctly rounded operator names the type its steps are rounded in";
        }
        break;
    case NumericClass::Bounded:
        if (!(n.ulpBound > 0)) {
            return "a bounded operator declares a positive ulp bound";
        }
        if ((n.sensitiveTo & sem::kApproxMath) == 0) {
            return "a bounded operator is sensitive to approx_math";
        }
        if (!floatCompute) {
            return "a bounded operator names the type it computes in";
        }
        break;
    }
    // 4. The reduction laws: each consistent in itself, and at most kNoLaw of them (an
    //    instance names one by a byte).
    if (record.laws.size() >= sem::kNoLaw) {
        return "it declares more reduction laws than an instance can name";
    }
    for (const sem::ReductionLaw &law : record.laws) {
        if (auto problem = sem::checkLaw(law)) {
            return *problem;
        }
    }
    // 5. The resources: consistent in themselves, and with the traits (design 4.4, check 2): a
    //    pure operator touches no resource; any other touches one, allocates one or has an
    //    external effect; an external effect only where the traits allow one (not pure, not
    //    state-only). The converse is a refinement (design 8.3): the traits of an assignment
    //    count it as external (no trait says "writes a variable"), its resources say it writes
    //    the program's own cell and nothing outside. The effect summaries stay computed from
    //    the traits.
    if (record.resources) {
        const sem::ResourceSpec &r = *record.resources;
        if (auto problem = sem::checkResources(r)) {
            return "resources: " + *problem;
        }
        if (traits) {
            if (traits->pure && (!r.accesses.empty() || r.allocates || r.externalEffect)) {
                return "resources: a pure operator touches no resource";
            }
            if (!traits->pure && r.accesses.empty() && !r.allocates && !r.externalEffect) {
                return "resources: an operator that is not pure touches, allocates or affects "
                       "something (declare it pure instead)";
            }
            if (r.externalEffect && (traits->pure || traits->stateOnly)) {
                return "resources: its traits give it no external effect";
            }
        }
    }
    // 5b. A higher-order operator calls the function values it receives: its traits say so.
    if (record.elementCalls && traits && !traits->callsGraphs) {
        return "element calls: the operator calls graphs (declare callsGraphs in its traits)";
    }
    // 6. The definition names a function of a library module and binds each parameter to an
    //    operand. That the function's signature agrees with the operator's, that its effects
    //    are within the operator's and that no definition reaches its own operator is checked
    //    when the library is compiled (camel/runtime/definitions.h): it is code in a file.
    if (record.definition) {
        const sem::Definition &d = *record.definition;
        if (d.module.empty() || d.function.empty()) {
            return "definition: it names a library module and a function";
        }
        if (d.params.size() >= 0xFF) {
            return "definition: too many parameters";
        }
        for (const sem::DefinitionParam &param : d.params) {
            if (param.operand == 0xFF) {
                return "definition: every parameter is bound to an operand";
            }
        }
    }
    return {};
}

} // namespace

void OperatorTable::declareSemantics(std::string_view uri, sem::OperatorSemantics semantics) {
    std::unique_lock lock(mutex_);
    const OperId id    = internLocked(uri);
    Entry &entry       = entries_[id];
    const uint8_t bits = entry.traits.load(std::memory_order_relaxed);
    const std::optional<OperatorTraits> traits =
        (bits & kDeclared) != 0 ? std::optional(decode(bits)) : std::nullopt;
    std::string problem = checkSemantics(uri, semantics, traits);
    if (problem.empty() && semantics.resources) {
        // Declared before or after its signature is registered: checked once both are there.
        const std::string signature =
            checkVarAgainstResources(uri, *semantics.resources, entry.resolver.get());
        if (!signature.empty()) {
            throw std::invalid_argument(signature);
        }
    }
    if (problem.empty()) {
        if (const sem::OperatorSemantics *old = entry.semantics.load(std::memory_order_relaxed);
            old != nullptr && std::tie(semantics.key.major, semantics.key.minor) <
                                  std::tie(old->key.major, old->key.minor)) {
            problem = std::format(
                "it would replace {} by the lower version {}",
                old->key.toString(),
                semantics.key.toString());
        }
    }
    if (!problem.empty()) {
        throw std::invalid_argument(
            std::format(
                "The semantics declared for the operator '{}' are rejected: {}.",
                uri,
                problem));
    }
    declarations_.push_back(std::make_unique<const sem::OperatorSemantics>(std::move(semantics)));
    entry.semantics.store(declarations_.back().get(), std::memory_order_release);
    epoch_.fetch_add(1, std::memory_order_acq_rel);
}

void OperatorTable::declareResources(std::string_view uri, sem::ResourceSpec resources) {
    // A record of its own when the operator has none; otherwise the same record with the
    // resources added, one minor version up (facts added, design 4.1). Load time only: the read
    // of the old record and the declaration are not one step, as for every declaration.
    sem::OperatorSemantics record;
    if (const auto id = lookup(uri); id && semantics(*id) != nullptr) {
        record = *semantics(*id);
        if (record.resources != resources) {
            ++record.key.minor;
        }
    } else {
        record.key = sem::OperatorKey::of(uri);
    }
    record.resources = std::move(resources);
    declareSemantics(uri, std::move(record));
}

void OperatorTable::declareDefinition(std::string_view uri, sem::Definition definition) {
    // As declareResources: a record of its own, or the existing one a minor version up.
    sem::OperatorSemantics record;
    if (const auto id = lookup(uri); id && semantics(*id) != nullptr) {
        record = *semantics(*id);
        if (record.definition != definition) {
            ++record.key.minor;
        }
    } else {
        record.key = sem::OperatorKey::of(uri);
    }
    record.definition = std::move(definition);
    declareSemantics(uri, std::move(record));
}

void OperatorTable::declareResources(
    std::string_view protocol,
    std::initializer_list<std::pair<std::string_view, sem::ResourceSpec>> operators) {
    for (const auto &[name, resources] : operators) {
        declareResources(std::string(protocol) + ":" + std::string(name), resources);
    }
}

std::optional<sem::OperatorKey> OperatorTable::key(OperId id) const {
    const sem::OperatorSemantics *declared = semantics(id);
    return declared ? std::optional(declared->key) : std::nullopt;
}

std::optional<OperId> OperatorTable::bind(const sem::OperatorKey &key) const {
    const auto id = lookup(key.uri());
    if (!id) {
        return std::nullopt;
    }
    const sem::OperatorSemantics *declared = semantics(*id);
    // A result recorded against key.minor may use facts added in that minor version.
    if (declared == nullptr || declared->key.major != key.major ||
        declared->key.minor < key.minor) {
        return std::nullopt;
    }
    return id;
}

} // namespace camel::core
