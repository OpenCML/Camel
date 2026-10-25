// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The types and the generic instantiation of the GIR builder: the generic templates a
 * module declares, the instance a call makes of one (resolveGenericCall, instantiateGeneric)
 * and the node a type is written as.
 */

#include "builder.h"
#include "builder_support.h"
#include "builder_helpers.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/data/primary.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/other.h"

#include <atomic>
#include <cctype>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

/// Whether an argument's own type is an unnamed struct. Such an argument is the entering form of
/// a named type (design principles 5.2: an unnamed struct value may be used directly as the
/// structurally identical named type), so where a type variable occurs at this position and at a
/// named one, the binding takes the named type: the exact form first, the entering one after
/// (design principles 5.1). `update(opt, qd, g, t)` with `params: P` and `state: AdamState<P>`,
/// where `qd` is a `tree_map` result whose type is the unnamed `{ w: Tensor, b: Tensor }` and `t`
/// is an `AdamState<Tensors>` for the `Tensors` that structure is named, binds `P = Tensors`;
/// binding the unnamed occurrence first would make `P` the written-out form and refuse `t` as
/// another named type, which is the one thing the position cannot be.
bool isUnnamedStructArgument(Type *type) {
    return type != nullptr && type->code() == TypeCode::Struct &&
           !static_cast<const StructType *>(type)->named();
}

/// The value of a `constas` constant as the key it stands for when it names a field or a
/// position: `w`, `0`, `true`. This is what `t.[n]` reads.
std::string constasKeyOf(const data_ptr_t &data) {
    if (auto text = std::dynamic_pointer_cast<StringData>(data)) {
        return text->data();
    }
    if (auto flag = std::dynamic_pointer_cast<PrimaryData<bool>>(data)) {
        return flag->data() ? "true" : "false";
    }
    if (auto integer = std::dynamic_pointer_cast<PrimaryData<int64_t>>(data)) {
        return std::to_string(integer->data());
    }
    if (auto integer = std::dynamic_pointer_cast<PrimaryData<int32_t>>(data)) {
        return std::to_string(integer->data());
    }
    return data != nullptr ? data->toString() : std::string("?");
}

/// The value as an instance name shows it: strings quoted, so that `f("w")` is `f<"w">`.
std::string constasTextOf(const data_ptr_t &data) {
    if (std::dynamic_pointer_cast<StringData>(data)) {
        return data->toString();
    }
    if (auto flag = std::dynamic_pointer_cast<PrimaryData<bool>>(data)) {
        return flag->data() ? "true" : "false";
    }
    if (auto integer = std::dynamic_pointer_cast<PrimaryData<int64_t>>(data)) {
        return std::to_string(integer->data());
    }
    if (auto integer = std::dynamic_pointer_cast<PrimaryData<int32_t>>(data)) {
        return std::to_string(integer->data());
    }
    return data != nullptr ? data->toString() : std::string("?");
}

namespace {

/// The integer a `constas` constant stands for when it can be an extent of a type.
std::optional<int64_t> constasExtentOf(const data_ptr_t &data) {
    if (auto integer = std::dynamic_pointer_cast<PrimaryData<int64_t>>(data)) {
        return integer->data();
    }
    if (auto integer = std::dynamic_pointer_cast<PrimaryData<int32_t>>(data)) {
        return static_cast<int64_t>(integer->data());
    }
    return std::nullopt;
}

/// The constant a static value of `type` holds, for a value whose node no longer carries the
/// `Data` it was made from (a primitive slot rebuilt here). A string or a composite has no such
/// form; those are read back from the constant the builder remembered for the node, if it made
/// the node itself.
data_ptr_t dataOfStaticValue(Type *type, slot_t value) {
    if (type == nullptr) {
        return nullptr;
    }
    using namespace camel::core::rtdata;
    switch (type->code()) {
    case TypeCode::Int32:
        return tt::as_shared<Data>(std::make_shared<PrimaryData<int32_t>>(fromSlot<int32_t>(value)));
    case TypeCode::Int64:
        return tt::as_shared<Data>(std::make_shared<PrimaryData<int64_t>>(fromSlot<int64_t>(value)));
    case TypeCode::Float32:
        return tt::as_shared<Data>(std::make_shared<PrimaryData<float>>(fromSlot<float>(value)));
    case TypeCode::Float64:
        return tt::as_shared<Data>(std::make_shared<PrimaryData<double>>(fromSlot<double>(value)));
    case TypeCode::Bool:
        return tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(fromSlot<bool>(value)));
    default:
        return nullptr;
    }
}

/// True when `part` is a proper part of `whole`: the type is built out of it -- an array's element
/// type, a member of a tuple or of a struct, the type argument of a named type, a function's
/// parameter or result -- and is not that same type. A recursive call that hands a generic
/// function a type built out of the type its caller already instantiated asks for one instance per
/// level (`T`, then `T[]`, then `T[][]`), and no level's key ever repeats.
bool typeHasProperPart(Type *whole, Type *part) {
    if (whole == nullptr || part == nullptr) {
        return false;
    }
    const auto holds = [&](Type *member) {
        return member != nullptr && (member->equals(part) || typeHasProperPart(member, part));
    };
    switch (whole->code()) {
    case TypeCode::Array:
        return holds(static_cast<ArrayType *>(whole)->elemType());
    case TypeCode::Tuple: {
        auto *tuple = static_cast<TupleType *>(whole);
        for (size_t i = 0; i < tuple->size(); ++i) {
            if (holds(tuple->typeAt(i))) {
                return true;
            }
        }
        return false;
    }
    case TypeCode::Stream:
        return holds(static_cast<StreamType *>(whole)->elemType());
    case TypeCode::Struct: {
        auto *record = static_cast<StructType *>(whole);
        if (record->named()) {
            // A named type is its declaration and its type arguments: the fields are the
            // declaration's, so only the arguments can be built out of another type.
            for (size_t i = 0; i < record->typeArgCount(); ++i) {
                if (holds(record->typeArgAt(i))) {
                    return true;
                }
            }
            return false;
        }
        for (size_t i = 0; i < record->size(); ++i) {
            if (holds(record->typeAt(i))) {
                return true;
            }
        }
        return false;
    }
    case TypeCode::Function: {
        auto *function = static_cast<FunctionType *>(whole);
        for (size_t i = 0; i < function->withTypesCount(); ++i) {
            if (holds(function->withTypeAt(i))) {
                return true;
            }
        }
        for (size_t i = 0; i < function->normTypesCount(); ++i) {
            if (holds(function->normTypeAt(i))) {
                return true;
            }
        }
        return function->hasExitType() && holds(function->exitType());
    }
    default:
        break;
    }
    if (const CollectionType *collection = asCollectionTypeOrPrototype(whole)) {
        for (size_t i = 0; i < collection->paramCount(); ++i) {
            if (holds(collection->params()[i])) {
                return true;
            }
        }
        return false;
    }
    if (const EnumType *enumType = asEnumTypeOrPrototype(whole)) {
        for (size_t i = 0; i < enumType->paramCount(); ++i) {
            if (holds(enumType->params()[i])) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

/// The constant a static data node (or a COPY of one) was made from, or null. A CAST that only
/// retypes a constant -- `null` into a `T?` -- is the constant of the type it retypes it to, and
/// carries it too.
data_ptr_t Builder::constantOfNode(node_handle_t node) const {
    if (node == nullptr) {
        return nullptr;
    }
    const auto found = constantNodes_.find(node);
    if (found != constantNodes_.end()) {
        return found->second;
    }
    node_handle_t base = throughCopy(node);
    if (base == node) {
        return nullptr;
    }
    const auto behind = constantNodes_.find(base);
    return behind != constantNodes_.end() ? behind->second : nullptr;
}

void Builder::registerGenericTemplates(const GCT::node_ptr_t &exec) {
    std::unordered_set<std::string> plainNames;
    for (size_t i = 0; i < exec->size(); ++i) {
        const auto &child = exec->at(i);
        if (child->type() != GCT::NodeKind::FUNC) {
            continue;
        }
        const std::string name = child->name();
        Type *type             = child->at(0)->dataType();
        if (!name.empty() && !isGenericHere(type, child)) {
            plainNames.insert(name);
        }
    }
    // The templates this block declares, in the order they are written: registered first, so
    // that two of them may call one another, then resolved by name (the second loop below).
    std::vector<generic_template_ptr_t> declared;
    for (size_t i = 0; i < exec->size(); ++i) {
        const auto &child = exec->at(i);
        if (child->type() != GCT::NodeKind::FUNC) {
            continue;
        }
        const std::string name = child->name();
        if (child->nativeDeclaration()) {
            // A body-less declaration is a rule of an operator, generic or not; a generic one is
            // instantiated by its resolver when it is called, not from a template (handover
            // step 1).
            continue;
        }
        Type *type = child->at(0)->dataType();
        if (name.empty() || !isGenericHere(type, child)) {
            continue;
        }
        // The generic functions of one name declared in one block are overloads, told apart by
        // their parameter types exactly as ordinary ones are (design principles 4.4): a call
        // takes the one its arguments fit. Two that their parameters cannot tell apart are one
        // signature, and the second is a redeclaration, which rejectRedeclaredFunction reports.
        // An operator takes its overloads from every layer instead (builder_operators.cpp).
        const bool isOperator = camel::core::isOverloadableOperatorGroup(name);
        if (!isOperator) {
            rejectRedeclaredFunction(name, tt::as_ptr<FunctionType>(boundType(type)), child);
        }
        if (!appliedDecoratorsOf(child).empty()) {
            diags_->of(SemanticDiag::GenericFunctionUnsupported)
                .atOrigin(child->origin())
                .commit(name, "decorated: a decorator takes a function value");
        }
        auto tmpl = std::make_shared<GenericTemplate>();
        // An operator's template is named as the program writes it (`operator +`), in its
        // diagnostics and in the names of its instances.
        tmpl->name           = isOperator ? displayNameOf(name) : name;
        tmpl->func           = child;
        tmpl->funcType       = tt::as_ptr<FunctionType>(boundType(type));
        if (const auto *head = GCT::Tree::of(child)->headTypeParamsOf(child)) {
            tmpl->headParams = *head;
        }
        tmpl->outerBindings  = typeBindings_;
        tmpl->ownerGraph     = currGraph_;
        tmpl->module         = module_;
        tmpl->nodeScope      = nodeScope_;
        tmpl->graphScope     = graphScope_;
        tmpl->decoratedScope = decoratedScope_;
        tmpl->templateScope  = templateScope_;
        tmpl->attrs          = currentAttrs_;
        // A generic function of a namespace (registered at the top level) is in that
        // namespace; one written in a body is where the body is.
        tmpl->namespacePath = currGraph_ == rootGraph_ ? Reference(name).paths() : namespacePath_;
        tmpl->usingFrames   = usingFrames_;
        if (const auto *usings = GCT::Tree::of(child)->usingsOf(child)) {
            tmpl->usingFrames.push_back(usings);
        }
        // The defaults of the declaration's parameters are evaluated where the
        // declaration is written, once, and every instance takes the same values: a default is
        // a compile-time constant (design principles 4.4).
        evaluateDefaults(child, tmpl->funcType, tmpl.get(), false);
        if (isOperator) {
            operatorTemplates_[name].push_back({templateScope_, tmpl});
        } else {
            // Several generic functions of one name stay one entity: the scope holds the
            // group, whose first template is the name's own until a second one is declared.
            auto group = templateScope_->get(name, false);
            if (group.has_value()) {
                genericGroupOf(group.value())->push_back(tmpl);
            } else {
                auto fresh = std::make_shared<GenericGroup>();
                fresh->push_back(tmpl);
                templateScope_->insert(name, generic_ref_t(fresh));
            }
        }
        declared.push_back(tmpl);
    }
    // Every declaration of this block is known now, so the names that do not depend on a type
    // parameter are resolved here, on the declaration's line, rather than waiting for an
    // instantiation that may never come (the rule on
    // `generic-rule-condition-unresolved-name`: a misspelled predicate is an unresolved name like
    // any other, and the module does not load). No type is read: what only the instantiation can
    // answer -- an operator overload, a field, the argument a call fits -- is not asked here.
    for (const generic_template_ptr_t &tmpl : declared) {
        resolveGenericTemplateNames(tmpl);
    }
}

std::vector<std::string> Builder::typeParameterNames(const generic_template_ptr_t &tmpl) const {
    std::vector<std::string> names = tmpl->headParams;
    std::vector<std::string> fromSignature;
    collectTypeVars(tmpl->funcType, fromSignature);
    for (const std::string &name : fromSignature) {
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    return names;
}

node_handle_t Builder::anonymousTemplateOf(const GCT::node_ptr_t &func) {
    Type *type             = func->at(0)->dataType();
    auto tmpl              = std::make_shared<GenericTemplate>();
    tmpl->func             = func;
    tmpl->funcType         = tt::as_ptr<FunctionType>(boundType(type));
    tmpl->outerBindings    = typeBindings_;
    tmpl->ownerGraph       = currGraph_;
    tmpl->module           = module_;
    tmpl->nodeScope        = nodeScope_;
    tmpl->graphScope       = graphScope_;
    tmpl->decoratedScope   = decoratedScope_;
    tmpl->templateScope    = templateScope_;
    tmpl->attrs            = currentAttrs_;
    tmpl->namespacePath    = namespacePath_;
    tmpl->usingFrames      = usingFrames_;
    node_handle_t drefNode = currGraph_->addDrefNode(
        std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{
            std::make_shared<graph_set_t>()});
    genericDrefs_[drefNode] = tmpl;
    anonymousTemplates_.push_back(tmpl);
    registerNodeOrigin(context_, drefNode, func, "gir.dref.generic");
    return drefNode;
}

void Builder::rejectGenericValue(
    const generic_template_ptr_t &tmpl, const GCT::node_ptr_t &origin, const std::string &why) {
    diags_->of(SemanticDiag::GenericFunctionAsValue)
        .atOrigin(origin->origin())
        .commit(tmpl->name, why, tmpl->name + ": " + tmpl->funcType->toString());
    throw BuildAbortException();
}

compile_graph_ptr_t Builder::instantiateGenericAs(
    const generic_template_ptr_t &tmpl, FunctionType *expected, const GCT::node_ptr_t &origin,
    std::string &why) {
    FunctionType *signature = tmpl->funcType;
    if (signature->hasRest()) {
        why = "a rest function is expanded at each call and has no function value";
        return nullptr;
    }
    if (signature->hasConstas()) {
        why = "a declaration with a 'constas' parameter needs the constant at its call, so it "
              "has no function value";
        return nullptr;
    }
    const size_t withCount  = signature->withTypesCount();
    const size_t normCount  = signature->normTypesCount();
    if (withCount != expected->withTypesCount() || normCount != expected->normTypesCount()) {
        why = std::format(
            "it takes {} parameter(s) and {} context parameter(s), and a function of type '{}' "
            "is expected",
            normCount,
            withCount,
            expected->toString());
        return nullptr;
    }
    const auto patternAt = [&](size_t i) {
        return i < withCount ? signature->withTypeAt(i) : signature->normTypeAt(i - withCount);
    };
    const auto expectedAt = [&](size_t i) {
        return i < withCount ? expected->withTypeAt(i) : expected->normTypeAt(i - withCount);
    };
    type_bindings_t bindings;
    for (size_t i = 0; i < withCount + normCount; ++i) {
        if (!unifyTypeVars(patternAt(i), expectedAt(i), bindings)) {
            why = std::format(
                "its parameter {}, '{}', does not have the form of '{}' in the function "
                "expected here, '{}'",
                i + 1,
                patternAt(i)->toString(),
                expectedAt(i)->toString(),
                expected->toString());
            return nullptr;
        }
    }
    if (signature->hasExitType() && expected->hasExitType()) {
        unifyTypeVars(signature->exitType(), expected->exitType(), bindings);
    }
    std::vector<std::string> names, open;
    collectTypeVars(signature, names);
    for (const std::string &name : names) {
        if (findTypeBinding(bindings, name) == nullptr) {
            open.push_back(name);
        }
    }
    if (!open.empty()) {
        why = std::format(
            "its type variable(s) {} cannot be determined from the function expected here, '{}'",
            strutil::join(open, ", ", [](const std::string &n) { return n; }),
            expected->toString());
        return nullptr;
    }
    for (size_t i = 0; i < withCount + normCount; ++i) {
        Type *param = substituteTypeVars(patternAt(i), bindings);
        if (containsTypeVar(expectedAt(i))) {
            why = std::format(
                "the function expected here, '{}', does not have all its types yet",
                expected->toString());
            return nullptr;
        }
        if (!param->equals(expectedAt(i))) {
            why = std::format(
                "with {} its parameter {} is '{}', and the function expected here is '{}'",
                typeBindingsToString(bindings),
                i + 1,
                param->toString(),
                expected->toString());
            return nullptr;
        }
    }
    if (const auto *when = GCT::Tree::of(tmpl->func)->whenOf(tmpl->func)) {
        // A function value has no `with` arguments of its own, so a condition that reads one
        // cannot be answered here; a false one makes this candidate inapplicable.
        const WhenResult verdict =
            evaluateWhenCondition(when->condition, signature, bindings, {}, tmpl);
        if (verdict.fails()) {
            why = std::format("its 'when' condition is false: when {}", when->text);
            return nullptr;
        }
        if (verdict.outcome == WhenOutcome::NeedsWithValue) {
            why = std::format(
                "its 'when' condition reads the 'with' parameter '{}', and a function value "
                "gives no 'with' arguments",
                verdict.subject);
            return nullptr;
        }
        if (!verdict.holds()) {
            reportWhenProblem(verdict, tmpl->name, when->condition);
        }
    }
    compile_graph_ptr_t graph = instantiateGeneric(tmpl, bindings, {}, {}, origin);
    if (graph == nullptr) {
        // The body does not compile for these types, and a scope is open: the attempt may be one
        // this position should try past (`SpeculativeInstantiation`), and what the body said is
        // kept there. Outside a scope `instantiateGeneric` has already reported and thrown.
        why = "its body does not compile for these types";
        return nullptr;
    }
    if (!graph->funcType()->hasExitType()) {
        diags_->of(SemanticDiag::CallingIncompleteFunction)
            .atOrigin(origin->origin())
            .commit(graph->name(), graph->funcType()->toString());
        throw BuildAbortException();
    }
    if (expected->hasExitType() && !containsTypeVar(expected->exitType()) &&
        !expected->exitType()->equals(graph->funcType()->exitType())) {
        why = std::format(
            "with {} it returns '{}', and the function expected here is '{}'",
            typeBindingsToString(bindings),
            graph->funcType()->exitType()->toString(),
            expected->toString());
        return nullptr;
    }
    return graph;
}

// ---- A generic rule where a rule is expected (rewrite design 2 (second version) 8.5) ----

void Builder::settleGenericRuleMaker(
    std::vector<Argument> &normInputs, type_vec_t &normTypes, const GCT::node_ptr_t &call) {
    // `__rule_generic(name, two_way, needs, consts, <the sides>)`, what the front end writes for a
    // rule whose head declares type parameters. The sides are the rule's own generic functions
    // (`name__lhs`, `name__rhs`, and `name__when` when it has a condition).
    constexpr size_t kFixed = 4;
    if (normInputs.size() <= kFixed) {
        return;
    }
    std::vector<generic_template_ptr_t> sides;
    for (size_t i = kFixed; i < normInputs.size(); ++i) {
        const auto found = genericDrefs_.find(normInputs[i].node);
        if (found == genericDrefs_.end()) {
            // A side that is not a generic function is not what this maker is for: the maker's own
            // resolution reports the call, as it would for any other argument it cannot take.
            return;
        }
        sides.push_back(found->second.one());
    }
    if (sides.size() < 2 || sides.size() > 3) {
        return;
    }
    // The rule's name as a reader writes it: the sides are the qualified names of its functions.
    std::string name = sides.front()->name;
    if (const size_t scope = name.rfind("::"); scope != std::string::npos) {
        name = name.substr(scope + 2);
    }
    if (const size_t side = name.rfind("__"); side != std::string::npos) {
        name.resize(side);
    }
    const uint64_t ruleId = registerGenericRule(
        *context_, static_cast<void *>(this), std::move(name), typeParameterNames(sides[0]),
        sides[0], sides[1], sides.size() == 3 ? sides[2] : nullptr);
    if (GenericRuleEntry *entry = genericRuleEntry(*context_, ruleId); entry != nullptr) {
        // Where the rule is written: a report about its condition points here from wherever the
        // instance that revealed the effect is made.
        entry->where = call->origin();
    }
    const data_ptr_t idValue =
        tt::as_shared<Data>(std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(ruleId)));
    const node_handle_t idNode = currGraph_->addStaticDataNode(idValue);
    rememberConstant(idNode, idValue);
    registerNodeOrigin(context_, idNode, call, "gir.rule.generic");
    // The rule value keeps the id of its templates, and the maker is called with it: what a generic
    // rule holds is not a function of the program being built, so nothing of the declaration enters
    // this graph but the id (design principles 5.5; the rule on
    // generic-rule-instantiation).
    normInputs.resize(kFixed + 1);
    normInputs[kFixed] = Argument{idNode, call, std::nullopt};
    normTypes.resize(kFixed + 1);
    normTypes[kFixed] = Type::Int64();
}

bool Builder::instantiateRuleTemplates(
    const GenericRuleEntry &entry, const std::vector<Type *> &types, compile_graph_ptr_t &lhs,
    compile_graph_ptr_t &rhs, compile_graph_ptr_t &when, std::string &why) {
    const auto lhsTemplate  = std::static_pointer_cast<GenericTemplate>(entry.lhs);
    const auto rhsTemplate  = std::static_pointer_cast<GenericTemplate>(entry.rhs);
    const auto whenTemplate = std::static_pointer_cast<GenericTemplate>(entry.when);
    ASSERT(lhsTemplate != nullptr && rhsTemplate != nullptr, "A rule has two sides.");
    const std::vector<std::string> names = typeParameterNames(lhsTemplate);
    if (names.size() != types.size()) {
        why = std::format(
            "it takes {} type parameter(s), and it was asked for {}",
            names.size(),
            types.size());
        return false;
    }
    type_bindings_t bindings;
    for (size_t i = 0; i < names.size(); ++i) {
        bindings.push_back({names[i], types[i]});
    }
    // The instance is built where the rule was declared: the templates are the ones the declaring
    // builder holds, and its own scopes and module answer for the body. That builder's build
    // finished long ago -- the rule is instantiated where the rule is applied, which is a pass
    // running later -- so the state a body build needs is entered again for the module the rule
    // belongs to (the same state a top-level declaration of it is built in). What a body says when
    // it does not compile for these types stays out of the program (the rule on
    // generic-rule-instantiation-set-and-predicate), and `why` is all that is kept of the attempt.
    std::string said;
    const GCT::node_ptr_t origin = lhsTemplate->func;
    const bool inModule          = this->inModule(lhsTemplate->module, [&] {
        SpeculativeInstantiation quiet(*this);
        lhs = instantiateGeneric(lhsTemplate, bindings, {}, {}, origin);
        rhs = lhs != nullptr ? instantiateGeneric(rhsTemplate, bindings, {}, {}, origin) : nullptr;
        when = lhs != nullptr && rhs != nullptr && whenTemplate != nullptr
                   ? instantiateGeneric(whenTemplate, bindings, {}, {}, origin)
                   : nullptr;
        // What the bodies said, read while the scope still holds it: it is the reason the attempt
        // is remembered by, and nobody hears it where it was said.
        said = quiet.reason();
        quiet.close();
    });
    if (!inModule) {
        lhs = rhs = when = nullptr;
        why = "the module that declared it is not in this builder any more";
        return false;
    }
    if (lhs == nullptr || rhs == nullptr) {
        lhs = rhs = when = nullptr;
        // Why the instance cannot be made is what the body said, and the maker's own phrase only
        // stands when it said nothing: `i32` has `+` and `-`, and a rule whose literal does not
        // fit the type is not one whose pattern has no operator there (
        // on `literal-inside-a-generic-body`, and on
        // `generic-rule-instantiation-set-and-predicate`, §3).
        why = said.empty() ? "the pattern has no operator for this type" : said;
        return false;
    }
    if (whenTemplate != nullptr && when == nullptr) {
        lhs = rhs = when = nullptr;
        why = said.empty() ? "the condition has no operator for this type" : said;
        return false;
    }
    // The instance is made and nobody asked whether making it was allowed: a condition that writes
    // an effect is a mistake of the rule wherever it stands, and the type arguments are what
    // revealed this one (the effect of a call whose candidate the types decide).
    reportEffectInRuleInstance(entry, lhsTemplate->module, when);
    return true;
}

RuleConditionAt Builder::ruleConditionVerdict(
    const GenericRuleEntry &entry, const std::vector<Type *> &types, std::string &why) {
    const auto whenTemplate = std::static_pointer_cast<GenericTemplate>(entry.when);
    if (whenTemplate == nullptr) {
        // No condition: the law is written for every type the pattern can be built for.
        return RuleConditionAt::Holds;
    }
    // The type parameters of the head, as the rule declares them: the names the condition's type
    // arguments are bound to. They are read from the entry, so the verdict is asked for exactly the
    // tuples the enumeration produces (`<T, U>` binds both).
    if (entry.typeParams.size() != types.size()) {
        why = std::format(
            "it takes {} type parameter(s), and it was asked about {}",
            entry.typeParams.size(),
            types.size());
        return RuleConditionAt::Unknown;
    }
    type_bindings_t bindings;
    bindings.reserve(types.size());
    for (size_t i = 0; i < types.size(); ++i) {
        bindings.push_back({entry.typeParams[i], types[i]});
    }
    if (whenTemplate->func == nullptr || whenTemplate->func->size() < 2) {
        why = "the condition template has no body to read";
        return RuleConditionAt::Unknown;
    }
    // The condition is asked where the rule was declared, as its instance would be: its predicate
    // calls resolve in that module's names, and instantiating one builds a body there. That is the
    // same state `instantiateRuleTemplates` enters, and for the same reason -- this runs while a
    // pass is applying the rule, long after the module was built. A predicate body that does not
    // compile for these types is held in a speculative scope, so the attempt is quiet and its
    // reason is what the caller keeps (the rule on
    // `generic-rule-instantiation-set-and-predicate`).
    WhenResult verdict;
    std::string said;
    const bool inModule = this->inModule(whenTemplate->module, [&] {
        SpeculativeInstantiation quiet(*this);
        try {
            verdict = evaluateWhenCondition(
                whenTemplate->func->at(1), whenTemplate->funcType, bindings, {}, whenTemplate);
        } catch (const BuildAbortException &) {
            // The compiler made a verdict this question cannot carry (a call the condition's
            // predicate makes that is not a compile-time one, an incomplete return type): the
            // condition is not one the check can ask, and `said` carries what was said.
            verdict         = WhenResult{};
            verdict.outcome = WhenOutcome::NotEvaluable;
        }
        said = quiet.reason();
        quiet.close();
    });
    if (!inModule) {
        why = "the module that declared it is not in this builder any more";
        return RuleConditionAt::Unknown;
    }
    if (verdict.fails()) {
        return RuleConditionAt::Fails;
    }
    if (verdict.holds()) {
        return RuleConditionAt::Holds;
    }
    // Everything else -- a condition whose rest reads the values it matches, and a condition the
    // compiler cannot answer at all -- keeps the tuple: a condition the check cannot ask must not
    // take an instance away (`why` carries what the compiler said), and a value-dependent one is an
    // instance whose firing is measured sample by sample, as it is for an ordinary conditional rule.
    why = !verdict.detail.empty() ? verdict.detail
                                  : (!said.empty() ? said : "the condition is not a compile-time one");
    return RuleConditionAt::ValueDependent;
}

node_handle_t
Builder::genericValueFor(node_handle_t node, Type *expected, const GCT::node_ptr_t &origin) {
    const auto generic = genericDrefs_.find(node);
    ASSERT(generic != genericDrefs_.end(), "Not the name of a generic function.");
    // A name with several generic functions stands for the one the type expected here fixes
    // (design principles 5.5), as a name with several ordinary ones does (overloadValueFor).
    const std::vector<generic_template_ptr_t> candidates = genericOverloadsOf(node);
    if (candidates.size() > 1) {
        // The checks below are the single-template ones, made for the candidates together: the
        // position is what says which function is wanted, so nothing else can be answered.
        for (const auto &candidate : candidates) {
            if (candidate->funcType->hasConstas()) {
                diags_->of(SemanticDiag::ConstasFunctionNeedsCall)
                    .atOrigin(origin != nullptr ? origin->origin() : 0)
                    .commit(candidate->name);
                throw BuildAbortException();
            }
            if (candidate->funcType->hasRest()) {
                diags_->of(SemanticDiag::RestFunctionNeedsExpansion)
                    .atOrigin(origin != nullptr ? origin->origin() : 0)
                    .commit(
                        candidate->name,
                        "a rest function has no runtime arity, so it cannot be passed as a "
                        "value; call it directly");
                throw BuildAbortException();
            }
        }
        if (expected == nullptr || expected->code() != TypeCode::Function) {
            rejectGenericValue(
                candidates.front(),
                origin,
                "nothing here says which of the overloads is wanted; write the type, e.g. "
                "'let f: (x: int) => int = " +
                    candidates.front()->name + "'");
        }
        std::string why;
        // A candidate whose body does not compile for the types this position fixes is not the
        // function it wants: the attempts are quiet about their bodies, and what they said is
        // reported only when not one of the candidates is usable (SpeculativeInstantiation).
        SpeculativeInstantiation quiet(*this);
        for (const auto &candidate : candidates) {
            std::string reason;
            if (compile_graph_ptr_t graph = instantiateGenericAs(
                    candidate, tt::as_ptr<FunctionType>(expected), origin, reason);
                graph != nullptr) {
                return createFuncDataNode(graph, true, false);
            }
            why = reason;
        }
        quiet.attach();
        rejectGenericValue(candidates.front(), origin, why);
    }
    const generic_template_ptr_t tmpl = candidates.empty() ? nullptr : candidates.front();
    if (tmpl->funcType->hasConstas()) {
        diags_->of(SemanticDiag::ConstasFunctionNeedsCall)
            .atOrigin(origin != nullptr ? origin->origin() : 0)
            .commit(tmpl->name);
        throw BuildAbortException();
    }
    if (tmpl->funcType->hasRest()) {
        diags_->of(SemanticDiag::RestFunctionNeedsExpansion)
            .atOrigin(origin != nullptr ? origin->origin() : 0)
            .commit(
                tmpl->name,
                "a rest function has no runtime arity, so it cannot be passed as a value; "
                "call it directly");
        throw BuildAbortException();
    }
    if (expected == nullptr) {
        std::vector<std::string> names = typeParameterNames(tmpl);
        rejectGenericValue(
            tmpl,
            origin,
            std::format(
                "nothing here says which function is wanted, so its type variable(s) {} cannot "
                "be determined; write the type, e.g. 'let f: (x: int) => int = {}'",
                strutil::join(names, ", ", [](const std::string &n) { return n; }),
                tmpl->name));
    }
    if (expected->code() != TypeCode::Function) {
        rejectGenericValue(
            tmpl,
            origin,
            "a value of type '" + expected->toString() + "' is expected here, not a function");
    }
    std::string why;
    compile_graph_ptr_t graph =
        instantiateGenericAs(tmpl, tt::as_ptr<FunctionType>(expected), origin, why);
    if (graph == nullptr) {
        rejectGenericValue(tmpl, origin, why);
    }
    return createFuncDataNode(graph, true, false);
}

void Builder::takeConstasValues(
    FunctionType *signature, std::vector<Argument> &normInputs, type_vec_t *normTypes,
    const GCT::node_ptr_t &call, ConstasValues &constas, ExtentValues &extents) {
    const size_t withCount     = signature->withTypesCount();
    const size_t declaredCount = signature->normTypesCount();
    // The arguments are written one per parameter, `constas` values included, so the parameter
    // of argument `i` is ordinary parameter `i`. A rest call has more arguments than that: the
    // ones after the fixed parameters are the rest parameter's and are kept.
    std::vector<Argument> runtimeInputs;
    type_vec_t runtimeTypes;
    runtimeInputs.reserve(normInputs.size());
    runtimeTypes.reserve(normInputs.size());
    const auto keep = [&](size_t i) {
        runtimeInputs.push_back(std::move(normInputs[i]));
        if (normTypes != nullptr && i < normTypes->size()) {
            runtimeTypes.push_back((*normTypes)[i]);
        }
    };
    for (size_t i = 0; i < declaredCount; ++i) {
        if (!signature->normIsConstasAt(i)) {
            if (i < normInputs.size()) {
                keep(i);
            }
            continue;
        }
        const std::string name(signature->argNameAt(withCount + i));
        if (i >= normInputs.size()) {
            diags_->of(SemanticDiag::ConstasArgumentMissing).atOrigin(call->origin()).commit(name);
            throw BuildAbortException();
        }
        Argument &argument          = normInputs[i];
        const GCT::node_ptr_t &expr = argument.expr;
        Type *valueType =
            argument.node != nullptr ? valueTypeOf(nodeTypeOf(argument.node)) : nullptr;
        data_ptr_t data =
            expr != nullptr && expr->type() == GCT::NodeKind::DATA ? expr->data() : nullptr;
        if (data == nullptr && argument.node != nullptr) {
            // A compile-time value handed on from another node: the `n` of a `static for`, a
            // `constas` forwarded to another `constas`, the `j` of `indices`.
            data = constantOfNode(argument.node);
        }
        if (data == nullptr && valueType != nullptr && argument.node != nullptr) {
            if (const StaticArg constant = staticArgOf(argument.node); constant.value) {
                data = dataOfStaticValue(valueType, *constant.value);
            }
        }
        if (data == nullptr && expr != nullptr && expr->type() == GCT::NodeKind::LINK) {
            // A call of a pure function with constant arguments is a constant, and this place
            // needs one: a call that does not reduce to one is refused by its own name
            // (design principles 5.1, 6.5), not as a value without a constant.
            const FoldedCall folded = foldConstantCall(expr);
            if (folded.data != nullptr) {
                data = folded.data;
            } else if (folded.refusal != FoldRefusal::NotAProgramCall) {
                reportFoldRefusal(folded, expr);
            }
        }
        if (data == nullptr || valueType == nullptr) {
            diags_->of(SemanticDiag::ConstasValueNotConstant)
                .atOrigin(expr != nullptr && expr->origin() != 0 ? expr->origin() : call->origin())
                .commit(name, signature->normTypeAt(i)->toString());
            throw BuildAbortException();
        }
        ConstasValue value;
        value.type      = signature->normTypeAt(i);
        value.valueType = valueType;
        value.data      = data;
        value.node      = argument.node;
        constas[name]   = std::move(value);
        if (auto extent = constasExtentOf(data); extent.has_value()) {
            extents[name] = *extent;
        }
    }
    for (size_t i = declaredCount; i < normInputs.size(); ++i) {
        keep(i);
    }
    normInputs = std::move(runtimeInputs);
    if (normTypes != nullptr) {
        *normTypes = std::move(runtimeTypes);
    }
}

std::vector<generic_template_ptr_t> Builder::genericOverloadsOf(node_handle_t node) const {
    const auto found = genericDrefs_.find(node);
    if (found == genericDrefs_.end()) {
        return {};
    }
    const generic_group_ptr_t &group = found->second.group();
    return group != nullptr ? *group : std::vector<generic_template_ptr_t>{};
}

generic_template_ptr_t Builder::chooseGenericOverload(
    const std::vector<generic_template_ptr_t> &candidates, const std::vector<Argument> &withInputs,
    const std::vector<Argument> &normInputs, const type_vec_t &withTypes, const type_vec_t &normTypes,
    const GCT::node_ptr_t &call) {
    // Design principles 4.4 and 5.1: a call takes the one overload whose parameters accept its
    // arguments, with the argument types first exactly as they are, and then with the literals
    // entering the parameter types. Two that fit are an ambiguity, not a matter of the order
    // they are declared in.
    const auto binds = [&](const generic_template_ptr_t &tmpl) {
        FunctionType *signature = tmpl->funcType;
        if (signature->hasRest() || signature->withTypesCount() != withTypes.size()) {
            return false;
        }
        const size_t params = signature->normTypesCount() - signature->constasCount();
        if (params != normTypes.size()) {
            return false;
        }
        // The arguments are matched to the parameters the way the call wrote them: a `constas`
        // parameter is written at its own position and is not a runtime argument. A variable
        // that occurs at an unnamed-struct position and at a named one takes the named type
        // (isUnnamedStructArgument).
        type_bindings_t bindings;
        for (int pass = 0; pass < 2; ++pass) {
            size_t written = 0;
            for (size_t i = 0; i < signature->normTypesCount(); ++i) {
                if (signature->normIsConstasAt(i)) {
                    continue;
                }
                Type *actual = valueTypeOf(normTypes[written]);
                ++written;
                if (isUnnamedStructArgument(actual) != (pass == 1)) {
                    continue;
                }
                if (!bindTypeVars(signature->normTypeAt(i), actual, bindings)) {
                    return false;
                }
            }
        }
        size_t written = 0;
        for (size_t i = 0; i < signature->normTypesCount(); ++i) {
            if (signature->normIsConstasAt(i)) {
                continue;
            }
            const size_t at  = written++;
            Type *pattern    = signature->normTypeAt(i);
            unifyTypeVars(pattern, valueTypeOf(normTypes[at]), bindings);
            Type *wanted = substituteTypeVars(pattern, bindings);
            Type *actual = valueTypeOf(normTypes[at]);
            if (entersImplicitly(wanted, actual)) {
                continue;
            }
            if (at < normInputs.size() &&
                compositeLiteralEntersIn(normInputs[at].node, wanted, normInputs[at].expr)) {
                continue;
            }
            return false;
        }
        return true;
    };
    // A name with one generic function keeps it, and its own report when the arguments do not
    // fit; only a name with several needs the choice made here.
    if (candidates.size() == 1) {
        return candidates.front();
    }
    std::vector<generic_template_ptr_t> fits;
    for (const auto &candidate : candidates) {
        if (binds(candidate)) {
            fits.push_back(candidate);
        }
    }
    if (fits.size() == 1) {
        return fits.front();
    }
    std::string argTypes;
    for (const auto &type : normTypes) {
        argTypes += (argTypes.empty() ? "" : ", ") + type->toString();
    }
    std::vector<std::string> listed;
    for (const auto &candidate : candidates) {
        listed.push_back(candidate->name + ": " + candidate->funcType->toString());
    }
    if (fits.size() > 1) {
        std::vector<std::string> also;
        for (const auto &candidate : fits) {
            also.push_back(candidate->name + ": " + candidate->funcType->toString());
        }
        rejectAmbiguousCall(candidates.front()->name, argTypes, also, call);
    }
    diags_->of(SemanticDiag::NoMatchingFunction)
        .atOrigin(call->origin())
        .commit(argTypes, strutil::join(listed, "; ", [](const std::string &s) { return s; }));
    throw BuildAbortException();
}

compile_graph_ptr_t Builder::resolveGenericCall(
    const generic_template_ptr_t &tmpl, std::vector<Argument> &withInputs,
    std::vector<Argument> &normInputs, type_vec_t &withTypes, type_vec_t &normTypes,
    const GCT::node_ptr_t &call, Type *expectedResult) {
    return instantiateForCall(
        tmpl, withInputs, normInputs, withTypes, normTypes, call, expectedResult);
}

compile_graph_ptr_t Builder::instantiateForCall(
    const generic_template_ptr_t &tmpl, std::vector<Argument> &withInputs,
    std::vector<Argument> &normInputs, type_vec_t &withTypes, type_vec_t &normTypes,
    const GCT::node_ptr_t &call, Type *expectedResult) {
    FunctionType *signature = tmpl->funcType;
    const size_t withCount  = signature->withTypesCount();
    const size_t declaredNormCount = signature->normTypesCount();
    // A `constas n: T` parameter is bound by an ordinary argument written at its position: a
    // compile-time constant, not a runtime one. It selects the instance and is then dropped
    // from the call's inputs (design principles 5.4). A declaration may have any number of
    // them, in any position.
    const size_t normCount = declaredNormCount - signature->constasCount();
    // The ordinary parameter of each runtime argument: the `constas` parameters are not ones.
    std::vector<size_t> runtimeIndices;
    runtimeIndices.reserve(normCount);
    for (size_t i = 0; i < declaredNormCount; ++i) {
        if (!signature->normIsConstasAt(i)) {
            runtimeIndices.push_back(i);
        }
    }
    const auto signatureIndexAt = [&](size_t i) {
        return i < withCount ? i : withCount + runtimeIndices[i - withCount];
    };
    // The arguments that name a generic function themselves: they have no type yet.
    std::vector<generic_template_ptr_t> waiting(withCount + normCount);
    const auto typesText = [&](const type_vec_t &types, size_t offset) {
        std::string text;
        for (size_t i = 0; i < types.size(); ++i) {
            const generic_template_ptr_t &generic =
                offset + i < waiting.size() ? waiting[offset + i] : nullptr;
            text += (i == 0 ? "" : ", ") +
                    (generic ? "<generic " + generic->name + ">" : types[i]->toString());
        }
        return text;
    };
    const auto argTypesStr = [&] {
        return withTypes.empty() ? std::format("({})", typesText(normTypes, withCount))
                                 : std::format(
                                       "<{}> ({})",
                                       typesText(withTypes, 0),
                                       typesText(normTypes, withCount));
    };
    const auto reject = [&](const std::string &why) {
        diags_->of(SemanticDiag::GenericArgumentsMismatch)
            .atOrigin(call->origin())
            .commit(tmpl->name, argTypesStr(), why, tmpl->name + ": " + signature->toString());
        throw BuildAbortException();
    };
    if (withCount != withTypes.size()) {
        // Every argument written in the parentheses (`take(xs, 2)`): say which parameters
        // are with-parameters and how the call is written.
        if (withTypes.empty() && normCount > 0 && withCount + declaredNormCount == normTypes.size() &&
            signature->argNamesCount() >= withCount + declaredNormCount) {
            const auto names = [&](size_t from, size_t count) {
                std::string out;
                for (size_t i = 0; i < count; ++i) {
                    out += (i == 0 ? "" : ", ") + std::string(signature->argNameAt(from + i));
                }
                return out;
            };
            const std::string withs = names(0, withCount), norms = [&] {
                std::string out;
                for (size_t i = 0; i < normCount; ++i) {
                    out += (i == 0 ? "" : ", ") +
                           std::string(signature->argNameAt(signatureIndexAt(withCount + i)));
                }
                return out;
            }();
            reject(
                std::format(
                    "it takes '{}' in angle brackets, not in the parentheses: write {}<{}>({}){}",
                    withs,
                    tmpl->name,
                    withs,
                    norms,
                    normCount == 1 ? std::format(" or {}->{}<{}>", norms, tmpl->name, withs)
                                   : std::string()));
        }
        reject(
            std::format(
                "it takes {} with-argument(s) and {} were given",
                withCount,
                withTypes.size()));
    }
    if (declaredNormCount != normTypes.size()) {
        // A call that stops before a `constas` parameter is missing its value, not only an
        // argument: say so, since the value is not a runtime one.
        if (normTypes.size() < declaredNormCount && signature->normIsConstasAt(normTypes.size())) {
            diags_->of(SemanticDiag::ConstasArgumentMissing)
                .atOrigin(call->origin())
                .commit(std::string(
                    signature->argNameAt(withCount + normTypes.size())));
            throw BuildAbortException();
        }
        reject(std::format(
            "it takes {} argument(s) and {} were given", declaredNormCount, normTypes.size()));
    }
    ConstasValues constas;
    ExtentValues extents;
    takeConstasValues(signature, normInputs, &normTypes, call, constas, extents);
    const auto inputAt      = [&](size_t i) -> Argument      &{
        return i < withCount ? withInputs[i] : normInputs[i - withCount];
    };
    const auto typeAt = [&](size_t i) -> Type *& {
        return i < withCount ? withTypes[i] : normTypes[i - withCount];
    };
    for (size_t i = 0; i < withCount + normCount; ++i) {
        if (auto generic = genericDrefs_.find(inputAt(i).node); generic != genericDrefs_.end()) {
            waiting[i] = generic->second.one();
        }
    }
    // A parameter by its position among all parameters (`with` ones first), with the `constas`
    // values of the call applied to the extents it writes.
    const auto patternAt = [&](size_t i) {
        Type *pattern = i < withCount ? signature->withTypeAt(i)
                                      : signature->normTypeAt(signatureIndexAt(i) - withCount);
        return substituteExtentValues(pattern, extents);
    };
    const auto actualAt = [&](size_t i) { return valueTypeOf(typeAt(i)); };
    const auto describe = [&](size_t i) {
        const size_t signatureIndex = signatureIndexAt(i);
        const std::string paramName =
            signatureIndex < signature->argNamesCount()
                ? std::string(signature->argNameAt(signatureIndex))
                : "?";
        return std::format(
            "{} {} {}, but its parameter '{}: {}'",
            i < withCount ? "with-argument" : "argument",
            (i < withCount ? i : i - withCount) + 1,
            waiting[i] ? "is the generic function '" + waiting[i]->name + "'"
                       : "has type '" + actualAt(i)->toString() + "'",
            paramName,
            patternAt(i)->toString());
    };
    // 1. Each variable takes the type found at its first binding position. An argument that is
    // not of the form of a `T?` / `T!` parameter enters it (design principles 3.8), so it
    // binds the variables of the payload: `or_zero(5, 0)` with `x: T?` gives
    // `T = int`. `null` binds nothing.
    //
    // The positions whose argument has a named type come first, and the ones whose argument is
    // an unnamed struct second (isUnnamedStructArgument), as in chooseGenericOverload: a `T?`
    // payload and a variable that a later named argument determines are bound the same way
    // whichever of the two comes first in the signature.
    type_bindings_t bindings;
    // Whether the argument at `i` is a literal written there — a number, a composite literal, or
    // an expression of literals only that folds to one. A literal's own type is the default its
    // kind has (design principles 5.1): the type of the position it is passed to is what it
    // takes, so the position has its say before the literal is bound. A name is a value of its
    // type, even when a `let` bound it to a constant, and keeps it: a value is not converted.
    const auto literalArgument = [&](size_t i) {
        const GCT::node_ptr_t &expr = inputAt(i).expr;
        if (expr == nullptr) {
            return false;
        }
        if (literalValueOfExpr(expr).has_value()) {
            return true;
        }
        if (expr->type() != GCT::NodeKind::DATA || expr->data() == nullptr ||
            expr->data()->type() == nullptr) {
            return false;
        }
        // A composite literal takes the type of its position, and so does a text or a boolean
        // literal: their type is the type the position asks for as much as a number's default is
        // (a text literal enters a text position, and nowhere else). The two data a literal
        // argument is not written as are the hole (`ref`), which stands for a value, and `null`
        // (`void`), which states nothing about a variable.
        const TypeCode code = expr->data()->type()->code();
        return code != TypeCode::Ref && code != TypeCode::Void;
    };
    const std::function<bool(Type *, Type *)> bindEntering = [&](Type *pattern, Type *actual) {
        type_bindings_t attempt = bindings;
        if (bindTypeVars(pattern, actual, attempt)) {
            bindings = std::move(attempt);
            return true;
        }
        TupleType *tagged = asTagged(pattern);
        if (tagged == nullptr) {
            return false;
        }
        if (tagged->isNullable() && actual->code() == TypeCode::Void) {
            return true;
        }
        return bindEntering(tagged->payloadType(), actual);
    };
    // 1. The arguments that have a type of their own bind the variables they mention first, as
    // they always have: what a value says about a variable stands, and the position a call stands
    // in never converts it (design principles 5.1). A literal argument is left to the position.
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < withCount + normCount; ++i) {
            if (waiting[i] || literalArgument(i) ||
                isUnnamedStructArgument(actualAt(i)) != (pass == 1)) {
                continue;
            }
            if (!bindEntering(patternAt(i), actualAt(i))) {
                reject(describe(i) + " needs a type of that form to bind its type variables");
            }
        }
    }
    // 2. A variable that its first binding position left open (the argument there is `null`, or a
    // generic function that is still waiting) takes the type found at any other value occurrence:
    // the constraints of all arguments are collected before any is held to its parameter.
    for (size_t i = 0; i < withCount + normCount; ++i) {
        if (!waiting[i] && !literalArgument(i)) {
            unifyTypeVars(patternAt(i), actualAt(i), bindings);
        }
    }
    // 3. The callee's result type aligned with the type of the position the call stands in solves
    // every variable it mentions (design principles 5.1, 5.5; the rule on
    // `expected-type-through-literals-and-calls`): not only a variable that appears in the result
    // alone, and not only one no value argument mentions. It stands before the literals, which
    // take the type it gives them: `keep([1.0, 2.0], 2)` in an `f32[]` position is `T = f32` and
    // the array is an `f32[]`, each element converted once from the value written, and a value
    // that already has a type is the type error it was (an `f64` value in an `f32` position).
    //
    // A `void` position is the absence of a requirement, not one: a value written where nothing is
    // wanted is discarded (`let x = <call>`), and it solves no variable — otherwise a
    // result-only variable would be solved to `void` and the call would be an instance of `void`.
    if (expectedResult != nullptr && expectedResult->code() != TypeCode::Void &&
        signature->hasExitType()) {
        unifyTypeVars(
            substituteExtentValues(signature->exitType(), extents), expectedResult, bindings);
    }
    // 4. The literals: what they are written as fills a variable nothing else fixed and does not
    // override the type the position gave it.
    for (size_t i = 0; i < withCount + normCount; ++i) {
        if (waiting[i] || !literalArgument(i)) {
            continue;
        }
        if (!bindEntering(patternAt(i), actualAt(i))) {
            reject(describe(i) + " needs a type of that form to bind its type variables");
        }
    }
    // 5. The waiting arguments, as long as one of them can be given its types: the parameter
    // it is passed to, with what is known so far, is the function it has to be; its instance
    // then says what is still open in that parameter (its return type).
    std::vector<std::string> reasons(waiting.size());
    {
        // A candidate here is tried for the parameter it is passed to, and one whose body does not
        // compile for the types so far is not one that can be given them: those bodies are quiet
        // (`SpeculativeInstantiation`) and the search goes on. Every other diagnostic this function
        // makes -- the argument form, a `constas` value that is not constant, a condition that
        // cannot be answered -- is the call's verdict and is reported the moment it is made.
        SpeculativeInstantiation attempts(*this);
        for (bool progress = true; progress;) {
            progress = false;
            for (size_t i = 0; i < waiting.size(); ++i) {
                if (!waiting[i]) {
                    continue;
                }
                Type *wanted = substituteTypeVars(patternAt(i), bindings);
                if (wanted->code() != TypeCode::Function) {
                    reasons[i] = wanted->code() == TypeCode::TypeVar
                                     ? "its parameter has no type of its own for it to take"
                                     : "its parameter is not a function";
                    continue;
                }
                compile_graph_ptr_t instance = instantiateGenericAs(
                    waiting[i],
                    tt::as_ptr<FunctionType>(wanted),
                    inputAt(i).expr,
                    reasons[i]);
                if (instance == nullptr) {
                    continue;
                }
                inputAt(i).node = createFuncDataNode(instance, true, false);
                typeAt(i)       = valueTypeOf(nodeTypeOf(inputAt(i).node));
                waiting[i]      = nullptr;
                unifyTypeVars(patternAt(i), actualAt(i), bindings);
                progress = true;
            }
        }
        // The function this call takes is decided: past here the builder reports where it always
        // did, so the instance the call takes is built with its own errors being the program's.
        attempts.close();
    }
    std::vector<std::string> names, open;
    collectTypeVars(signature, names);
    for (const std::string &name : names) {
        if (findTypeBinding(bindings, name) == nullptr) {
            open.push_back(name);
        }
    }
    const std::string openText = strutil::join(open, ", ", [](const std::string &n) { return n; });
    for (size_t i = 0; i < waiting.size(); ++i) {
        if (waiting[i]) {
            reject(
                std::format(
                    "{} cannot be given its types{}: {}",
                    describe(i),
                    open.empty() ? std::string()
                                 : " while the type variable(s) " + openText + " of '" +
                                       tmpl->name + "' are not determined by the other arguments",
                    reasons[i]));
        }
    }
    if (!open.empty()) {
        reject(
            "the type variable(s) " + openText +
            " cannot be determined from the arguments or expected result; give an expected type "
            "or explicit " +
            tmpl->name + "::<...> type arguments");
    }
    // ...and then every argument is held to its parameter exactly as for an ordinary function.
    for (size_t i = 0; i < withCount + normCount; ++i) {
        Type *wanted = substituteTypeVars(patternAt(i), bindings);
        if (containsTypeVar(wanted)) {
            reject(describe(i) + " leaves a type variable without a type");
        }
        if (entersImplicitly(wanted, actualAt(i))) {
            continue;
        }
        // A literal enters the parameter's type as every literal does (design principles
        // 5.1): with `T` fixed to `f32` by the position or by another argument, `[1.0]` in the
        // `T[]` parameter is an `f32[]` and `1.0` in the `T` parameter is an `f32`, converted
        // once from the value written. The conversion is the one an ordinary call makes
        // (enterParameters); a literal that does not fit is refused at its element or at the
        // value itself, and one that does not fit at all is described below. A value that
        // already has a type is not converted: `coerceTo` refuses it, as `enterParameters`
        // does, so it is the type error it was (`let v: float = 1.0; keep(v, 1)` with an `f32`
        // position).
        const std::optional<literal::Value> written = literalValueOfExpr(inputAt(i).expr);
        const bool literalEnters =
            (written.has_value() && wanted->isPrimitive() && literal::kindEnters(*written, wanted)) ||
            compositeLiteralEntersIn(inputAt(i).node, wanted, inputAt(i).expr);
        if (literalEnters) {
            if (node_handle_t entered = checkValue(inputAt(i).node, wanted, inputAt(i).expr)) {
                inputAt(i).node = entered;
                typeAt(i)       = wanted;
                continue;
            }
        }
        reject(
            std::format(
                "{} is '{}' here ({})",
                describe(i),
                wanted->toString(),
                typeBindingsToString(bindings)));
    }
    // A `constas` value is held to the type of its parameter like any other argument; its type
    // may still name a variable another argument bound (design principles 5.4).
    for (size_t i = 0; i < declaredNormCount; ++i) {
        if (!signature->normIsConstasAt(i)) {
            continue;
        }
        const std::string name(signature->argNameAt(withCount + i));
        ConstasValue &value = constas.at(name);
        Type *wanted =
            substituteTypeVars(substituteExtentValues(signature->normTypeAt(i), extents), bindings);
        if (containsTypeVar(wanted)) {
            reject(std::format(
                "the 'constas' parameter '{}' leaves a type variable without a type", name));
        }
        if (!wanted->assignableFrom(value.valueType) &&
            !entersImplicitly(wanted, value.valueType)) {
            reject(std::format(
                "the value given for the 'constas' parameter '{}' is '{}', but its parameter "
                "is '{}'",
                name,
                value.valueType->toString(),
                wanted->toString()));
        }
        if (!wanted->equals(value.valueType)) {
            if (data_ptr_t converted = value.data->convertTo(wanted); converted != nullptr) {
                value.data      = std::move(converted);
                value.valueType = wanted;
            }
        }
        value.type = wanted;
    }
    // The declaration's `when`, now that every type variable has a type: a false condition
    // makes this candidate inapplicable (design principles 5.13), so the call is reported with
    // the condition it could not meet rather than with a mismatch of types.
    if (const auto *when = GCT::Tree::of(tmpl->func)->whenOf(tmpl->func)) {
        std::unordered_map<std::string, data_ptr_t> withValues;
        for (size_t i = 0; i < withCount && i < withInputs.size(); ++i) {
            data_ptr_t value = constantOfNode(withInputs[i].node);
            if (value != nullptr) {
                withValues.emplace(std::string(signature->argNameAt(i)), value);
            }
        }
        const WhenResult verdict =
            evaluateWhenCondition(when->condition, signature, bindings, withValues, tmpl);
        if (verdict.fails()) {
            std::vector<Type *> all(withTypes);
            all.insert(all.end(), normTypes.begin(), normTypes.end());
            diags_->of(SemanticDiag::WhenConditionUnmet)
                .atOrigin(call->origin())
                .commit(
                    tmpl->name,
                    strutil::join(all, ", ", [](Type *t) { return t->toString(); }),
                    tmpl->name + " — when " + when->text);
            throw BuildAbortException();
        }
        if (!verdict.holds()) {
            reportWhenProblem(verdict, tmpl->name, when->condition);
        }
    }
    return instantiateGeneric(tmpl, bindings, extents, constas, call);
}

void Builder::settleGenericOperatorArguments(
    const oper_group_ptr_t &ops, std::vector<Argument> &withInputs,
    std::vector<Argument> &normInputs, type_vec_t &withTypes, type_vec_t &normTypes,
    const GCT::node_ptr_t &call) {
    // A name that still has no function value: a generic function, or a name with several
    // functions (overloads of the program, an operator with several signatures).
    const auto isOverloaded = [&](const Argument &arg) {
        if (arg.node == nullptr || !nodeIsKind(arg.node, runtime::GCNodeKind::Dref) ||
            genericDrefs_.contains(arg.node) || boundWithArgs_.contains(arg.node)) {
            return false;
        }
        const auto &target = nodeGraphOf(arg.node)->drefTarget(arg.node);
        return std::holds_alternative<graph_vec_ptr_t>(target) ||
               std::holds_alternative<oper_group_ptr_t>(target);
    };
    const auto isWaiting = [&](const Argument &arg) {
        return genericDrefs_.contains(arg.node) || isOverloaded(arg);
    };
    // The types to try for a parameter of the function: what the other arguments are made
    // of (the element type of an array, of a set, the key and value types of a map and their
    // pair), then those arguments' own types.
    type_vec_t candidates;
    const auto add = [&](Type *type) {
        if (type == nullptr || type->code() == TypeCode::Void || containsTypeVar(type)) {
            return;
        }
        for (Type *known : candidates) {
            if (known->equals(type)) {
                return;
            }
        }
        candidates.push_back(type);
    };
    const auto addParts = [&](Type *type) {
        if (type == nullptr) {
            return;
        }
        if (type->code() == TypeCode::Array) {
            add(tt::as_ptr<ArrayType>(type)->elemType());
        } else if (type->isOtherType()) {
            const auto params = static_cast<OtherType *>(type)->paramsSpan();
            for (Type *param : params) {
                add(param);
            }
            if (params.size() == 2) {
                add(TupleType::create(std::vector<Type *>{params[0], params[1]}));
            }
        }
    };
    for (const auto *list : {&withInputs, &normInputs}) {
        const type_vec_t &types = list == &withInputs ? withTypes : normTypes;
        for (size_t i = 0; i < list->size(); ++i) {
            if (!isWaiting((*list)[i])) {
                addParts(valueTypeOf(types[i]));
            }
        }
    }
    for (const auto *list : {&withInputs, &normInputs}) {
        const type_vec_t &types = list == &withInputs ? withTypes : normTypes;
        for (size_t i = 0; i < list->size(); ++i) {
            if (!isWaiting((*list)[i])) {
                add(valueTypeOf(types[i]));
            }
        }
    }
    const auto accepted = [&] {
        std::vector<StaticArg> statics;
        statics.reserve(normInputs.size());
        for (const Argument &input : normInputs) {
            statics.push_back(isWaiting(input) ? StaticArg{} : staticArgOf(input.node));
        }
        return ops->resolve(withTypes, normTypes, statics, Modifier::None).has_value();
    };
    // A name with several functions: the one the operator accepts, when it is exactly one
    // (`xs->map<abs>`, `xs->reduce<max, 0>`). The operator is asked about each candidate's
    // type; none or several accepted leave the name without a value, and the diagnostic of
    // that lists the candidates.
    const auto settleOverloaded = [&](Argument &arg, Type *&type) {
        Type *saved        = type;
        const auto &target = nodeGraphOf(arg.node)->drefTarget(arg.node);
        std::vector<compile_graph_ptr_t> graphs;
        std::vector<oper_idx_ptr_t> operators;
        const auto accepts = [&](FunctionType *candidate) {
            if (candidate == nullptr || !candidate->hasExitType()) {
                return false;
            }
            type          = candidate;
            const bool ok = accepted();
            type          = saved;
            return ok;
        };
        if (std::holds_alternative<graph_vec_ptr_t>(target)) {
            if (auto all = asCompileGraphVec(target)) {
                for (const auto &g : *all) {
                    if (!decoratedOf_.contains(g.get()) && accepts(g->funcType())) {
                        graphs.push_back(g);
                    }
                }
            }
        } else {
            const auto &group = std::get<oper_group_ptr_t>(target);
            const auto add    = [&](const std::string &uri, FunctionType *candidate) {
                for (const auto &known : operators) {
                    if (known->uri() == uri && known->funcType()->equals(candidate)) {
                        return;
                    }
                }
                if (accepts(candidate)) {
                    operators.push_back(
                        std::make_shared<OperatorIndex>(group->name(), candidate, uri));
                }
            };
            for (const auto &[uri, resolver] : group->resolvers()) {
                if (FunctionType *fixed = resolver->fixedSignature()) {
                    add(uri, fixed);
                    continue;
                }
                // A signature computed from the argument types: asked for the candidate
                // types, one and two parameters.
                for (Type *first : candidates) {
                    if (auto one = resolver->resolve({}, {first}, Modifier::None)) {
                        add(uri, *one);
                    }
                    for (Type *second : candidates) {
                        if (auto two = resolver->resolve({}, {first, second}, Modifier::None)) {
                            add(uri, *two);
                        }
                    }
                }
            }
        }
        if (graphs.size() + operators.size() != 1) {
            return; // stays a name: rejectNameWithoutValue lists what it stands for
        }
        compile_graph_ptr_t chosen =
            !graphs.empty() ? graphs.front() : operatorValueGraphOf(operators.front(), arg.expr);
        if (chosen != nullptr) {
            arg.node = createFuncDataNode(chosen, true, false);
            type     = valueTypeOf(nodeTypeOf(arg.node));
        }
    };
    const auto settleOne = [&](Argument &arg, Type *&type) {
        if (isOverloaded(arg)) {
            settleOverloaded(arg, type);
            return;
        }
        const generic_template_ptr_t tmpl = genericDrefs_.find(arg.node)->second.one();
        FunctionType *signature   = tmpl->funcType;
        const size_t params       = signature->normTypesCount();
        std::vector<std::string> names;
        collectTypeVars(signature, names);
        std::string tried;
        // The instances the types of the other arguments suggest are candidates, not the answer: one
        // whose body does not compile for the types it was tried with is simply not one this
        // operator accepts, so the bodies tried are quiet and what they said is reported only when
        // the name cannot stand for any of them (SpeculativeInstantiation).
        SpeculativeInstantiation quiet(*this);
        // An instance that was built and that the operator then refused (its result type
        // is not what the operator needs): the first one stands for the name, so that the
        // operator's own diagnostic says what does not fit.
        node_handle_t refused{};
        Type *refusedType = nullptr;
        if (signature->withTypesCount() == 0 && params > 0 && !candidates.empty()) {
            // Every choice of candidate types for the parameters, the first candidates first.
            const size_t limit = 256;
            std::vector<size_t> pick(params, 0);
            for (size_t round = 0; round < limit; ++round) {
                type_bindings_t bindings;
                bool fits = true;
                for (size_t p = 0; fits && p < params; ++p) {
                    fits = unifyTypeVars(signature->normTypeAt(p), candidates[pick[p]], bindings);
                }
                for (size_t n = 0; fits && n < names.size(); ++n) {
                    fits = findTypeBinding(bindings, names[n]) != nullptr;
                }
                for (size_t p = 0; fits && p < params; ++p) {
                    fits = substituteTypeVars(signature->normTypeAt(p), bindings)
                               ->equals(candidates[pick[p]]);
                }
                if (fits) {
                    FunctionType *asType = instantiateFunctionType(signature, bindings);
                    Type *saved          = type;
                    // The operator is asked before the instance is built, so that the body
                    // of an instance it would refuse is never built (its errors would be
                    // reported for types the program does not mean). With a declared return
                    // type the question is exact; without one it is asked for the return
                    // types an element function usually has.
                    bool wanted = false;
                    if (asType->hasExitType()) {
                        type   = asType;
                        wanted = accepted();
                    } else {
                        type_vec_t results = candidates;
                        for (Type *usual :
                             {Type::Bool(), Type::Int64(), Type::Float64(), Type::String()}) {
                            results.push_back(usual);
                        }
                        for (size_t r = 0; !wanted && r < results.size(); ++r) {
                            OpenFunctionType *probe = instantiateFunctionType(signature, bindings);
                            probe->setExitType(results[r]);
                            type   = probe;
                            wanted = accepted();
                        }
                    }
                    type = saved;
                    if (wanted) {
                        std::string why;
                        compile_graph_ptr_t instance =
                            instantiateGenericAs(tmpl, asType, arg.expr, why);
                        if (instance != nullptr) {
                            node_handle_t value = createFuncDataNode(instance, true, false);
                            type                = valueTypeOf(nodeTypeOf(value));
                            if (accepted()) {
                                arg.node = value;
                                return;
                            }
                            if (refused == nullptr) {
                                refused     = value;
                                refusedType = type;
                            }
                            type = saved;
                        }
                    }
                    if (!tried.empty()) {
                        tried += "; ";
                    }
                    tried += typeBindingsToString(bindings);
                }
                // The next choice.
                size_t p = 0;
                while (p < params && ++pick[p] == candidates.size()) {
                    pick[p++] = 0;
                }
                if (p == params) {
                    break;
                }
            }
        }
        if (refused != nullptr) {
            arg.node = refused;
            type     = refusedType;
            return;
        }
        if (tmpl->name.empty()) {
            // The element function of a list comprehension: nothing to take `x` from.
            Type *source = !normTypes.empty() ? valueTypeOf(normTypes.front()) : nullptr;
            quiet.attach();
            diags_->of(SemanticDiag::ComprehensionSourceRefused)
                .atOrigin(call->origin())
                .commit(source != nullptr ? source->toString() : std::string("void"));
            throw BuildAbortException();
        }
        quiet.attach();
        rejectGenericValue(
            tmpl,
            arg.expr,
            std::format(
                "the operator '{}' does not say which function it expects, and no instance "
                "suggested by its other arguments is one it accepts{}; its type variable(s) {} "
                "stay undetermined. Write the call in a function whose parameter types are "
                "written out, e.g. '(x: int) => {}(x)'",
                ops->name(),
                tried.empty() ? std::string() : " (tried: " + tried + ")",
                strutil::join(names, ", ", [](const std::string &n) { return n; }),
                tmpl->name));
    };
    for (size_t i = 0; i < withInputs.size(); ++i) {
        if (isWaiting(withInputs[i])) {
            settleOne(withInputs[i], withTypes[i]);
        }
    }
    for (size_t i = 0; i < normInputs.size(); ++i) {
        if (isWaiting(normInputs[i])) {
            settleOne(normInputs[i], normTypes[i]);
        }
    }
}

void Builder::rejectInvalidCollectionKey(Type *type, const GCT::node_ptr_t &at) {
    OtherType *invalid = findInvalidCollectionKey(type);
    if (invalid == nullptr) {
        return;
    }
    Type *keyType                          = invalid->params()[0];
    camel::source::origin_id_t fieldOrigin = camel::source::kInvalidOriginId;
    std::string fieldLabel;
    std::string problem = collectionKeyProblem(keyType, &fieldOrigin, &fieldLabel);
    auto diag = diags_->of(SemanticDiag::CollectionKeyNotAllowed).atOrigin(at->origin());
    if (fieldOrigin != camel::source::kInvalidOriginId) {
        // A field of an enum is what keeps the enum from being a key: point at its declaration.
        diag.relatedOrigin(fieldOrigin, fieldLabel + " is declared here");
    }
    diag.commit(keyType->toString(), invalid->toString(), problem);
    throw BuildAbortException();
}

compile_graph_ptr_t Builder::instantiateGeneric(
    const generic_template_ptr_t &tmpl, const type_bindings_t &bindings, const ExtentValues &extents,
    const ConstasValues &constas, const GCT::node_ptr_t &call) {
    ExpectedValueScope instanceScope(expectedValueType_, nullptr);
    // All routes (inferred calls, function values and::<T>) enforce the same type rules.
    Type *concrete = substituteExtentValues(substituteTypeVars(tmpl->funcType, bindings), extents);
    if (TupleType *repeated = findRepeatedMark(concrete)) {
        diags_->of(SemanticDiag::TaggedTypeNesting)
            .atOrigin(call->origin())
            .commit(
                repeated->toString(),
                asTagged(repeated->payloadType())->payloadType()->toString());
        throw BuildAbortException();
    }
    rejectInvalidCollectionKey(concrete, call);
    const auto sameBindings = [&](const type_bindings_t &other) {
        if (other.size() != bindings.size()) {
            return false;
        }
        for (const auto &[name, type] : bindings) {
            Type *bound = findTypeBinding(other, name);
            if (bound == nullptr || !bound->equals(type)) {
                return false;
            }
        }
        return true;
    };
    // The `constas` values are part of the instance key too: two calls that differ only in a
    // string (or bool, or any other constant) are two instances (design principles 5.4).
    const auto sameConstas = [&](const ConstasValues &other) {
        if (other.size() != constas.size()) {
            return false;
        }
        for (const auto &[name, value] : constas) {
            const auto found = other.find(name);
            if (found == other.end()) {
                return false;
            }
            if (value.data == nullptr || found->second.data == nullptr) {
                if (value.data != found->second.data) {
                    return false;
                }
                continue;
            }
            if (!value.data->equals(found->second.data)) {
                return false;
            }
        }
        return true;
    };
    // One instance per set of types in the whole program, whichever module calls it: the
    // instance is a graph under the graph the function was declared in (the root graph for a
    // function of a module's top level), and its body resolves its names, and captures the
    // values of the top level, of the module that declares it. An instance whose body is
    // still being built is found here too: that is a recursive call.
    const bool foreign = tmpl->module != module_;
    for (const GenericInstance &instance : tmpl->instances) {
        if (sameBindings(instance.bindings) && instance.extents == extents &&
            sameConstas(instance.constas)) {
            if (instance.failed) {
                // Built once and it did not compile for these types. Outside a scope that is the
                // program's error as much as it was the first time; inside one the answer is "no
                // instance", and nothing is reported that was not reported before.
                if (speculative_ != nullptr) {
                    return nullptr;
                }
                throw BuildAbortException();
            }
            // A recursive call of the instance being built can share its graph, but only once
            // its return type is known. A function with no declared return type has none yet:
            // returning the open graph would hand the caller half a type.
            if (bodyStates_[instance.graph.get()] == BodyState::Open &&
                !instance.graph->funcType()->hasExitType()) {
                diags_->of(SemanticDiag::GenericInstantiationCycle)
                    .atOrigin(call->origin())
                    .commit(tmpl->name, typeBindingsToString(bindings));
                throw BuildAbortException();
            }
            return instance.graph;
        }
    }
    const std::string typeText =
        strutil::join(bindings, ", ", [](const std::pair<std::string, Type *> &b) {
            return b.second->toString();
        });
    const std::string bindingsText =
        strutil::join(bindings, ", ", [](const std::pair<std::string, Type *> &b) {
            return b.first + " = " + b.second->toString();
        });
    // The `constas` values in the order the declaration writes them, for the instance name
    // and the label of a diagnostic reported in the instance.
    std::string constasValues, constasText;
    for (size_t i = 0; i < tmpl->funcType->normTypesCount(); ++i) {
        if (!tmpl->funcType->normIsConstasAt(i)) {
            continue;
        }
        const std::string name(tmpl->funcType->argNameAt(
            tmpl->funcType->withTypesCount() + i));
        const auto found = constas.find(name);
        if (found == constas.end()) {
            continue;
        }
        constasValues += (constasValues.empty() ? "" : ", ") + constasTextOf(found->second.data);
        constasText +=
            (constasText.empty() ? "" : ", ") + name + " = " + constasTextOf(found->second.data);
    }
    const std::string instanceArguments =
        bindingsText.empty() ? constasText
                             : constasText.empty() ? bindingsText
                                                   : bindingsText + ", " + constasText;
    const std::string instanceTypes =
        typeText.empty() ? constasValues
                         : constasValues.empty() ? typeText : typeText + ", " + constasValues;
    const std::string instanceName = std::format("{}<{}>", tmpl->name, instanceTypes);
    // A type argument built out of the one an instance on the call cycle already holds asks for
    // an instance at every level (`T`, then `T[]`, then `T[][]`), so the family of instances has
    // no fixed point and no count of them is what bounds it. This is refused where the growth
    // first appears rather than by the count below, which stays as the bound of design principles
    // 5.5 for the recursion whose type arguments are many but do not contain one another.
    // Design principles 5.4: a named dimension is not such a growth -- it is not part of the
    // instance key at all -- so only the types the key is made of are compared.
    for (const GenericInstance &open : tmpl->instances) {
        const auto state = bodyStates_.find(open.graph.get());
        if (state == bodyStates_.end() || state->second != BodyState::Open) {
            continue;
        }
        for (const auto &[name, type] : bindings) {
            Type *earlier = findTypeBinding(open.bindings, name);
            if (earlier == nullptr || earlier->equals(type) ||
                !typeHasProperPart(type, earlier)) {
                continue;
            }
            diags_->of(SemanticDiag::GenericTypeArgumentGrows)
                .atOrigin(call->origin())
                .commit(name, tmpl->name, earlier->toString(), type->toString());
            throw BuildAbortException();
        }
    }
    if (tmpl->openInstances >= kMaxOpenInstancesOfOne ||
        instantiationDepth_ >= kMaxInstantiationDepth) {
        diags_->of(SemanticDiag::GenericInstantiationTooDeep)
            .atOrigin(call->origin())
            .commit(
                tmpl->name,
                typeBindingsToString(bindings),
                tmpl->openInstances >= kMaxOpenInstancesOfOne
                    ? std::format(
                          "{} instances of it are already being built inside one another",
                          tmpl->openInstances)
                    : std::format(
                          "{} instantiations are already nested inside one another",
                          instantiationDepth_));
        throw BuildAbortException();
    }

    // Where an error in the body is reported it also says which call asked for the instance:
    // a secondary location at the call. The one-line format prints it after the message as
    // " [in the instance ..., instantiated by the call at line:col]".
    const DiagLabel instanceLabel{
        SourceRange{call->origin()},
        tmpl->name.empty()
            ? std::format(
                  "in a list comprehension whose elements have type '{}'",
                  bindings.empty() ? std::string("?") : bindings.front().second->toString())
            : std::format(
                  "in the instance '{}' of the generic function '{}' with {}, instantiated by "
                  "the call",
                  instanceName,
                  tmpl->name,
                  instanceArguments),
        ""};

    // The instance is built where the function was declared, not where it is called.
    struct BuildState {
        Builder &self;
        compile_graph_ptr_t graph;
        node_scope_ptr_t nodeScope;
        graph_scope_ptr_t graphScope;
        decorated_scope_ptr_t decoratedScope;
        template_scope_ptr_t templateScope;
        type_bindings_t bindings;
        ExtentValues extents;
        ConstasValues constas;
        bool synced, varied, declaresVariable, calleeAllowed;
        node_handle_t lastSynced;
        camel::core::module::module_ptr_t module;
        std::vector<std::string> namespacePath;
        std::vector<const std::vector<Reference> *> usingFrames;
        GenericTemplate &opened;
        BuildState(Builder &b, GenericTemplate &t)
            : self(b), graph(b.currGraph_), nodeScope(b.nodeScope_), graphScope(b.graphScope_),
              decoratedScope(b.decoratedScope_), templateScope(b.templateScope_),
              bindings(b.typeBindings_), extents(b.extentBindings_), constas(b.constasBindings_),
              synced(b.synced_), varied(b.varied_),
              declaresVariable(b.declaresVariable_), calleeAllowed(b.genericCalleeAllowed_),
              lastSynced(b.lastSyncedNode_), module(b.module_), namespacePath(b.namespacePath_),
              usingFrames(b.usingFrames_), opened(t) {
            ++self.instantiationDepth_;
            ++opened.openInstances;
        }
        ~BuildState() {
            --opened.openInstances;
            self.module_               = module;
            self.namespacePath_        = std::move(namespacePath);
            self.usingFrames_          = std::move(usingFrames);
            self.currGraph_            = graph;
            self.nodeScope_            = nodeScope;
            self.graphScope_           = graphScope;
            self.decoratedScope_       = decoratedScope;
            self.templateScope_        = templateScope;
            self.typeBindings_         = std::move(bindings);
            self.extentBindings_       = std::move(extents);
            self.constasBindings_      = std::move(constas);
            self.synced_               = synced;
            self.varied_               = varied;
            self.declaresVariable_     = declaresVariable;
            self.genericCalleeAllowed_ = calleeAllowed;
            self.lastSyncedNode_       = lastSynced;
            --self.instantiationDepth_;
        }
    };

    // Whatever the body of this instance says goes to the scope's sink while it is built: a body
    // that does not compile for the types it was tried with may belong to a candidate nobody wants,
    // and it is the one thing a search is allowed to hold back (`SpeculativeInstantiation`). With no
    // scope open this changes nothing.
    BodyDiagnostics bodyDiags(*this);
    const size_t instanceIndex = tmpl->instances.size();
    const size_t diagsBefore   = diags_->count();
    const size_t errorsBefore  = diags_->count(Severity::Error);
    // The bookkeeping of a body that failed, wherever it failed: the instance is remembered as
    // failed (a second attempt at the same types is answered without building it again), and what
    // it said is located and labelled as the instance's.
    const auto markFailed = [&] {
        if (instanceIndex < tmpl->instances.size()) {
            tmpl->instances[instanceIndex].failed = true;
        }
        if (foreign) {
            diags_->locateByOriginFrom(diagsBefore);
        }
        diags_->addRelatedToErrorsFrom(diagsBefore, instanceLabel, 3);
    };
    compile_graph_ptr_t graph;
    try {
        BuildState saved(*this, *tmpl);
        AttrScope attrs(currentAttrs_, tmpl->attrs);
        currGraph_      = tmpl->ownerGraph;
        module_         = tmpl->module;
        namespacePath_  = tmpl->namespacePath;
        usingFrames_    = tmpl->usingFrames;
        nodeScope_      = tmpl->nodeScope;
        graphScope_     = tmpl->graphScope;
        decoratedScope_ = tmpl->decoratedScope;
        templateScope_  = tmpl->templateScope;
        typeBindings_   = tmpl->outerBindings;
        typeBindings_.insert(typeBindings_.end(), bindings.begin(), bindings.end());
        extentBindings_ = extents;
        constasBindings_ = constas;
        synced_               = false;
        varied_               = false;
        declaresVariable_     = false;
        genericCalleeAllowed_ = false;
        lastSyncedNode_       = {};

        FunctionType *funcType = instantiateFunctionType(tmpl->funcType, bindings, &extents);
        const auto graphId = context_->graphIds().allocate();
        graph              = tmpl->name.empty()
                                 ? createCompileGraph(graphId, funcType, currGraph_)
                                 : createCompileGraph(graphId, funcType, currGraph_, instanceName);
        if (const auto declaring = units_.find(tmpl->module.get());
            declaring != units_.end() && !tmpl->name.empty()) {
            // Shown with the module that declares it (design principles 10.2).
            graph->setName(qualifiedName(*declaring->second, instanceName));
        }
        if (currGraph_ == rootGraph_) {
            // The instance of a top-level generic function belongs to the module declaring it.
            const auto declaring = units_.find(tmpl->module.get());
            rootSubGraphOwners_[graph.get()] =
                declaring != units_.end() ? declaring->second.get() : unit_;
        }
        tmpl->instances.push_back({bindings, constas, extents, graph, false});
        // A generic `#impl` function's executor is chosen here, at instantiation, because this
        // is where the instance's types are known (rule impl-kernel-site). An instance
        // whose type has no executor keeps the body and runs it: the body is the semantics, so
        // the result is the same, and `std::profile` tells the two paths apart.
        adoptImplKernel(tmpl, graph, funcType);
        // The instance takes the declaration's parameter defaults as they are: they are
        // constants, the same for every instance, and a call of the instance fills them.
        if (const auto found = defaults_.find(tmpl.get()); found != defaults_.end()) {
            defaults_[graph.get()] = found->second;
        }
        // Scopes are kept by name under the scope they were entered from, here the scope the
        // function was declared in: two functions written in place, which have no name to
        // tell them apart, need two scopes; so do two generic overloads, which share their
        // name. Two overloads can also be given the same instance name -- `init::<P>(opt: Sgd,
        // params: P)` and `init::<P>(opt: Adam, params: P)` are both `init<Model>` for a
        // `Model` -- and they must not share a scope either: one instance's parameters would
        // answer for the other's names.
        const bool severalUnderOneName = [&] {
            if (tmpl->name.empty() || tmpl->templateScope == nullptr) {
                return false;
            }
            const auto declared = tmpl->templateScope->get(tmpl->name);
            return declared.has_value() && declared->group() != nullptr &&
                   declared->group()->size() > 1;
        }();
        const bool sharedName = tmpl->name.empty() || severalUnderOneName ||
                                camel::core::isOverloadableOperatorGroup(tmpl->func->name());
        enterGraphScope(
            graph,
            sharedName ? std::format("{}@{:p}", instanceName, static_cast<void *>(tmpl.get()))
                       : instanceName);
        // The body keeps the `on nodes` tags of the generic declaration: they are the same for
        // every instance, and they were resolved once (tagFactsOf remembers by node).
        const TagFacts tags = tagFactsOf(tmpl->func, camel::core::tag::Func);
        if (camel::core::tag::declaresMacro(tags.declaration)) {
            graph->draft().setFlags(static_cast<uint8_t>(
                graph->draft().flags() | camel::runtime::kGraphFlagMacro));
        }
        buildFuncBody(tmpl->func, graph, funcType, instanceName, tags);
        leaveScope();
    } catch (const BuildAbortException &) {
        markFailed();
        // A body that does not compile for the types it was tried with is not a function this
        // position necessarily wants: inside a scope the attempt is answered with "no instance" and
        // what the body said stays in the scope's sink (`SpeculativeInstantiation`). Outside one the
        // diagnostic stands and the build aborts, as it always did.
        if (speculative_ != nullptr) {
            return nullptr;
        }
        throw;
    } catch (...) {
        // Not a language error: nothing about it is the caller's to judge.
        markFailed();
        throw;
    }
    // A statement of the body that fails is skipped so that the rest is still checked; the
    // instance is unusable all the same.
    if (diags_->count(Severity::Error) > errorsBefore) {
        markFailed();
        if (speculative_ != nullptr) {
            return nullptr;
        }
        throw BuildAbortException();
    }
    return graph;
}

Type *Builder::resolveTypeNode(Type *type, const GCT::node_ptr_t &at) {
    if (type == nullptr) {
        return nullptr;
    }
    if (type->code() == TypeCode::TypeOf) {
        // `typeof e`: build e where the type is used and take the type of the value it makes.
        // A build-time tuple is taken without a FILL, as `indices` and `...` take one.
        const auto *placeholder = tt::as_ptr<TypeOfType>(type);
        const auto *held       = static_cast<const GCT::Node *>(placeholder->expression());
        ASSERT(held != nullptr, "typeof holds the expression it was made with.");
        GCT::Node *expression = const_cast<GCT::Node *>(held);
        Type *valueType       = nullptr;
        const std::any result = visit(expression);
        if (result.type() == typeid(StaticTuple)) {
            valueType = any_cast<const StaticTuple &>(result).type;
        } else if (node_handle_t value = valueNodeOf(result)) {
            valueType = valueTypeOf(nodeTypeOf(value));
        }
        if (valueType == nullptr || valueType->code() == TypeCode::Void) {
            diags_->of(SemanticDiag::TypeOfNotAValue)
                .atOrigin(expression->origin())
                .commit(sourceTextOf(expression), valueType == nullptr ? "no value" : "void");
            throw BuildAbortException();
        }
        return boundType(valueType);
    }
    if (containsTypeOf(type)) {
        diags_->of(SemanticDiag::TypeOfNotWholeType)
            .atOrigin(at != nullptr ? at->origin() : 0)
            .commit(type->toString());
        throw BuildAbortException();
    }
    return boundType(type);
}

Type *Builder::visitTypeNode(const GCT::node_ptr_t &gct) {
    ENTER("TYPE");
    Type *type = resolveTypeNode(gct->dataType(), gct);
    LEAVE("TYPE");
    return type;
}

} // namespace camel::compile::gir
