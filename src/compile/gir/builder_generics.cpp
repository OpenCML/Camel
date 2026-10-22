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
 * Author: Zhenjie Wei
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The types and the generic instantiation of the GIR builder: the generic templates a
 * module declares, the instance a call makes of one (resolveGenericCall, instantiateGeneric)
 * and the node a type is written as.
 */

#include "builder.h"
#include "builder_support.h"
#include "builder_helpers.h"

#include "camel/compile/gir/static_function.h"
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

void Builder::registerGenericTemplates(const GCT::node_ptr_t &exec) {
    std::unordered_set<std::string> plainNames;
    for (size_t i = 0; i < exec->size(); ++i) {
        const auto &child = exec->at(i);
        if (child->type() != GCT::NodeKind::FUNC) {
            continue;
        }
        const std::string name = child->name();
        Type *type             = child->at(0)->dataType();
        if (!name.empty() && !isGenericHere(type)) {
            plainNames.insert(name);
        }
    }
    for (size_t i = 0; i < exec->size(); ++i) {
        const auto &child = exec->at(i);
        if (child->type() != GCT::NodeKind::FUNC) {
            continue;
        }
        const std::string name = child->name();
        if (child->nativeDeclaration()) {
            // A body-less declaration is a rule of an operator, generic or not; a generic one is
            // instantiated by its resolver when it is called, not from a template (handover
            // H19, step 1).
            continue;
        }
        Type *type = child->at(0)->dataType();
        if (name.empty() || !isGenericHere(type)) {
            continue;
        }
        // One name is either a generic function or a set of ordinary ones: which of two
        // generic functions, or of a generic and an ordinary one, a call means is not defined.
        // An operator is the exception: its overloads, generic or not, are one set, and a call
        // that more than one of them accepts is an error (chooseOperatorOverload).
        const bool isOperator = camel::core::isOverloadableOperatorGroup(name);
        if (!isOperator && (templateScope_->has(name, false) || plainNames.contains(name))) {
            diags_->of(SemanticDiag::Redeclaration).atOrigin(child->origin()).commit(name);
            continue;
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
        if (isOperator) {
            operatorTemplates_[name].push_back({templateScope_, tmpl});
        } else {
            templateScope_->insert(name, tmpl);
        }
    }
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
    const template_ptr_t &tmpl, const GCT::node_ptr_t &origin, const std::string &why) {
    diags_->of(SemanticDiag::GenericFunctionAsValue)
        .atOrigin(origin->origin())
        .commit(tmpl->name, why, tmpl->name + ": " + tmpl->funcType->toString());
    throw BuildAbortException();
}

compile_graph_ptr_t Builder::instantiateGenericAs(
    const template_ptr_t &tmpl, FunctionType *expected, const GCT::node_ptr_t &origin,
    std::string &why) {
    FunctionType *signature = tmpl->funcType;
    if (signature->hasRest()) {
        why = "a rest function is expanded at each call and has no function value";
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
    compile_graph_ptr_t graph = instantiateGeneric(tmpl, bindings, origin);
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

node_handle_t
Builder::genericValueFor(node_handle_t node, Type *expected, const GCT::node_ptr_t &origin) {
    const auto generic = genericDrefs_.find(node);
    ASSERT(generic != genericDrefs_.end(), "Not the name of a generic function.");
    const template_ptr_t tmpl = generic->second;
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
        std::vector<std::string> names;
        collectTypeVars(tmpl->funcType, names);
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

compile_graph_ptr_t Builder::resolveGenericCall(
    const template_ptr_t &tmpl, std::vector<Argument> &withInputs,
    std::vector<Argument> &normInputs, type_vec_t &withTypes, type_vec_t &normTypes,
    const GCT::node_ptr_t &call, Type *expectedResult) {
    FunctionType *signature = tmpl->funcType;
    const size_t withCount  = signature->withTypesCount();
    const size_t normCount  = signature->normTypesCount();
    const auto inputAt      = [&](size_t i) -> Argument      &{
        return i < withCount ? withInputs[i] : normInputs[i - withCount];
    };
    const auto typeAt = [&](size_t i) -> Type *& {
        return i < withCount ? withTypes[i] : normTypes[i - withCount];
    };
    // The arguments that name a generic function themselves: they have no type yet.
    std::vector<template_ptr_t> waiting(withCount + normCount);
    const auto typesText = [&](const type_vec_t &types, size_t offset) {
        std::string text;
        for (size_t i = 0; i < types.size(); ++i) {
            const template_ptr_t &generic =
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
        if (withTypes.empty() && normCount > 0 && withCount + normCount == normTypes.size() &&
            signature->argNamesCount() >= withCount + normCount) {
            const auto names = [&](size_t from, size_t count) {
                std::string out;
                for (size_t i = 0; i < count; ++i) {
                    out += (i == 0 ? "" : ", ") + std::string(signature->argNameAt(from + i));
                }
                return out;
            };
            const std::string withs = names(0, withCount), norms = names(withCount, normCount);
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
    if (normCount != normTypes.size()) {
        reject(
            std::format("it takes {} argument(s) and {} were given", normCount, normTypes.size()));
    }
    for (size_t i = 0; i < withCount + normCount; ++i) {
        if (auto generic = genericDrefs_.find(inputAt(i).node); generic != genericDrefs_.end()) {
            waiting[i] = generic->second;
        }
    }
    // A parameter by its position among all parameters (`with` ones first).
    const auto patternAt = [&](size_t i) {
        return i < withCount ? signature->withTypeAt(i) : signature->normTypeAt(i - withCount);
    };
    const auto actualAt = [&](size_t i) { return valueTypeOf(typeAt(i)); };
    const auto describe = [&](size_t i) {
        const std::string paramName =
            i < signature->argNamesCount() ? std::string(signature->argNameAt(i)) : "?";
        return std::format(
            "{} {} {}, but its parameter '{}: {}'",
            i < withCount ? "with-argument" : "argument",
            (i < withCount ? i : i - withCount) + 1,
            waiting[i] ? "is the generic function '" + waiting[i]->name + "'"
                       : "has type '" + actualAt(i)->toString() + "'",
            paramName,
            patternAt(i)->toString());
    };
    // 1. Each variable takes the type found at its `typeas` position. An argument that is
    // not of the form of a `T?` / `T!` parameter enters it (design principles 3.8), so it
    // binds the variables of the payload: `or_zero(5, 0)` with `x: (typeas T)?` gives
    // `T = int`. `null` binds nothing.
    type_bindings_t bindings;
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
    for (size_t i = 0; i < withCount + normCount; ++i) {
        if (!waiting[i] && !bindEntering(patternAt(i), actualAt(i))) {
            reject(describe(i) + " needs a type of that form to bind its type variables");
        }
    }
    // 2. A variable that its `typeas` position left open (the argument there is `null`, or a
    // generic function that is still waiting) takes the type found at any other occurrence:
    // the constraints of all arguments are collected before any is held to its parameter.
    for (size_t i = 0; i < withCount + normCount; ++i) {
        if (!waiting[i]) {
            unifyTypeVars(patternAt(i), actualAt(i), bindings);
        }
    }
    if (expectedResult && signature->hasExitType()) {
        unifyTypeVars(signature->exitType(), expectedResult, bindings);
    }
    // 3. The waiting arguments, as long as one of them can be given its types: the parameter
    // it is passed to, with what is known so far, is the function it has to be; its instance
    // then says what is still open in that parameter (its return type).
    std::vector<std::string> reasons(waiting.size());
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
        // A composite literal enters the parameter's type as any literal does (design
        // principles 5.1): with `T` fixed to `f32` by another argument, `[1.0]` in the `T[]`
        // parameter is an `f32[]`, its elements converted once from the values written. The
        // conversion is the one an ordinary call makes (enterParameters); a literal that does
        // not fit is refused at its element, and one that does not fit at all is described
        // below.
        if (compositeLiteralEntersIn(inputAt(i).node, wanted, inputAt(i).expr)) {
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
    return instantiateGeneric(tmpl, bindings, call);
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
        const template_ptr_t tmpl = genericDrefs_.find(arg.node)->second;
        FunctionType *signature   = tmpl->funcType;
        const size_t params       = signature->normTypesCount();
        std::vector<std::string> names;
        collectTypeVars(signature, names);
        std::string tried;
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
            diags_->of(SemanticDiag::ComprehensionSourceRefused)
                .atOrigin(call->origin())
                .commit(source != nullptr ? source->toString() : std::string("void"));
            throw BuildAbortException();
        }
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
    const template_ptr_t &tmpl, const type_bindings_t &bindings, const GCT::node_ptr_t &call) {
    ExpectedValueScope instanceScope(expectedValueType_, nullptr);
    // All routes (inferred calls, function values and ::<T>) enforce the same type rules.
    Type *concrete = substituteTypeVars(tmpl->funcType, bindings);
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
    // One instance per set of types in the whole program, whichever module calls it: the
    // instance is a graph under the graph the function was declared in (the root graph for a
    // function of a module's top level), and its body resolves its names, and captures the
    // values of the top level, of the module that declares it. An instance whose body is
    // still being built is found here too: that is a recursive call.
    const bool foreign = tmpl->module != module_;
    for (const GenericInstance &instance : tmpl->instances) {
        if (sameBindings(instance.bindings)) {
            if (instance.failed) {
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
    const std::string instanceName = std::format(
        "{}<{}>",
        tmpl->name,
        strutil::join(bindings, ", ", [](const std::pair<std::string, Type *> &b) {
            return b.second->toString();
        }));
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
                  typeBindingsToString(bindings)),
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
        bool waited, synced, varied, declaresVariable, calleeAllowed;
        node_handle_t lastSynced;
        camel::core::module::module_ptr_t module;
        std::vector<std::string> namespacePath;
        std::vector<const std::vector<Reference> *> usingFrames;
        GenericTemplate &opened;
        BuildState(Builder &b, GenericTemplate &t)
            : self(b), graph(b.currGraph_), nodeScope(b.nodeScope_), graphScope(b.graphScope_),
              decoratedScope(b.decoratedScope_), templateScope(b.templateScope_),
              bindings(b.typeBindings_), waited(b.waited_), synced(b.synced_), varied(b.varied_),
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
            self.waited_               = waited;
            self.synced_               = synced;
            self.varied_               = varied;
            self.declaresVariable_     = declaresVariable;
            self.genericCalleeAllowed_ = calleeAllowed;
            self.lastSyncedNode_       = lastSynced;
            --self.instantiationDepth_;
        }
    };

    const size_t instanceIndex = tmpl->instances.size();
    const size_t diagsBefore   = diags_->count();
    const size_t errorsBefore  = diags_->count(Severity::Error);
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
        waited_               = false;
        synced_               = false;
        varied_               = false;
        declaresVariable_     = false;
        genericCalleeAllowed_ = false;
        lastSyncedNode_       = {};

        FunctionType *funcType = instantiateFunctionType(tmpl->funcType, bindings);
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
        tmpl->instances.push_back({bindings, graph, false});
        // Scopes are kept by name under the scope they were entered from, here the scope the
        // function was declared in: two functions written in place, which have no name to
        // tell them apart, need two scopes; so do two generic overloads of one operator,
        // which share their name.
        const bool sharedName =
            tmpl->name.empty() || camel::core::isOverloadableOperatorGroup(tmpl->func->name());
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
    } catch (...) {
        if (instanceIndex < tmpl->instances.size()) {
            tmpl->instances[instanceIndex].failed = true;
        }
        if (foreign) {
            diags_->locateByOriginFrom(diagsBefore);
        }
        diags_->addRelatedToErrorsFrom(diagsBefore, instanceLabel, 3);
        throw;
    }
    // A statement of the body that fails is skipped so that the rest is still checked; the
    // instance is unusable all the same.
    if (diags_->count(Severity::Error) > errorsBefore) {
        tmpl->instances[instanceIndex].failed = true;
        if (foreign) {
            diags_->locateByOriginFrom(diagsBefore);
        }
        diags_->addRelatedToErrorsFrom(diagsBefore, instanceLabel, 3);
        throw BuildAbortException();
    }
    return graph;
}

Type *Builder::visitTypeNode(const GCT::node_ptr_t &gct) {
    ENTER("TYPE");
    Type *type = boundType(gct->dataType());
    LEAVE("TYPE");
    return type;
}

} // namespace camel::compile::gir
