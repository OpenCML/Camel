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
#include "camel/core/type/typecall.h"

#include <algorithm>
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

/// True for a type whose compile-time values an instance can be keyed by: the kinds a value in a
/// static slot has (an integer, a real, a bool or a string). A rule's `with` value is half of the
/// key its instance is made under and is read from that slot, so a value of any other type has no
/// instance to name (design principles 5.4, 5.5).
bool ruleValueKind(Type *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Float16:
    case TypeCode::BFloat16:
    case TypeCode::Bool:
    case TypeCode::String:
        return true;
    default:
        return false;
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
            // instantiated by its resolver when it is called, not from a template (design principles
            // 10.4).
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
        // The tags written before the declaration belong to it, as they do for a function that is
        // not generic (a generic pass names the tags it reads).
        if (!isOperator) {
            const TagFacts tags = tagFactsOf(child, camel::core::tag::Func);
            if (tags.declaration != camel::core::attr::kNoAttrs) {
                camel::core::tag::attachToDeclaration(*module_, Reference(name), tags.declaration);
            }
        }
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

/// The names of the `constas` parameters a template's functions declare, in the order the
/// declaration writes them: a rule's `with` parameters are those, and every function the rule
/// becomes takes one per parameter. Empty when the declaration does not declare `expected` of
/// them, which is how the maker learns that the values it was given are not the ones the rule
/// takes (design principles 5.4, 5.5).
namespace {
std::vector<std::string>
constasParameterNames(const generic_template_ptr_t &tmpl, size_t expected) {
    std::vector<std::string> names;
    if (tmpl == nullptr || tmpl->funcType == nullptr) {
        return names;
    }
    const size_t withCount = tmpl->funcType->withTypesCount();
    for (size_t i = 0; i < tmpl->funcType->normTypesCount(); ++i) {
        if (tmpl->funcType->normIsConstasAt(i)) {
            names.emplace_back(tmpl->funcType->argNameAt(withCount + i));
        }
    }
    if (names.size() != expected) {
        names.clear();
    }
    return names;
}
} // namespace

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
    // `__rule_generic(name, two_way, needs, consts, <the sides>[, <the with values>])`, what the
    // front end writes for a rule that is instantiated: one whose head declares type parameters,
    // and one that declares `with` parameters. The sides are the rule's own generic functions
    // (`name__lhs`, `name__rhs`, and `name__when` when it has a condition); each `with` parameter
    // follows them as the value it is bound to, in the order the declaration writes them, so the
    // rule value is a `Rule` of the values it was bound to and the instance is the one for the
    // type arguments and those values together (design principles 5.4, 5.5).
    constexpr size_t kFixed = 4;
    if (normInputs.size() <= kFixed) {
        return;
    }
    std::vector<generic_template_ptr_t> sides;
    size_t firstValue = kFixed;
    for (; firstValue < normInputs.size(); ++firstValue) {
        const auto found = genericDrefs_.find(normInputs[firstValue].node);
        if (found == genericDrefs_.end()) {
            break;
        }
        sides.push_back(found->second.one());
    }
    if (sides.size() < 2 || sides.size() > 3) {
        return;
    }
    const size_t withCount                    = normInputs.size() - firstValue;
    const std::vector<std::string> withParams = constasParameterNames(sides.front(), withCount);
    if (withParams.size() != withCount) {
        // The values the maker was given are not the ones the rule's functions declare: the
        // resolution of the call reports the argument list, as it does for any other argument.
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
        withParams, sides[0], sides[1], sides.size() == 3 ? sides[2] : nullptr);
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
    // The rule value keeps the id of its templates and the values it was bound to, and the maker is
    // called with them: what a rule that is instantiated holds is not a function of the program
    // being built, so nothing of the declaration enters this graph but those (design principles
    // 5.5; the rule on generic-rule-instantiation).
    std::vector<Argument> values(normInputs.begin() + static_cast<ptrdiff_t>(firstValue), normInputs.end());
    std::vector<Type *> valueTypes(
        normTypes.begin() + static_cast<ptrdiff_t>(firstValue), normTypes.end());
    normInputs.resize(kFixed + 1 + values.size());
    normInputs[kFixed] = Argument{idNode, call, std::nullopt};
    for (size_t i = 0; i < values.size(); ++i) {
        normInputs[kFixed + 1 + i] = values[i];
    }
    normTypes.resize(kFixed + 1 + valueTypes.size());
    normTypes[kFixed] = Type::Int64();
    for (size_t i = 0; i < valueTypes.size(); ++i) {
        normTypes[kFixed + 1 + i] = valueTypes[i];
    }
}

// ---- A generic fact, asked for by its own query (rewrite design 7.5) ----

void Builder::settleGenericFactMaker(
    std::vector<Argument> &normInputs, type_vec_t &normTypes, const GCT::node_ptr_t &call) {
    // `__fact(name, <the fact's type parameters>, <its functions>, withCount, <with arguments>,
    // <queried nodes>, <case triples>)`, what the front end writes for a `fact` declaration. A
    // fact whose head declares type parameters is one fact per type argument (design principles
    // 5.5): the functions it is made of are templates and have no value of their own, so the
    // instances of the fact this call stands for are built here, from the instance of the fact's
    // query -- the type arguments are the ones that instance was resolved with, the ones written
    // at the query (`range::<int>(n)`), which is the channel rules and facts share (design
    // principles 5.9) and the only channel a caller that holds a node has. A query whose arguments
    // fix none of them is reported here and asks for them. They are part of what the fact is: two
    // instances at two types are two facts, and the written arguments name it (`range<int>`).
    constexpr size_t kTypes   = 1;
    constexpr size_t kInitial = 2;
    if (normInputs.size() <= kInitial) {
        return;
    }
    // The fact's functions, as the front end named them: every argument of the call that is one is
    // generic, and each is instantiated at the types the query was resolved with. They are not all
    // of them leading -- the `with` count, the `with` values and the queried nodes sit between the
    // fact's clauses and its cases (`__fact('range', 'int', initial, join, ..., 0, n, case0,
    // case0_fact, ...)`, modules/rewrite/fact.h), and a generic function left uninstantiated in an
    // argument position is a function value the operator cannot take.
    std::vector<size_t> at;
    std::vector<generic_template_ptr_t> made;
    for (size_t i = kInitial; i < normInputs.size(); ++i) {
        const auto found = genericDrefs_.find(normInputs[i].node);
        if (found == genericDrefs_.end()) {
            continue;
        }
        at.push_back(i);
        made.push_back(found->second.one());
    }
    if (made.empty()) {
        // A fact whose head declares no type parameters is the value its functions already are.
        return;
    }
    const std::vector<std::string> names = typeParameterNames(made.front());
    if (names.empty()) {
        return;
    }
    // The fact's name, as a reader writes it: the functions the declaration becomes are its name
    // with a suffix (`range__initial`, `range__case0_fact`).
    std::string factName = made.front()->name;
    if (const size_t scope = factName.rfind("::"); scope != std::string::npos) {
        factName = factName.substr(scope + 2);
    }
    if (const size_t side = factName.rfind("__"); side != std::string::npos) {
        factName.resize(side);
    }
    type_bindings_t bindings;
    std::string arguments;
    std::string declared;
    for (const std::string &one : names) {
        declared += (declared.empty() ? "" : ", ") + one;
        Type *bound = findTypeBinding(typeBindings_, one);
        if (bound == nullptr || containsTypeVar(bound)) {
            // A type the query does not fix is no instance of the fact: the type arguments of a
            // fact whose head declares type parameters are the ones its query was resolved with,
            // and a query whose arguments fix none of them has to write them
            // (`range::<int>(n)`) -- the same answer any generic call gives, and this is where
            // the instance the query asked for is built.
            diags_->of(SemanticDiag::GenericArgumentsMismatch)
                .atOrigin(call->origin())
                .commit(
                    factName,
                    declared,
                    "the type variable(s) " + one +
                        " cannot be determined from the arguments or expected result; give an "
                        "expected type or explicit " +
                        factName + "::<...> type arguments",
                    factName + "::<" + declared + ">");
            throw BuildAbortException();
        }
        bindings.emplace_back(one, bound);
        arguments += (arguments.empty() ? "" : ", ") + bound->toString();
    }
    for (size_t i = 0; i < made.size(); ++i) {
        const compile_graph_ptr_t instance = instantiateGeneric(made[i], bindings, {}, {}, call);
        if (instance == nullptr) {
            return; // the instance reported its own reason, at the declaration
        }
        normInputs[at[i]] = Argument{createFuncDataNode(instance, true, false), call, std::nullopt};
        normTypes[at[i]]  = valueTypeOf(nodeTypeOf(normInputs[at[i]].node));
    }
    // What the type arguments are: the fact's key reads them, and the engine makes one fact per
    // key (`buildDefinition`), so the instances of two types never share a value or a kind.
    const data_ptr_t written = tt::as_shared<Data>(std::make_shared<StringData>(arguments));
    const node_handle_t typesNode = currGraph_->addStaticDataNode(written);
    rememberConstant(typesNode, written);
    registerNodeOrigin(context_, typesNode, call, "gir.fact.types");
    normInputs[kTypes] = Argument{typesNode, call, std::nullopt};
    normTypes[kTypes]  = Type::String();
}

bool Builder::instantiateRuleTemplates(
    const GenericRuleEntry &entry, const std::vector<Type *> &types, const RuleWithArguments &with,
    compile_graph_ptr_t &lhs, compile_graph_ptr_t &rhs, compile_graph_ptr_t &when,
    std::string &why) {
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
    // The values the rule value carries are the `constas` parameters of every function of the
    // declaration -- one per `with` parameter, in the order the declaration writes them -- and the
    // instance reads each from a static slot (design principles 5.4, 5.5). They are part of the
    // instance key, and an integer one is an extent the declaration's types may use.
    ConstasValues constas;
    ExtentValues extents;
    if (!entry.withParams.empty()) {
        if (with.size() != entry.withParams.size()) {
            why = std::format(
                "it takes {} `with` parameter(s), and it was asked for {}",
                entry.withParams.size(),
                with.size());
            return false;
        }
        FunctionType *signature = lhsTemplate->funcType;
        size_t at               = 0;
        for (size_t i = 0; i < signature->normTypesCount() && at < with.size(); ++i) {
            if (!signature->normIsConstasAt(i)) {
                continue;
            }
            const data_ptr_t bound = with[at];
            ConstasValue value;
            value.type      = signature->normTypeAt(i);
            value.valueType = bound != nullptr ? bound->type() : nullptr;
            // The declaration's own value stays as the rule value holds it: what the instance does
            // with the constant -- putting it in its parameter's type -- is the instance's own.
            value.data = bound != nullptr ? bound->clone() : nullptr;
            constas[entry.withParams[at]] = std::move(value);
            if (bound != nullptr) {
                if (const auto extent = constasExtentOf(bound); extent.has_value()) {
                    extents[entry.withParams[at]] = *extent;
                }
            }
            ++at;
        }
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
        lhs = instantiateGeneric(lhsTemplate, bindings, extents, constas, origin);
        rhs = lhs != nullptr ? instantiateGeneric(rhsTemplate, bindings, extents, constas, origin)
                             : nullptr;
        when = lhs != nullptr && rhs != nullptr && whenTemplate != nullptr
                   ? instantiateGeneric(whenTemplate, bindings, extents, constas, origin)
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
    FunctionType *signature, std::vector<Argument> &withInputs, type_vec_t *withTypes,
    std::vector<Argument> &normInputs, type_vec_t *normTypes, const GCT::node_ptr_t &call,
    ConstasValues &constas, ExtentValues &extents) {
    const size_t withCount     = signature->withTypesCount();
    const size_t declaredCount = signature->normTypesCount();
    // The value of one argument written at the position of a `constas` parameter: a literal, a
    // compile-time value handed on from another node (the `n` of a `static for`, a `constas`
    // forwarded to another `constas`, the `j` of `indices`), or a call of a pure function that
    // reduces to one. Anything else is refused where the argument is written.
    const auto take = [&](Argument &argument, const std::string &name, Type *declared) {
        const GCT::node_ptr_t &expr = argument.expr;
        Type *valueType =
            argument.node != nullptr ? valueTypeOf(nodeTypeOf(argument.node)) : nullptr;
        data_ptr_t data =
            expr != nullptr && expr->type() == GCT::NodeKind::DATA ? expr->data() : nullptr;
        if (data == nullptr && argument.node != nullptr) {
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
                .commit(name, declared->toString());
            throw BuildAbortException();
        }
        ConstasValue value;
        value.type      = declared;
        value.valueType = valueType;
        value.data      = data;
        value.node      = argument.node;
        constas[name]   = std::move(value);
        if (auto extent = constasExtentOf(data); extent.has_value()) {
            extents[name] = *extent;
        }
    };
    // The `with` arguments are complete when they get here (a default fills the ones a call
    // leaves out), written one per parameter; the ordinary arguments are written one per
    // parameter too, `constas` values included, so the parameter of argument `i` is parameter
    // `i` of its list. A rest call has more ordinary arguments than that: the ones after the
    // fixed parameters are the rest parameter's and are kept.
    if (withTypes != nullptr || withCount > 0) {
        std::vector<Argument> runtimeWith;
        type_vec_t runtimeWithTypes;
        for (size_t i = 0; i < withInputs.size(); ++i) {
            if (i < withCount && signature->withIsConstasAt(i)) {
                take(withInputs[i], std::string(signature->argNameAt(i)), signature->withTypeAt(i));
                continue;
            }
            runtimeWith.push_back(std::move(withInputs[i]));
            if (withTypes != nullptr && i < withTypes->size()) {
                runtimeWithTypes.push_back((*withTypes)[i]);
            }
        }
        withInputs = std::move(runtimeWith);
        if (withTypes != nullptr) {
            *withTypes = std::move(runtimeWithTypes);
        }
    }
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
        take(normInputs[i], name, signature->normTypeAt(i));
    }
    for (size_t i = declaredCount; i < normInputs.size(); ++i) {
        keep(i);
    }
    normInputs = std::move(runtimeInputs);
    if (normTypes != nullptr) {
        *normTypes = std::move(runtimeTypes);
    }
}

/// The compile-time value one `with` argument of a rule binds is, held to the type of its
/// parameter. The value is one the compiler knows while the program is compiled -- a literal, a
/// constant, a `static for` counter, a `constas` forwarded, a pure call that reduces to one -- and
/// it is of a kind an instance can be keyed by: what a rule is bound to is part of the key its
/// instance is made under (design principles 5.4, 5.5). Anything else is refused where it is
/// written, and the attempt throws.
data_ptr_t Builder::ruleWithArgumentValue(
    const Argument &argument, Type *declared, const std::string &ruleName,
    const std::string &paramName, const GCT::node_ptr_t &at) {
    const GCT::node_ptr_t &expr = argument.expr;
    Type *valueType = argument.node != nullptr ? valueTypeOf(nodeTypeOf(argument.node)) : nullptr;
    data_ptr_t data =
        expr != nullptr && expr->type() == GCT::NodeKind::DATA ? expr->data() : nullptr;
    if (data == nullptr && argument.node != nullptr) {
        // A compile-time value handed on from another node: a name bound to a constant, the `n` of
        // a `static for`, a `constas` value forwarded.
        data = constantOfNode(argument.node);
    }
    if (data == nullptr && valueType != nullptr && argument.node != nullptr) {
        if (const StaticArg constant = staticArgOf(argument.node); constant.value) {
            data = dataOfStaticValue(valueType, *constant.value);
        }
    }
    if (data == nullptr && expr != nullptr && expr->type() == GCT::NodeKind::LINK) {
        // A call of a pure function with constant arguments is a constant (design principles 5.1,
        // 6.5): one that does not reduce to a constant is refused here by name, not as a value
        // without one.
        const FoldedCall folded = foldConstantCall(expr);
        if (folded.data != nullptr) {
            data = folded.data;
        } else if (folded.refusal != FoldRefusal::NotAProgramCall) {
            reportFoldRefusal(folded, expr);
        }
    }
    if (data != nullptr && declared != nullptr && !declared->equals(data->type())) {
        // The value is what the instance is keyed by and what its static slot holds, so it is put
        // in the type the parameter declares here, once, where the binding is written.
        if (data_ptr_t converted = data->clone()->convertTo(declared); converted != nullptr) {
            data = std::move(converted);
        }
    }
    if (data == nullptr || !ruleValueKind(data->type())) {
        diags_->of(SemanticDiag::RuleWithArgumentNotConstant)
            .atOrigin(expr != nullptr && expr->origin() != 0 ? expr->origin() : at->origin())
            .commit(paramName, ruleName, declared != nullptr ? declared->toString() : "?");
        throw BuildAbortException();
    }
    return data;
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
    // A candidate named by its signature -- which the siblings of one name share -- and by the
    // condition it wrote, which is what tells two arms of that name apart (design principles 4.4:
    // the call site lists the candidates with the conditions each of them holds under).
    const auto describeCandidate = [](const generic_template_ptr_t &tmpl) {
        std::string text = tmpl->name + ": " + tmpl->funcType->toString();
        if (const auto *when = GCT::Tree::of(tmpl->func)->whenOf(tmpl->func)) {
            text += " — when " + when->text;
        }
        return text;
    };
    // The candidates whose parameters take the arguments and whose own `when` is false for the
    // types those arguments bind: kept for the report a call that reaches no candidate at all
    // gets, because a condition that does not hold is not a mismatch of types (design
    // principles 4.4).
    std::vector<std::string> unmetConditions;
    // Design principles 4.4 and 5.1: a call takes the one overload whose parameters accept its
    // arguments, with the argument types first exactly as they are, and then with the literals
    // entering the parameter types. Two that fit are an ambiguity, not a matter of the order
    // they are declared in.
    const auto binds = [&](const generic_template_ptr_t &tmpl) {
        FunctionType *signature = tmpl->funcType;
        if (signature->hasRest() || withTypes.size() > signature->withTypesCount()) {
            return false;
        }
        // A `with` parameter the call leaves out needs a default of this declaration.
        const auto registered = defaults_.find(tmpl.get());
        for (size_t i = 0; i < signature->withTypesCount(); ++i) {
            const bool written = i < withInputs.size() && !withInputs[i].left;
            if (written) {
                continue;
            }
            if (registered == defaults_.end() || i >= registered->second.with.size() ||
                !registered->second.with[i].has_value()) {
                return false;
            }
        }
        const size_t params = signature->normTypesCount() - signature->normConstasCount();
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
        // The declaration's `when`, now that the arguments have bound its type variables: a
        // false condition makes this declaration inapplicable, not erroneous, so it is not a fit
        // and the call goes on to the other candidates (design principles 4.4).
        if (const auto *when = GCT::Tree::of(tmpl->func)->whenOf(tmpl->func)) {
            std::unordered_map<std::string, data_ptr_t> withValues;
            for (size_t i = 0; i < signature->withTypesCount() && i < withInputs.size(); ++i) {
                data_ptr_t value = constantOfNode(withInputs[i].node);
                if (value != nullptr) {
                    withValues.emplace(std::string(signature->argNameAt(i)), value);
                }
            }
            const WhenResult verdict =
                evaluateWhenCondition(when->condition, signature, bindings, withValues, tmpl);
            if (verdict.fails()) {
                unmetConditions.push_back(describeCandidate(tmpl));
                return false;
            }
            if (!verdict.holds()) {
                reportWhenProblem(verdict, tmpl->name, when->condition);
            }
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
        // Two (or more) candidates the arguments fit and whose conditions hold: no declaration
        // order chooses between them, so the call is ambiguous and the report names each
        // candidate with the condition it wrote (design principles 4.4: "带 `when` 的候选都适用
        // 时，调用处报歧义，列出两条候选与各自成立的条件"). Sorted, so that two files holding one
        // program with the declarations in the two orders report one and the same diagnostic.
        std::vector<std::string> also;
        also.reserve(fits.size());
        for (const auto &candidate : fits) {
            also.push_back(describeCandidate(candidate));
        }
        std::sort(also.begin(), also.end());
        also.erase(std::unique(also.begin(), also.end()), also.end());
        rejectAmbiguousCall(candidates.front()->name, argTypes, also, call);
    }
    if (!unmetConditions.empty()) {
        // Every candidate the arguments fit was made inapplicable by its own `when`: the call
        // reports the conditions it could not meet rather than the overloads it did not match
        // (design principles 4.4; questions-2026-10-23 `sorts-per-element-type` 1: "条件不满足与
        // 类型不匹配分开报"). Sorted, for the reason the ambiguity above is sorted.
        std::sort(unmetConditions.begin(), unmetConditions.end());
        unmetConditions.erase(
            std::unique(unmetConditions.begin(), unmetConditions.end()), unmetConditions.end());
        yieldToPipe(call);
        diags_->of(SemanticDiag::WhenConditionUnmet)
            .atOrigin(call->origin())
            .commit(
                candidates.front()->name,
                argTypes,
                strutil::join(unmetConditions, "; ", [](const std::string &s) { return s; }));
        throw BuildAbortException();
    }
    yieldToPipe(call);
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
    // A `constas n: T` parameter is bound by an argument written at its position, in the angle
    // brackets or the parentheses: a compile-time constant, not a runtime one. It selects the
    // instance and is then dropped from the call's inputs (design principles 5.4). A declaration
    // may have any number of them, in any position.
    const size_t declaredWithCount = signature->withTypesCount();
    const size_t withCount         = declaredWithCount - signature->withConstasCount();
    const size_t declaredNormCount = signature->normTypesCount();
    const size_t normCount         = declaredNormCount - signature->normConstasCount();
    // The ordinary parameter of each runtime argument: the `constas` parameters are not ones.
    std::vector<size_t> runtimeIndices;
    runtimeIndices.reserve(normCount);
    for (size_t i = 0; i < declaredNormCount; ++i) {
        if (!signature->normIsConstasAt(i)) {
            runtimeIndices.push_back(i);
        }
    }
    std::vector<size_t> runtimeWithIndices;
    for (size_t i = 0; i < declaredWithCount; ++i) {
        if (!signature->withIsConstasAt(i)) {
            runtimeWithIndices.push_back(i);
        }
    }
    // The parameter of the runtime argument `i`, over all the declared parameters (`with` ones
    // first): the `constas` parameters are not runtime arguments and have none.
    const auto signatureIndexAt = [&](size_t i) {
        return i < withCount ? runtimeWithIndices[i]
                             : declaredWithCount + runtimeIndices[i - withCount];
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
    if (declaredWithCount != withTypes.size()) {
        // Every argument written in the parentheses (`take(xs, 2)`): say which parameters
        // are with-parameters and how the call is written.
        if (withTypes.empty() && normCount > 0 &&
            declaredWithCount + declaredNormCount == normTypes.size() &&
            signature->argNamesCount() >= declaredWithCount + declaredNormCount) {
            const auto names = [&](size_t from, size_t count) {
                std::string out;
                for (size_t i = 0; i < count; ++i) {
                    out += (i == 0 ? "" : ", ") + std::string(signature->argNameAt(from + i));
                }
                return out;
            };
            const std::string withs = names(0, declaredWithCount), norms = [&] {
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
                declaredWithCount,
                withTypes.size()));
    }
    if (declaredNormCount != normTypes.size()) {
        // A call that stops before a `constas` parameter is missing its value, not only an
        // argument: say so, since the value is not a runtime one.
        if (normTypes.size() < declaredNormCount && signature->normIsConstasAt(normTypes.size())) {
            diags_->of(SemanticDiag::ConstasArgumentMissing)
                .atOrigin(call->origin())
                .commit(std::string(
                    signature->argNameAt(declaredWithCount + normTypes.size())));
            throw BuildAbortException();
        }
        reject(std::format(
            "it takes {} argument(s) and {} were given", declaredNormCount, normTypes.size()));
    }
    ConstasValues constas;
    ExtentValues extents;
    takeConstasValues(
        signature, withInputs, &withTypes, normInputs, &normTypes, call, constas, extents);
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
        const size_t at = signatureIndexAt(i);
        Type *pattern   = at < declaredWithCount ? signature->withTypeAt(at)
                                                 : signature->normTypeAt(at - declaredWithCount);
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
        Type *bound = findTypeBinding(bindings, name);
        if (bound == nullptr) {
            open.push_back(name);
            continue;
        }
        // A variable the arguments bound to a type that states no element type is not inferred:
        // the only argument that mentions it was an empty literal, which says no element type and
        // leaves the position to say one, and a generic parameter states none (design principles
        // 5.1, 5.2). `void` is the "no value" marker, not an element type, so the binding is not
        // a type the call can be made at.
        if (isElementTypeUnstated(bound)) {
            diags_->of(SemanticDiag::FunctionTypeArgumentNotInferred)
                .atOrigin(call->origin())
                .commit(name, tmpl->name);
            throw BuildAbortException();
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
    for (size_t p = 0; p < declaredWithCount + declaredNormCount; ++p) {
        const bool inWith = p < declaredWithCount;
        if (inWith ? !signature->withIsConstasAt(p)
                   : !signature->normIsConstasAt(p - declaredWithCount)) {
            continue;
        }
        const std::string name(signature->argNameAt(p));
        ConstasValue &value = constas.at(name);
        Type *declaredType = inWith ? signature->withTypeAt(p)
                                    : signature->normTypeAt(p - declaredWithCount);
        Type *wanted =
            substituteTypeVars(substituteExtentValues(declaredType, extents), bindings);
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
    // makes this candidate inapplicable (design principles 4.4), so the call is reported with
    // the condition it could not meet rather than with a mismatch of types.
    if (const auto *when = GCT::Tree::of(tmpl->func)->whenOf(tmpl->func)) {
        std::unordered_map<std::string, data_ptr_t> withValues;
        for (size_t i = 0; i < withCount && i < withInputs.size(); ++i) {
            data_ptr_t value = constantOfNode(withInputs[i].node);
            if (value != nullptr) {
                withValues.emplace(std::string(signature->argNameAt(runtimeWithIndices[i])), value);
            }
        }
        for (size_t i = 0; i < declaredWithCount; ++i) {
            if (signature->withIsConstasAt(i)) {
                const std::string name(signature->argNameAt(i));
                withValues.emplace(name, constas.at(name).data);
            }
        }
        const WhenResult verdict =
            evaluateWhenCondition(when->condition, signature, bindings, withValues, tmpl);
        if (verdict.fails()) {
            yieldToPipe(call);
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
        return ops->resolve(withTypes, normTypes, statics).has_value();
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
                    if (auto one = resolver->resolve({}, {first})) {
                        add(uri, *one);
                    }
                    for (Type *second : candidates) {
                        if (auto two = resolver->resolve({}, {first, second})) {
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
    // A name that has no type until a position of function type resolves it is not bound to a free
    // type variable: the position `T` of `println(x: T, ..rest)` says nothing about which function
    // the name stands for, so the name stays unsettled and is refused. A rule whose signature is
    // not declared (a computed one) says nothing here, and the name is tried as before.
    const auto positionTakesFunction = [&](bool with, size_t index) {
        for (const auto &[uri, resolver] : ops->resolvers()) {
            FunctionType *sig = resolver->fixedSignature();
            if (sig == nullptr) {
                sig = resolver->declaredSignature();
            }
            if (sig == nullptr) {
                return true;
            }
            const size_t count = with ? sig->withTypesCount() : sig->normTypesCount();
            const bool pack    = with ? sig->hasWithRest() : sig->hasRest();
            if (index >= count - (pack ? 1 : 0)) {
                continue; // a pack position, or no position of this rule: any type, free
            }
            Type *param = with ? sig->withTypeAt(index) : sig->normTypeAt(index);
            if (param->code() != TypeCode::TypeVar) {
                return true;
            }
        }
        return false;
    };
    for (size_t i = 0; i < withInputs.size(); ++i) {
        if (isWaiting(withInputs[i]) && positionTakesFunction(true, i)) {
            settleOne(withInputs[i], withTypes[i]);
        }
    }
    for (size_t i = 0; i < normInputs.size(); ++i) {
        if (isWaiting(normInputs[i]) && positionTakesFunction(false, i)) {
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
    const generic_template_ptr_t &tmpl, const type_bindings_t &callBindings,
    const ExtentValues &extents, const ConstasValues &constas, const GCT::node_ptr_t &call) {
    ExpectedValueScope instanceScope(expectedValueType_, nullptr);
    // The key of an instance is the template's own type parameters and what they are bound to,
    // whatever else the caller passed: a call from the body gives just those, a predicate
    // evaluated for a `when` gives the template's outer bindings first (they are the template's
    // own, kept in `tmpl->outerBindings` and put in front of these when the body is built). Two
    // routes to one instance are one instance, and one diagnostic.
    const std::vector<std::string> ownNames = typeParameterNames(tmpl);
    type_bindings_t bindings;
    for (const auto &binding : callBindings) {
        const bool own = std::find(ownNames.begin(), ownNames.end(), binding.first) !=
                             ownNames.end() &&
                         findTypeBinding(tmpl->outerBindings, binding.first) == nullptr;
        const bool seen = findTypeBinding(bindings, binding.first) != nullptr;
        if (own && !seen) {
            bindings.push_back(binding);
        }
    }
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
    const size_t withParams = tmpl->funcType->withTypesCount();
    for (size_t i = 0; i < withParams + tmpl->funcType->normTypesCount(); ++i) {
        if (i < withParams ? !tmpl->funcType->withIsConstasAt(i)
                           : !tmpl->funcType->normIsConstasAt(i - withParams)) {
            continue;
        }
        const std::string name(tmpl->funcType->argNameAt(i));
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
        adoptImplKernel(tmpl, graph, funcType, bindings);
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
            graph->edit().setFlags(static_cast<uint8_t>(
                graph->view().flags() | camel::runtime::kGraphFlagMacro));
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

Type *Builder::inferTypeOf(const GCT::node_ptr_t &expression) {
    if (expression == nullptr) {
        return nullptr;
    }
    // The graph `e` is built in: it stands inside the one the type is used in, so the names it
    // reads resolve where they are written, and it is nobody's subgraph, so nothing of it is
    // reached from the program.
    //
    // Three things make that graph safe to drop, and all three have to hold before this is
    // changed. (1) Its diagnostics go to a sink of its own: nothing the inference says about `e`
    // reaches the program's diagnostics, because `e` is checked where it is evaluated; the one
    // report the caller gets is the replay further down, for an `e` written nowhere else.
    // (2) The graph, the tail of the chain, `chainedNodes_` and the four scope pointers are
    // restored as a whole afterwards (the guard below), so an inference that stops halfway
    // leaves nothing of itself behind and the next statement is built as if it had not happened.
    // (3) The entries this builder keeps keyed by a graph's address -- `capturedValues_`,
    // `valueUsedGraphs_` -- are erased with the graph, and the graph is not registered as a body
    // of the unit, so no pass over the built bodies ever judges it.
    const compile_graph_ptr_t scratch = createCompileGraph(
        context_->graphIds().allocate(), FunctionType::create(), nullptr, "__typeof__");
    scratch->setOuterGraph(currGraph_);
    std::erase_if(
        unit_->builtBodies,
        [&](const compile_graph_ptr_t &known) { return known.get() == scratch.get(); });

    const compile_graph_ptr_t savedGraph       = currGraph_;
    const node_handle_t savedSynced            = lastSyncedNode_;
    const auto savedChained                    = chainedNodes_;
    const bool savedSyncedFlag                 = synced_;
    const bool savedVaried                     = varied_;
    const bool savedDeclares                   = declaresVariable_;
    const node_scope_ptr_t savedNodes          = nodeScope_;
    const graph_scope_ptr_t savedGraphs        = graphScope_;
    const decorated_scope_ptr_t savedDecorated = decoratedScope_;
    const template_scope_ptr_t savedTemplate   = templateScope_;
    const diagnostics_ptr_t savedDiags         = diags_;
    // What the inference says about `e` is nobody's business: `e` is checked where it is
    // evaluated. The sink is what tells "nothing to report" apart from "the expression is not a
    // value", the way a speculative instantiation keeps what it learns about the bodies it tries.
    auto sink = std::make_shared<Diagnostics>(
        diags_->moduleName(), diags_->modulePath(), diags_->sourceContext());

    // The scopes and the chain of the graph the type is used in are restored as a whole: an
    // inference that stops halfway leaves nothing of itself behind in either.
    struct Restore {
        Builder &self;
        compile_graph_ptr_t graph;
        node_handle_t synced;
        std::unordered_set<node_handle_t> chained;
        bool syncedFlag;
        bool varied;
        bool declares;
        node_scope_ptr_t nodes;
        graph_scope_ptr_t graphs;
        decorated_scope_ptr_t decorated;
        template_scope_ptr_t templ;
        diagnostics_ptr_t diags;
        DraftGraphBuilder *scratch;
        ~Restore() {
            self.nodeScope_        = nodes;
            self.graphScope_       = graphs;
            self.decoratedScope_   = decorated;
            self.templateScope_    = templ;
            self.currGraph_        = graph;
            self.lastSyncedNode_   = synced;
            self.chainedNodes_     = std::move(chained);
            self.synced_           = syncedFlag;
            self.varied_           = varied;
            self.declaresVariable_ = declares;
            self.diags_            = diags;
            // The entries are keyed by the address of the graph that is going away, and another
            // graph may take that address, so they go with it.
            self.capturedValues_.erase(scratch);
            self.valueUsedGraphs_.erase(scratch);
        }
    } restore{*this,
              savedGraph,
              savedSynced,
              savedChained,
              savedSyncedFlag,
              savedVaried,
              savedDeclares,
              savedNodes,
              savedGraphs,
              savedDecorated,
              savedTemplate,
              savedDiags,
              scratch.get()};

    diags_ = sink;
    enterGraphScope(scratch, "__typeof__");
    lastSyncedNode_ = {};
    chainedNodes_.clear();
    Type *type = nullptr;
    try {
        const std::any result = visit(expression);
        if (result.type() == typeid(StaticTuple)) {
            type = any_cast<const StaticTuple &>(result).type;
        } else if (node_handle_t value = valueNodeOf(result)) {
            type = valueTypeOf(nodeTypeOf(value));
        }
    } catch (const BuildAbortException &) {
        type = nullptr;
    }
    if (type == nullptr && !sink->errors().empty()) {
        // The first thing `e` said is why there is no type. `e` written only inside a `typeof` is
        // checked nowhere else, so it is reported here, once, at the `typeof` that kept it.
        savedDiags->add(Diagnostic(*sink->errors().front()));
        throw BuildAbortException();
    }
    return type;
}

Type *Builder::resolveTypeNode(Type *type, const GCT::node_ptr_t &at) {
    if (type == nullptr) {
        return nullptr;
    }
    if (type->code() == TypeCode::TypeOf) {
        // `typeof e`: the type of the value `e` would have, inferred without evaluating it. A
        // build-time tuple is taken without a FILL, as `indices` and `...` take one.
        const auto *placeholder = tt::as_ptr<TypeOfType>(type);
        const auto *held        = static_cast<const GCT::Node *>(placeholder->expression());
        ASSERT(held != nullptr, "typeof holds the expression it was made with.");
        GCT::Node *expression = const_cast<GCT::Node *>(held);
        Type *valueType       = inferTypeOf(expression);
        if (valueType == nullptr || valueType->code() == TypeCode::Void) {
            diags_->of(SemanticDiag::TypeOfNotAValue)
                .atOrigin(expression->origin())
                .commit(sourceTextOf(expression), valueType == nullptr ? "no value" : "void");
            throw BuildAbortException();
        }
        return boundType(valueType);
    }
    if (containsTypeOf(type)) {
        // `typeof e` gives the type `e` would have and evaluates nothing, so it is a type
        // expression, not only a whole type (ruling of 2026-10-31): it may stand as the argument
        // of a type function, and `ElementOf<typeof xs>` is the element type of what `xs` holds.
        // Each `typeof` met on the way down is resolved to the type it stands for; the type
        // function is then applied to a concrete argument like any other.
        std::function<Type *(Type *)> resolveTypeOfs = [&](Type *held) -> Type * {
            if (held->code() == TypeCode::TypeOf) {
                return resolveTypeNode(held, at);
            }
            if (held->code() != TypeCallType::typeCode()) {
                return held;
            }
            const auto *call = tt::as_ptr<TypeCallType>(held);
            std::vector<Type *> arguments;
            bool changed = false;
            for (Type *argument : call->paramsSpan()) {
                Type *resolved = resolveTypeOfs(argument);
                changed        = changed || resolved != argument;
                arguments.push_back(resolved);
            }
            if (!changed) {
                return held;
            }
            // The arguments are concrete now, so the function is one the type it computes
            // stands for; one it does not accept stays a call and is reported where it is used.
            std::string error;
            if (Type *computed = TypeCallType::evaluate(call->name(), arguments, error)) {
                return computed;
            }
            return TypeCallType::create(call->name(), arguments);
        };
        type = resolveTypeOfs(type);
    }
    if (containsTypeOf(type)) {
        diags_->of(SemanticDiag::TypeOfNotWholeType)
            .atOrigin(at != nullptr ? at->origin() : 0)
            .commit(type->toString());
        throw BuildAbortException();
    }
    Type *bound = boundType(type);
    if (containsTypeCall(bound) && !containsTypeVar(bound)) {
        // The variables are bound and the type function does not accept what they were bound to.
        std::string what = bound->toString();
        std::string why  = "it is not defined for its argument";
        describeUnevaluatedTypeCall(bound, what, why);
        diags_->of(SemanticDiag::TypeFunctionNotApplicable)
            .atOrigin(at != nullptr ? at->origin() : 0)
            .commit(what, why);
        throw BuildAbortException();
    }
    return bound;
}

Type *Builder::visitTypeNode(const GCT::node_ptr_t &gct) {
    ENTER("TYPE");
    Type *type = resolveTypeNode(gct->dataType(), gct);
    LEAVE("TYPE");
    return type;
}

} // namespace camel::compile::gir

namespace camel::compile::gir {

std::vector<std::string> genericSignaturesOf(const camel::core::module::Entity &entity) {
    std::vector<std::string> signatures;
    for (const generic_template_ptr_t &tmpl : genericTemplatesOfEntity(entity)) {
        if (tmpl != nullptr && tmpl->funcType != nullptr) {
            signatures.push_back(tmpl->funcType->toString());
        }
    }
    return signatures;
}

} // namespace camel::compile::gir
