// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operator table (see operator_table.h).
 */

#include "camel/core/operator_table.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_names.h"
#include "camel/core/type/typevar.h"

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

OperatorTraits OperatorTable::traitsOf(const sem::OperatorSemantics &facts) {
    // The one mapping from facts to trait bits. Nothing else computes a trait: purity and the
    // state-only answer are read off the access list, and "may fault" off the fault table
    // (docs/design/op-semantics/README.cn.md 4.2).
    return OperatorTraits{
        .pure      = facts.effect() == sem::Effect::Pure,
        .elementwise = facts.elementwise || (facts.binary && facts.binary->elementwise),
        .stateOnly = facts.effect() == sem::Effect::StateOnly,
        // A declared empty table: it never faults. No table: undeclared, may fault.
        .mayFault  = facts.mayFault(),
        // Calling the function values it receives (declared, or by calling elements) is what
        // makes the call depend on those functions.
        .callsGraphs = facts.callsGraphs || facts.elementCalls.has_value(),
        .readsGraphs = facts.readsGraphs,
        .macro       = facts.macro,
        .foldable    = facts.foldable,
        .withAfterNorm = facts.withAfterNorm,
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
    std::unique_lock lock(mutex_);
    const OperId id = internLocked(uri);
    Entry &entry    = entries_[id];
    const sem::OperatorSemantics *old = entry.facts.load(std::memory_order_relaxed);
    // The traits are not a second declaration: they are the shorthand the record's facts are
    // written with, and a record may exist before any semantics (the effects of a built-in
    // operator are declared before its numeric semantics).
    sem::OperatorSemantics facts =
        old ? *old : sem::OperatorSemantics{.key = sem::OperatorKey::of(uri)};
    // The effect the traits state, in the record's own terms
    // (design principles 3.3; docs/design/op-semantics/README.cn.md 4.2): pure is the empty access list,
    // so it is written as one and never as a word about the operator; state-only and effectful are
    // what a module says when it has not itemized its accesses. An access list already declared is
    // the truth about the operator and is not taken back by a later word.
    facts.declaredEffect = traits.stateOnly ? sem::Effect::StateOnly
                         : traits.pure      ? sem::Effect::Pure
                                            : sem::Effect::Effectful;
    if (traits.pure) {
        if (!facts.resources) {
            facts.resources = sem::ResourceSpec{};
        }
    } else if (facts.resources && facts.resources->none()) {
        // The word replaces an empty access list: the operator is state-only or effectful, and
        // what it touches is not itemized here (a later declareResources may). Keeping the empty
        // list would leave the record saying it touches nothing.
        facts.resources.reset();
    }
    facts.elementwise  = traits.elementwise;
    facts.callsGraphs  = traits.callsGraphs;
    facts.readsGraphs  = traits.readsGraphs;
    facts.macro        = traits.macro;
    facts.foldable     = traits.foldable;
    facts.withAfterNorm = traits.withAfterNorm;
    // "Never faults" is the empty fault table. Saying it may fault (the default) does not take
    // back a declared empty table; a declaration that does contradict it is refused when the
    // semantics are declared (checkSemantics).
    if (!traits.mayFault) {
        facts.faults = std::vector<sem::FaultSpec>{};
    }
    declarations_.push_back(
        std::make_unique<const sem::OperatorSemantics>(std::move(facts)));
    entry.facts.store(declarations_.back().get(), std::memory_order_release);
    if (entry.declared.load(std::memory_order_relaxed) != nullptr) {
        // Semantics already published: the traits belong to the same record, so publish the
        // updated one there too. No version bump: a trait is not a semantics fact.
        entry.declared.store(declarations_.back().get(), std::memory_order_release);
    }
}

void OperatorTable::declare(
    std::string_view protocol, std::initializer_list<std::pair<std::string_view, bool>> operators) {
    for (const auto &[name, pure] : operators) {
        set(std::string(protocol) + ":" + std::string(name), {.pure = pure});
    }
}

void OperatorTable::declareUnfoldable(
    std::string_view protocol, std::initializer_list<std::string_view> operators) {
    for (std::string_view name : operators) {
        set(std::string(protocol) + ":" + std::string(name), {.pure = true, .foldable = false});
    }
}

std::optional<OperatorTraits> OperatorTable::find(OperId id) const {
    const sem::OperatorSemantics *facts = record(id);
    // Effect::Undeclared is the record of an operator whose semantics were declared without
    // anybody saying what it does to the world: find says nobody declared its traits.
    if (facts == nullptr || facts->declaredEffect == sem::Effect::Undeclared) {
        return std::nullopt;
    }
    return traitsOf(*facts);
}

std::optional<OperatorTraits> OperatorTable::find(std::string_view uri) const {
    const auto id = lookup(uri);
    return id ? find(*id) : std::nullopt;
}

bool OperatorTable::isPure(OperId id) const {
    const sem::OperatorSemantics *facts = record(id);
    return facts != nullptr && facts->declaredEffect != sem::Effect::Undeclared &&
           facts->effect() == sem::Effect::Pure;
}

bool OperatorTable::isPure(std::string_view uri) const {
    const auto id = lookup(uri);
    return id && isPure(*id);
}

bool OperatorTable::withAfterNorm(std::string_view uri) const {
    const auto id = lookup(uri);
    const sem::OperatorSemantics *facts = id ? record(*id) : nullptr;
    return facts != nullptr && facts->withAfterNorm;
}

bool OperatorTable::mayBeRecomputed(OperId id) const {
    const sem::OperatorSemantics *facts = record(id);
    return facts != nullptr && facts->declaredEffect != sem::Effect::Undeclared &&
           facts->effect() == sem::Effect::Pure && facts->foldable;
}

bool OperatorTable::mayBeRecomputed(std::string_view uri) const {
    const auto id = lookup(uri);
    return id && mayBeRecomputed(*id);
}

bool OperatorTable::hasExternalEffect(OperId id) const {
    const sem::OperatorSemantics *facts = record(id);
    // The access list decides it, or the declaration that says an effect exists without an
    // itemized one; an operator nobody declared keeps the conservative answer (design
    // principles 3.3).
    return facts == nullptr || facts->external();
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
    if (const sem::OperatorSemantics *declared = entry.declared.load(std::memory_order_relaxed);
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
    if (const sem::OperatorSemantics *facts = record(id); facts && facts->resources) {
        return sem::writesOperand(*facts->resources, isWith, index);
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

std::vector<sem::ResourceAccess> OperatorTable::accessList(OperId id) const {
    if (const sem::OperatorSemantics *facts = record(id); facts != nullptr) {
        return facts->accessList();
    }
    // An operator nobody registered: nothing is declared about it, so the conservative reading
    // of design principles 3.3 -- it writes the global resource -- is the whole answer.
    return {sem::globalAccess(sem::AccessMode::Write)};
}

type::resolver_ptr_t OperatorTable::resolver(OperId id) const {
    std::shared_lock lock(mutex_);
    return entries_[id].resolver;
}

type::resolver_ptr_t OperatorTable::resolver(std::string_view uri) const {
    const auto id = lookup(uri);
    return id ? resolver(*id) : nullptr;
}

void OperatorTable::declareGroup(std::string_view group, const std::vector<std::string> &uris) {
    std::unique_lock lock(mutex_);
    for (auto &[name, declared] : groups_) {
        if (name != group) {
            continue;
        }
        for (const std::string &uri : uris) {
            if (std::ranges::find(declared, uri) == declared.end()) {
                declared.push_back(uri);
            }
        }
        return;
    }
    groups_.emplace_back(std::string(group), uris);
}

std::vector<std::string> OperatorTable::groups() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> names;
    names.reserve(groups_.size());
    for (const auto &[name, uris] : groups_) {
        names.push_back(name);
    }
    return names;
}

void OperatorTable::declareCallForm(std::string_view uri, const CallForm &form) {
    std::unique_lock lock(mutex_);
    for (auto &[declared, existing] : callForms_) {
        if (declared != uri) {
            continue;
        }
        // The name of the first declaration stands. One operator may be declared under two
        // spellings (`:op/itol` is both `int` and `i64`); a reader of the graph has one text to
        // write and either is the operator, and the first declaration is the one the language
        // documents it under.
        existing.minArgs   = std::min(existing.minArgs, form.minArgs);
        existing.withCount = std::min(existing.withCount, form.withCount);
        // A module that states the shape itself keeps saying so, whichever declaration came first.
        existing.declaredByModule = existing.declaredByModule || form.declaredByModule;
        existing.exported         = existing.exported || form.exported;
        return;
    }
    callForms_.emplace_back(std::string(uri), form);
}

void OperatorTable::markExported(std::string_view uri) {
    std::unique_lock lock(mutex_);
    for (auto &[declared, form] : callForms_) {
        // A declaration may cover a family of operators with one `*` (`:op/*tos`), and the export
        // names one operator of that family: the family's shape is the one a program writes.
        const size_t star  = declared.find('*');
        const bool family = star != std::string::npos && uri.size() + 1 >= declared.size() &&
                            uri.starts_with(std::string_view(declared.data(), star)) &&
                            uri.ends_with(std::string_view(
                                declared.data() + star + 1, declared.size() - star - 1));
        if (declared == uri || family) {
            form.exported = true;
            return;
        }
    }
}

std::optional<OperatorTable::CallForm> OperatorTable::callFormOf(std::string_view uri) const {
    std::shared_lock lock(mutex_);
    for (const auto &[declared, form] : callForms_) {
        if (declared == uri) {
            return form;
        }
    }
    // A declaration may cover a family of operators with one `*` (`:op/*tos`: one conversion per
    // source type). Its call shape is the family's, so a URI of the family takes it.
    for (const auto &[declared, form] : callForms_) {
        const size_t star = declared.find('*');
        if (star == std::string::npos || uri.size() + 1 < declared.size()) {
            continue;
        }
        const std::string_view prefix(declared.data(), star);
        const std::string_view suffix(declared.data() + star + 1, declared.size() - star - 1);
        if (uri.starts_with(prefix) && uri.ends_with(suffix)) {
            return form;
        }
    }
    return std::nullopt;
}

std::vector<std::string> OperatorTable::groupOperators(std::string_view group) const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> uris;
    for (const auto &[name, declared] : groups_) {
        if (name == group) {
            uris.insert(uris.end(), declared.begin(), declared.end());
        }
    }
    return uris;
}

std::string OperatorTable::groupOf(std::string_view uri) const {
    std::shared_lock lock(mutex_);
    // One operator may belong to more than one group: `tensor:matmul` is registered in `__mat__`
    // (the language's `@`) and in `matmul` (the name the module exports), because the same
    // `OpDef` names it both ways. Both answers are true, and the question "which group is this
    // operator's" has two answers that matter differently: a program writes the sign of the
    // language group, and the module's own name is what an import gives it.
    //
    // The first group that holds the URI is not the right answer to "how does a program write
    // this": it depends on registration order, and a module that registers the exported name
    // first hid the language's symbol from the printer, which then refused every `@` node. A
    // group with a symbol is the source spelling, so it is preferred; the first group otherwise.
    std::string first;
    for (const auto &[name, declared] : groups_) {
        for (const std::string &member : declared) {
            if (member == uri) {
                if (operatorSymbolOf(name) != nullptr) {
                    return name;
                }
                if (first.empty()) {
                    first = name;
                }
                break;
            }
        }
    }
    return first;
}

std::vector<type::Type *> OperatorTable::registeredTypes(std::string_view group) const {
    std::vector<std::string> uris;
    {
        std::shared_lock lock(mutex_);
        for (const auto &[name, declared] : groups_) {
            if (name == group) {
                uris.insert(uris.end(), declared.begin(), declared.end());
            }
        }
    }
    std::vector<type::Type *> types;
    const auto add = [&types](type::Type *type) {
        if (type == nullptr || type == type::Type::Never()) {
            // `never` is the result of an operator that does not return (`fail`): no value has it, so no
            // rule is instantiated at it.
            return;
        }
        // A signature that still writes a type variable belongs to a *generic* resolver (a group
        // declares `map::<T, U>` as well as `map` on one type): it names no concrete type, and a
        // rule cannot be instantiated at it. The types a group registered are the concrete ones.
        if (type::containsTypeVar(type)) {
            return;
        }
        for (type::Type *known : types) {
            if (known == type || known->equals(type)) {
                return;
            }
        }
        types.push_back(type);
    };
    // The lock is taken once per overload, after the group's list has been read: a group may be
    // declared again while modules load, and an overload's resolver may be replaced then.
    for (const std::string &uri : uris) {
        const type::resolver_ptr_t resolver          = this->resolver(uri);
        type::FunctionType *const signature =
            resolver != nullptr ? resolver->fixedSignature() : nullptr;
        if (signature == nullptr) {
            continue;
        }
        for (size_t i = 0; i < signature->withTypesCount(); ++i) {
            add(signature->withTypeAt(i));
        }
        for (size_t i = 0; i < signature->normTypesCount(); ++i) {
            add(signature->normTypeAt(i));
        }
        if (signature->hasExitType()) {
            add(signature->exitType());
        }
    }
    return types;
}

std::vector<type::Type *> OperatorTable::allRegisteredTypes() const {
    std::vector<type::Type *> types;
    for (const std::string &group : groups()) {
        for (type::Type *type : registeredTypes(group)) {
            bool known = false;
            for (type::Type *have : types) {
                if (have == type || have->equals(type)) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                types.push_back(type);
            }
        }
    }
    return types;
}

void OperatorTable::setInPlaceForm(std::string_view uri, std::string_view inPlaceUri) {
    std::unique_lock lock(mutex_);
    const OperId plain                  = internLocked(uri);
    const OperId form                   = internLocked(inPlaceUri);
    entries_[plain].inPlace             = entries_[form].uri;
    entries_[form].plain                = entries_[plain].uri;
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

std::optional<std::string_view> OperatorTable::plainForm(std::string_view uri) const {
    const auto id = lookup(uri);
    if (!id) {
        return std::nullopt;
    }
    std::shared_lock lock(mutex_);
    const SymId plain = entries_[*id].plain;
    if (plain == kEmptySym) {
        return std::nullopt;
    }
    return symbolText(plain);
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

void OperatorTable::declareConversion(std::string_view uri, ConversionTypes types) {
    std::unique_lock lock(mutex_);
    entries_[internLocked(uri)].conversion = types;
}

std::optional<ConversionTypes> OperatorTable::conversionTypesOf(std::string_view uri) const {
    const auto id = lookup(uri);
    if (!id) {
        return std::nullopt;
    }
    std::shared_lock lock(mutex_);
    return entries_[*id].conversion;
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
    const OperId id                  = internLocked(uri);
    Entry &entry                     = entries_[id];
    const sem::OperatorSemantics *old = entry.facts.load(std::memory_order_relaxed);
    // The traits declared so far are the old record's: the checks compare the new semantics
    // with the facts already declared, and the derivation is the same one find() uses.
    const std::optional<OperatorTraits> traits =
        old != nullptr && old->declaredEffect != sem::Effect::Undeclared
            ? std::optional<OperatorTraits>(traitsOf(*old))
            : std::nullopt;
    std::string problem = checkSemantics(uri, semantics, traits);
    if (problem.empty() && semantics.resources) {
        // Declared before or after its signature is registered: checked once both are there.
        const std::string signature =
            checkVarAgainstResources(uri, *semantics.resources, entry.resolver.get());
        if (!signature.empty()) {
            throw std::invalid_argument(signature);
        }
    }
    if (problem.empty() && old != nullptr &&
        std::tie(semantics.key.major, semantics.key.minor) <
            std::tie(old->key.major, old->key.minor)) {
        problem = std::format(
            "it would replace {} by the lower version {}",
            old->key.toString(),
            semantics.key.toString());
    }
    if (!problem.empty()) {
        throw std::invalid_argument(
            std::format(
                "The semantics declared for the operator '{}' are rejected: {}.",
                uri,
                problem));
    }
    // The record is one: the semantics declared here do not erase the facts declared before
    // (an effect, an elementwise law, an access list, a fault table). They are the same record's
    // fields.
    if (old != nullptr) {
        if (semantics.declaredEffect == sem::Effect::Undeclared) {
            semantics.declaredEffect = old->declaredEffect;
        }
        if (!semantics.resources) {
            semantics.resources = old->resources;
        }
        semantics.elementwise = semantics.elementwise || old->elementwise;
        semantics.callsGraphs = semantics.callsGraphs || old->callsGraphs;
        semantics.readsGraphs = semantics.readsGraphs || old->readsGraphs;
        semantics.macro       = semantics.macro || old->macro;
        semantics.withAfterNorm = semantics.withAfterNorm || old->withAfterNorm;
        if (!semantics.faults && old->faults) {
            semantics.faults = old->faults;
        }
    }
    declarations_.push_back(std::make_unique<const sem::OperatorSemantics>(std::move(semantics)));
    const sem::OperatorSemantics *published = declarations_.back().get();
    entry.facts.store(published, std::memory_order_release);
    entry.declared.store(published, std::memory_order_release);
    epoch_.fetch_add(1, std::memory_order_acq_rel);
}

void OperatorTable::declareResources(std::string_view uri, sem::ResourceSpec resources) {
    // A record of its own when the operator has no semantics; otherwise the same record with
    // the resources added, one minor version up (facts added, design 4.1). A record that only
    // carries traits keeps them: the resources join the same facts. Load time only: the read
    // of the old record and the declaration are not one step, as for every declaration.
    std::unique_lock lock(mutex_);
    const OperId id                  = internLocked(uri);
    Entry &entry                     = entries_[id];
    const sem::OperatorSemantics *old = entry.facts.load(std::memory_order_relaxed);
    sem::OperatorSemantics record;
    if (old != nullptr && entry.declared.load(std::memory_order_relaxed) != nullptr) {
        record = *old;
        if (record.resources != resources) {
            ++record.key.minor;
        }
    } else {
        record = old != nullptr ? *old : sem::OperatorSemantics{};
        record.key = sem::OperatorKey::of(uri);
    }
    record.resources = std::move(resources);
    lock.unlock();
    declareSemantics(uri, std::move(record));
}

void OperatorTable::declareFaults(std::string_view uri, std::vector<sem::FaultSpec> faults) {
    // The table joins the operator's facts the way a trait does: one record, one step further, and
    // no version of its own (a fault table is what the operator can raise, not a new release of
    // it). A record nobody declared semantics for stays that way: the effect facts of an operator
    // are declared before its semantics and are not semantics by themselves. Load time only.
    std::unique_lock lock(mutex_);
    const OperId id                  = internLocked(uri);
    Entry &entry                     = entries_[id];
    const sem::OperatorSemantics *old = entry.facts.load(std::memory_order_relaxed);
    if (old != nullptr && !faults.empty() && old->declaredEffect != sem::Effect::Undeclared &&
        !traitsOf(*old).mayFault) {
        throw std::invalid_argument(
            std::format(
                "The faults declared for the operator '{}' are rejected: it is declared never to "
                "fault.",
                uri));
    }
    sem::OperatorSemantics facts =
        old != nullptr ? *old : sem::OperatorSemantics{.key = sem::OperatorKey::of(uri)};
    facts.faults = std::move(faults);
    declarations_.push_back(std::make_unique<const sem::OperatorSemantics>(std::move(facts)));
    const sem::OperatorSemantics *published = declarations_.back().get();
    entry.facts.store(published, std::memory_order_release);
    if (entry.declared.load(std::memory_order_relaxed) != nullptr) {
        entry.declared.store(published, std::memory_order_release);
    }
    epoch_.fetch_add(1, std::memory_order_acq_rel);
}

void OperatorTable::declareDefinition(std::string_view uri, sem::Definition definition) {
    // As declareResources: a record of its own, or the existing one a minor version up.
    std::unique_lock lock(mutex_);
    const OperId id                  = internLocked(uri);
    Entry &entry                     = entries_[id];
    const sem::OperatorSemantics *old = entry.facts.load(std::memory_order_relaxed);
    sem::OperatorSemantics record;
    if (old != nullptr && entry.declared.load(std::memory_order_relaxed) != nullptr) {
        record = *old;
        if (record.definition != definition) {
            ++record.key.minor;
        }
    } else {
        record = old != nullptr ? *old : sem::OperatorSemantics{};
        record.key = sem::OperatorKey::of(uri);
    }
    record.definition = std::move(definition);
    lock.unlock();
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
