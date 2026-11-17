// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The builder's shared vocabulary: the handle and scope aliases every part of the GIR builder
 * names, what a generic declaration and an instance of it are, and what a `when` condition says.
 * These are types, not the builder, so they are declared on their own: `builder.h` includes this
 * and declares `Builder` itself.
 */

#pragma once

#include <any>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include "camel/common/ref.h"
#include "camel/common/scope.h"
#include "camel/core/context/context.h"
#include "camel/core/error/abort.h"
#include "camel/core/module/module.h"
// The payload of a module entity: a generic function of another module arrives as it was
// exported (genericTemplatesOfEntity).
#include "core/module/entity_internal.h"

#include "camel/compile/gct.h"
#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/rule_templates.h"
#include "camel/compile/gir/value_entity.h"
#include "camel/compile/semantic_recorder.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/node_attrs.h"
#include "camel/core/tags.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/resolver.h"
#include "camel/core/type/resource_conflict.h"
#include "camel/core/type/typevar.h"
#include "ssa_env.h"

namespace camel::compile::gir {
using void_ptr_t   = void *;
using Type         = camel::core::type::Type;
using FunctionType = camel::core::type::FunctionType;
using data_ptr_t   = camel::core::data::data_ptr_t;

using compile_graph_ptr_t   = std::shared_ptr<DraftGraphBuilder>;
using node_handle_t         = camel::runtime::DraftNodeRef;
using graph_set_t           = std::vector<compile_graph_ptr_t>;
using graph_set_ptr_t       = std::shared_ptr<graph_set_t>;
using graph_scope_t         = Scope<std::string, graph_set_ptr_t>;
using graph_scope_ptr_t     = std::shared_ptr<graph_scope_t>;
using decorated_scope_t     = Scope<std::string, compile_graph_ptr_t>;
using decorated_scope_ptr_t = std::shared_ptr<decorated_scope_t>;

class Builder;

struct GenericTemplate;
struct ConstasValue;

/// A `constas` parameter of a generic declaration and the compile-time value a call bound to
/// it (design principles 5.4): the value selects the instance and the body reads it as a static
/// value. It is not a runtime argument, so the instance has no port for it.
struct ConstasValue {
    Type *type      = nullptr; // the declared type of the parameter, the other extents substituted
    Type *valueType = nullptr; // the type of the constant the call wrote
    data_ptr_t data;           // the constant itself, the instance key's value
    node_handle_t node = {};   // the node the call gave it as
};
using ConstasValues = std::map<std::string, ConstasValue>;

/// One set of types a generic function was instantiated at, and the graph built for it.
struct GenericInstance {
    camel::core::type::type_bindings_t bindings;
    // The compile-time values bound to the `constas` parameters of the declaration, and the
    // integer ones among them as the extent bindings they stand for.
    ConstasValues constas;
    camel::core::type::ExtentValues extents;
    compile_graph_ptr_t graph;
    // An error was found in the body: later calls with the same types fail quietly.
    bool failed = false;
};

/// The generic functions of one name declared in one block. They are told apart by their
/// parameter types, exactly as ordinary overloads are (design principles 4.4): a call takes the
/// one whose parameters accept its arguments. One name is one template until a second generic
/// function is declared beside it, and then one group, whose first template is the one an
/// unqualified read of the name means.
using generic_template_ptr_t = std::shared_ptr<GenericTemplate>;
using GenericGroup           = std::vector<generic_template_ptr_t>;
using generic_group_ptr_t    = std::shared_ptr<GenericGroup>;
/// What the scope of a name and a generic DREF holds: one template, or a group of overloads.
/// `->` answers the first of the group, which is the template a name with one overload is.
class generic_ref_t {
  public:
    generic_ref_t() = default;
    /// Defined where GenericTemplate is complete: it reads the template's name.
    generic_ref_t(generic_template_ptr_t one);
    explicit generic_ref_t(generic_group_ptr_t group) : group_(std::move(group)) {}
    explicit operator bool() const { return group_ != nullptr && !group_->empty(); }
    const generic_group_ptr_t &group() const { return group_; }
    /// The template a name with one overload stands for, and the first of a group.
    generic_template_ptr_t one() const {
        return group_ != nullptr && !group_->empty() ? group_->front() : nullptr;
    }
    generic_template_ptr_t operator->() const { return one(); }

  private:
    generic_group_ptr_t group_;
};
/// The names of the generic functions in force where a template was declared: one entry per
/// name, holding the one generic function of that name or the group of its overloads.
using template_scope_t     = Scope<std::string, generic_ref_t>;
using template_scope_ptr_t = std::shared_ptr<template_scope_t>;

/// A generic declaration: the function that still holds type variables, and everything an
/// instance of it needs. The instance key is the bindings with the `constas` values, and the
/// instance is built in the module that declared the function, not at the call.
struct GenericTemplate {
    std::string name;
    GCT::node_ptr_t func = nullptr;
    // The declared type with the variables of enclosing instances replaced; the function's
    // own variables remain.
    FunctionType *funcType = nullptr;
    // The type parameters the declaration put at its head, in order, whether or not the
    // signature writes them; an explicit `::<...>` binds them by position or by name.
    std::vector<std::string> headParams;
    // The bindings in force where the template was declared (a generic function declared
    // inside an instance of another one).
    camel::core::type::type_bindings_t outerBindings;
    // Where the function was declared: instances are built there, not at the call. There is
    // one instance per set of types in the whole program, whichever module calls it.
    compile_graph_ptr_t ownerGraph;
    // The module that declares the function: the names of the body are resolved there.
    camel::core::module::module_ptr_t module;
    node_scope_ptr_t nodeScope;
    graph_scope_ptr_t graphScope;
    decorated_scope_ptr_t decoratedScope;
    template_scope_ptr_t templateScope;
    // The node attributes in force where it was declared (currentAttrs_).
    camel::core::attr::AttrSetId attrs = camel::core::attr::kNoAttrs;
    // The namespace and the `using namespace` in force where it was declared: its body
    // resolves its names as if it were built there.
    std::vector<std::string> namespacePath;
    std::vector<const std::vector<Reference> *> usingFrames;
    std::vector<GenericInstance> instances;
    // Instances whose bodies are being built right now, one inside another.
    size_t openInstances = 0;
};

inline generic_ref_t::generic_ref_t(generic_template_ptr_t one)
    : group_(std::make_shared<GenericGroup>()) {
    if (one != nullptr) {
        group_->push_back(std::move(one));
    }
}
/// The group a scope entry holds, in one expression: the entry is always a group.
inline GenericGroup *genericGroupOf(const generic_ref_t &ref) { return ref.group().get(); }

/// The generic functions an imported name brings: the payload of the module entity is the list of
/// the templates of that name, one of them for a name declared once and all of them when the name
/// is declared as several generic overloads (`makeGenericTemplates`). A call of the name picks
/// among them exactly as it does for a name of this module (design principles 4.4, 5.5).
inline std::vector<generic_template_ptr_t>
genericTemplatesOfEntity(const camel::core::module::Entity &entity) {
    if (!camel::core::module::detail::EntityAccess::isGenericTemplate(entity)) {
        return {};
    }
    const auto &tmpls = camel::core::module::detail::EntityAccess::genericTemplates(entity);
    std::vector<generic_template_ptr_t> templates;
    templates.reserve(tmpls.size());
    for (const std::shared_ptr<void> &one : tmpls) {
        templates.push_back(std::static_pointer_cast<GenericTemplate>(one));
    }
    return templates;
}

/// The payload a module exports for a name with generic functions: every template of the group,
/// so that an importer selects the overload a call takes.
inline std::vector<std::shared_ptr<void>> genericTemplatePayloadOf(const generic_ref_t &ref) {
    std::vector<std::shared_ptr<void>> payload;
    if (GenericGroup *group = genericGroupOf(ref); group != nullptr) {
        payload.reserve(group->size());
        for (const generic_template_ptr_t &tmpl : *group) {
            payload.push_back(std::static_pointer_cast<void>(tmpl));
        }
    }
    return payload;
}

/// What a declaration's `when` condition says at one instantiation (design principles 4.4).
/// The condition is a compile-time predicate over the declaration's type parameters and `with`
/// parameters; it is evaluated when the declaration is instantiated or selected, never when it
/// is parsed. A condition the compiler cannot answer is never taken for `true`.
enum class WhenOutcome {
    Holds,           // it is true
    Fails,           // it is false: the declaration is not applicable here
    NeedsWithValue,  // it reads a `with` parameter whose value the call did not give
    NotBool,         // its value is not a bool
    NotEvaluable,    // it is not a compile-time predicate (a runtime value, a user call, ...)
    TypeNotConcrete, // a type query still sees a type variable
    UnknownName,     // a name outside the declaration (an ordinary parameter, a body name, ...)
    // A predicate call the condition cannot make, though the name exists.
    NotImported,        // declared by a module this one does not import
    NotCompileTime,     // imported, but not a generic function answered at compile time
    PredicateAmbiguous, // two imports bring predicates of that name, and neither is chosen
};

struct WhenResult {
    WhenOutcome outcome = WhenOutcome::NotEvaluable;
    /// The name, type or expression the outcome is about, for the report.
    std::string subject;
    /// A sentence saying what is wrong or missing, for the report.
    std::string detail;
    bool holds() const { return outcome == WhenOutcome::Holds; }
    bool fails() const { return outcome == WhenOutcome::Fails; }
};

/// The value of a `constas` constant as the key it stands for when it names a field or a
/// position (`t.[n]`): the text of a string, the digits of an integer, `true` / `false`.
std::string constasKeyOf(const camel::core::data::data_ptr_t &data);
/// The value as an instance name shows it, strings quoted: `f(3)` is `f<3>`, `f("w")` is
/// `f<"w">`.
std::string constasTextOf(const camel::core::data::data_ptr_t &data);

/// Whether a value of this type is read once on each execution path: a stream is (design
/// principles 5.4), and so is the step function of a stream -- a function taking nothing whose
/// answer carries one, which is how a stream's value is spelled. A tuple or a struct that holds a
/// stream is not a value of this kind: reading two of its members reads two members, not the
/// stream twice. Used where a capture is made (a captured stream is refused) and by the check that
/// counts the reads of the built graphs, so both read one rule.
bool streamReadOnce(const camel::core::type::Type *type);

} // namespace camel::compile::gir
