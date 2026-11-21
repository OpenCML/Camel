// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The layout of a body for direct SSA construction: which names stay cells and which become
 * values (scanBodyForSsa), the answer a call gives for a name and the values it hands back
 * (calleeValueOf), the conditions a declaration and a name are read by (argumentMayWriteBack,
 * ssaDeclarationIsEligible, nameIsSsa), and the name a tag binds (bindTemporary).
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"
#include "camel/core/trace.h"
#include <format>

#include "camel/compile/gir/static_function.h"
#include "camel/core/data/composite/struct.h"
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
#include "camel/core/type/ref.h"

#include <atomic>
#include <cctype>
#include <stdexcept>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {
std::string Builder::bindTemporary(node_handle_t node) {
    std::string name = "$tagged" + std::to_string(tagTemporaries_++);
    insertNode(name, node);
    return name;
}

// ---------------------------------------------------------------------------------------------
// The value environment of a `var` built as SSA
//  A `var` whose every
// read and every write lies in the region that
// declares it is built as a value: the name is bound to the value in `nodeScope_`, and an
// assignment rebinds it (`ssaAssignmentOf`). Nothing reads or writes a cell, so nothing counts
// units and nothing has to be folded afterwards.
// ---------------------------------------------------------------------------------------------

namespace {
/// Whether a node opens a region of the graph: an `if`/`match` arm, a loop's body, a closure's
/// body, a block that carries tags, a static loop. A `var` read or written across one of them is
/// carried into it -- a join's φ or a loop head's port -- and where there is neither, such a name
/// keeps its cell.
bool opensRegion(GCT::NodeKind kind) {
    switch (kind) {
    case GCT::NodeKind::CASE:
    case GCT::NodeKind::EXEC:
    case GCT::NodeKind::LOOP:
    case GCT::NodeKind::FUNC:
    case GCT::NodeKind::TMTCH:
    case GCT::NodeKind::TCASE:
    case GCT::NodeKind::SFOR:
    case GCT::NodeKind::SLOOP:
    case GCT::NodeKind::SACC:
    case GCT::NodeKind::SCOMP:
        return true;
    default:
        return false;
    }
}

/// What a region does with a `var` that is read and written from outside it: an arm hands the value
/// to the branch's JOIN, a loop body hands it to the loop head's port, and
/// every other region -- a closure's body, a block of its own, a static loop's body, a loop's
/// condition -- has nowhere to put it, so a name used across one of those keeps its cell.
enum class SsaRegion : uint8_t { Other, Arm, LoopBody };

/// One region the walk below is inside: the depth of the body it holds, and what it can carry.
struct OpenRegion {
    size_t contentDepth = 0;
    SsaRegion kind      = SsaRegion::Other;
};

/// The scan's walk: a node, the region depth it stands at, the call it is an argument of (null when
/// it is not one of a call's arguments) and that argument's index, whether it stands in a statement,
/// whether it is a loop's body, and whether it is written -- an assignment's target -- or read.
using SsaWalk =
    std::function<void(const GCT::Node *, size_t, const GCT::Node *, size_t, bool, bool, bool)>;
} // namespace

std::unordered_map<const GCT::Node *, Builder::SsaAnswer> Builder::scanBodyForSsa(
    const GCT::Node *body) {
    // One walk of the body, before any of it is built: every `var` declaration in it, and whether
    // the declaration may be built as a value. The answer is about the declaration and nothing
    // else, so two builds of the same body decide the same way. A name belongs to the declaration
    // the innermost scope binds it to, and a use rules the declaration out when it stands in
    // another region (the value would have to be carried into it) or at a `var` parameter (the
    // callee writes the caller's cell back, and a value has nowhere to write).
    std::unordered_map<const GCT::Node *, SsaAnswer> eligible;
    std::unordered_map<const GCT::Node *, size_t> declaredIn;
    // The regions the walk is inside, outermost first: the chain between a declaration and a use is
    // what says whether the value can be carried (a branch's JOIN or a loop head), and the
    // innermost arm decides which join's one-value limit a write counts against.
    std::vector<OpenRegion> regions;
    std::map<size_t, std::set<const GCT::Node *>> branchWrites;
    std::vector<std::unordered_map<std::string, const GCT::Node *>> scopes(1);
    const auto lookup = [&](const std::string &name) -> const GCT::Node * {
        for (size_t i = scopes.size(); i-- > 0;) {
            const auto found = scopes[i].find(name);
            if (found != scopes[i].end()) {
                return found->second; // null for a `let`, a `catch` binding or a parameter
            }
        }
        return nullptr;
    };
    const auto use = [&](const GCT::Node *decl,
                         size_t depth,
                         const GCT::Node *call,
                         size_t index,
                         bool written) {
        SsaAnswer &answer = eligible[decl];
        if (declaredIn[decl] != depth) {
            // Across a region the value has to be carried into it. The regions that carry one are
            // the arm of a statement branch, whose JOIN carries it, and a loop body,
            // whose head carries it; every region between the declaration and the use
            // has to be one of those, one inside the other, and nothing else. A closure body, a
            // block of its own, a static loop's body and a loop's condition have nowhere to put the
            // value, so a name used across one of them keeps its cell.
            const size_t declared   = declaredIn[decl];
            bool carried            = depth > declared;
            bool carriedAsAHandle   = depth > declared;
            for (const OpenRegion &region : regions) {
                if (region.contentDepth <= declared) {
                    continue;
                }
                if (region.kind == SsaRegion::Other) {
                    carried = false;
                    // A read of a handle across such a region is not a use that has to be carried:
                    // the closure captures the value, which is what a handle is (design principles
                    // 3.5). A write is: it has to land
                    // somewhere the closure and the body share, and a value is not such a place.
                    if (written) {
                        carriedAsAHandle = false;
                    }
                }
            }
            if (!carried) {
                answer.asValue = false;
            }
            if (!carriedAsAHandle) {
                answer.asHandle = false;
            }
            if (carried && !regions.empty() && regions.back().kind == SsaRegion::Arm) {
                // One join carries one value: a branch whose arms write two names carries neither,
                // settled after the walk. A loop head carries as many values as its body writes, so
                // a write inside a loop body is not counted here.
                branchWrites[depth].insert(decl);
            }
        }
        if (call != nullptr && argumentMayWriteBack(call, index, scopes)) {
            answer.asValue  = false; // written by the callee through its `var` parameter
            answer.asHandle = false;
        }
    };
    SsaWalk walk = [&](const GCT::Node *node,
            size_t depth,
            const GCT::Node *call,
            size_t index,
            bool statement,
            bool loopBody,
            bool written) {
            if (node == nullptr || node->type() == GCT::NodeKind::TYPE) {
                return;
            }
            if (node->type() == GCT::NodeKind::DREF) {
                if (const GCT::Node *decl = lookup(node->name())) {
                    use(decl, depth, call, index, written);
                }
                return;
            }
            const bool declares =
                node->type() == GCT::NodeKind::VARI || node->type() == GCT::NodeKind::NREF;
            // The declaration's own region is the one it is written in, so a `var` in an arm is at
            // the arm's depth and a use of it in an enclosing block is across a region.
            const bool region  = node != body && opensRegion(node->type());
            const size_t inner = declares ? depth : (region ? depth + 1 : depth);
            // The arms of a statement branch are a depth whose writes are carried, and a loop
            // body is another. A branch whose value is used fills the join's only value slot
            // with its own value, so its arms are not carried -- the red line, settled before any
            // carry is written.
            const bool statementBranch = statement && (node->type() == GCT::NodeKind::CASE ||
                                                       node->type() == GCT::NodeKind::TCASE);
            // Anything written directly in a region body is a statement of that body.
            const bool childStatement = region || node == body;
            if (region) {
                scopes.emplace_back();
                // A loop node is a region of the walk like its body is: what the head carries is
                // read in the body and in the loop's own initial values, and the head hands it out
                // after the loop. Its condition is a region of its own inside it and carries
                // nothing ( a name used in a loop's condition keeps its cell).
                const bool carries = loopBody || node->type() == GCT::NodeKind::LOOP;
                regions.push_back(
                    OpenRegion{
                        inner,
                        statementBranch ? SsaRegion::Arm
                                        : (carries ? SsaRegion::LoopBody : SsaRegion::Other)});
            }
            if (declares) {
                // `var x = e` is a `VARI` over the one name it declares. Any other shape (the names
                // of a destructuring declaration) is left to the declarations below, which bind
                // each of those names as a name this scan does not build as a value.
                const GCT::Node *nameNode = node;
                const bool single         = node->type() == GCT::NodeKind::NREF ||
                                            (node->size() == 1 && node->at(0) != nullptr &&
                                             node->at(0)->type() == GCT::NodeKind::NREF);
                if (!single) {
                    for (size_t i = 0; i < node->size(); ++i) {
                        walk(node->at(i), inner, nullptr, 0, childStatement, false, false);
                    }
                    if (region) {
                        scopes.pop_back();
                        regions.pop_back();
                    }
                    return;
                }
                if (node->type() == GCT::NodeKind::VARI) {
                    nameNode = node->at(0);
                }
                // The initializer is read before the name stands for anything.
                const GCT::Node *initializer = nameNode->size() > 0 ? nameNode->at(0) : nullptr;
                // A `var` whose initializer is a type (`var x: T = ...`, or a cell whose value is
                // written where it stands) is the cell itself, not a value over it.
                const bool typed =
                    initializer != nullptr && initializer->type() == GCT::NodeKind::TYPE;
                if (!typed && initializer != nullptr) {
                    walk(initializer, depth, nullptr, 0, false, false, false);
                }
                if (node->type() == GCT::NodeKind::VARI && !typed) {
                    eligible[nameNode]   = SsaAnswer{};
                    declaredIn[nameNode] = depth;
                }
                scopes.back()[nameNode->name()] =
                    node->type() == GCT::NodeKind::VARI && !typed ? nameNode : nullptr;
                if (region) {
                    scopes.pop_back();
                    regions.pop_back();
                }
                return;
            }
            const bool isCall = node->type() == GCT::NodeKind::LINK ||
                                node->type() == GCT::NodeKind::WITH ||
                                node->type() == GCT::NodeKind::TAPP;
            // A `var` an in-place operator rewrites keeps its cell: the
            // update takes the value's storage and hands it back by rebinding the name, which needs
            // a cell to write. Two shapes, both read off the GCT node: the plain write of a place
            // (`v[i] = e`, `v.f = e`) and the write of a call over the variable itself.
            //   ASSIGN(Plain, <path>, e)      `v[i] = e`, `v.f = e`
            //   ASSIGN(Plain, v, LINK(method, v, ...))
            //                                 `v = v->append(x)`, `v = append(v, x)`
            // The operator form `v = v + 1` (a `__`-prefixed callee) builds a new value and is not
            // one of these, so it stays a value. A shape this misses only keeps a cell, which is
            // the cheap direction; the other way round drops an in-place update, which `--copies`
            // sees.
            if (node->type() == GCT::NodeKind::ASSIGN &&
                node->assignmentForm() == GCT::AssignmentForm::Plain) {
                const GCT::Node *target = node->assignmentTarget();
                const bool plainTarget = target != nullptr && target->type() == GCT::NodeKind::DREF;
                if (!plainTarget) {
                    std::function<void(const GCT::Node *)> keepCell = [&](const GCT::Node *place) {
                        if (place == nullptr) {
                            return;
                        }
                        if (place->type() == GCT::NodeKind::DREF) {
                            if (const GCT::Node *decl = lookup(place->name())) {
                                eligible[decl].asValue  = false;
                                eligible[decl].asHandle = false;
                            }
                        }
                        for (size_t i = 0; i < place->size(); ++i) {
                            keepCell(place->at(i));
                        }
                    };
                    keepCell(target);
                }
                // A self-referential write through a method (`v = v->append(x)`) is deliberately
                // NOT excluded here, and that is measured, not an omission: making such a name a
                // cell lets the in-place form change its storage *and* the assignment hand out a
                // value, so the append happens twice -- `new_values.macro_append.{nvm,fvm,macro}`
                // printed `[0, 1, 25, 3]` where the program says `[0, 1, 25, 2]`. Keeping the cell
                // is not "putting it back where it was"; it is a change of semantics. So the name
                // stays a value and its update is copied (`--copies` says so at
                // `element_assigned_append`).
            }
            for (size_t i = 0; i < node->size(); ++i) {
                // A loop's body is the region its head carries values around: the walk says
                // so for that child and for nothing else -- a loop's condition, initial values and
                // sequence stand where the loop is written.
                const bool childLoopBody =
                    node->type() == GCT::NodeKind::LOOP && node->loopBody() == node->at(i);
                // The target of an assignment is written, and a write is what a region that carries
                // nothing cannot hold (see `use`). A place's write (`v[i] = e`) keeps its cell by
                // the answer above; what this says is that the name itself is written, so a read
                // across a closure is the only use a handle's cell does not have to be kept for.
                const bool childWritten =
                    node->type() == GCT::NodeKind::ASSIGN &&
                    node->assignmentForm() != GCT::AssignmentForm::Rebuilt &&
                    node->assignmentTarget() == node->at(i);
                if (isCall) {
                    walk(node->at(i), inner, node, i, childStatement, childLoopBody, childWritten);
                } else {
                    walk(node->at(i), inner, nullptr, 0, childStatement, childLoopBody, childWritten);
                }
            }
            if (region) {
                scopes.pop_back();
                regions.pop_back();
            }
        };
    walk(body, 0, nullptr, 0, false, false, false);
    // A branch that writes two declarations carries neither: its JOIN has one value slot. The
    // census over the corpus found that shape only in a generator's carried state, which a capture
    // keeps a cell anyway; if a plain branch ever writes two names, the join has to grow the tuple form.
    for (const auto &[depth, names] : branchWrites) {
        if (names.size() >= 2) {
            for (const GCT::Node *decl : names) {
                eligible[decl].asValue  = false;
                eligible[decl].asHandle = false;
            }
        }
    }
    return eligible;
}

Builder::CalleeValue Builder::calleeValueOf(const GCT::Node *callee) {
    CalleeValue value;
    if (callee == nullptr) {
        return value;
    }
    // An operator the compiler made for this node (`:op/assn_i`, `:enum/field`): the id is what
    // names it, and its overloads are a group rather than a function value. Whether such a group
    // writes an argument back is the group's own business -- for the assignment it is the rebinding
    // the builder writes (`ssaAssignmentOf`), not a `var` parameter of a call -- so it is not a
    // write-back this scan counts.
    if (GCT::Tree::of(callee)->operRefOf(callee).has_value()) {
        value.kind = CalleeValueKind::Operator;
        return value;
    }
    const std::string &written = callee->name();
    // A name the compiler wrote for a built-in operator it means itself (`core::compilerNamedOperator`:
    // the `filter` and `map` of a comprehension, the `range` an interval is). The built-in module is
    // where the build resolves it (visitDRefNode), and what it names is a group, so the answer is the
    // same as for any other operator.
    if (written.starts_with(kBuiltinPrefix)) {
        if (context_ != nullptr) {
            const auto entity =
                context_->importModule("")->getNativeEntity(Reference(written.substr(
                    kBuiltinPrefix.size())));
            if (entity.has_value() && entity->isOperGroup()) {
                value.kind = CalleeValueKind::Operator;
            }
        }
        return value;
    }
    // This module's own functions: their graphs exist from the moment the module is declared, and
    // the signature each carries is the one its declaration wrote (design principles 10.2, layers
    // 1 and 2; a name this body binds was answered before this is asked).
    if (const ModuleUnit *unit = currentModuleUnit(); unit != nullptr) {
        if (auto graphs = unit->graphScope->get(moduleLevelName(written), false);
            graphs.has_value()) {
            for (const auto &graph : **graphs) {
                if (graph != nullptr) {
                    value.signatures.push_back(graph->funcType());
                }
            }
            if (!value.signatures.empty()) {
                value.kind = CalleeValueKind::Function;
                return value;
            }
        }
    }
    // The signatures the values a name can stand for carry, in one place: the graphs of the
    // functions declared under it, and the template of a generic one (its declared type is what
    // the name stands for until a call instantiates it). A decorated name is deliberately left
    // out: what it stands for is the decorated graph, whose type is not the declaration's, and a
    // call written as a `var` argument reads the declaration's marks -- reading the decorated
    // type here would turn a cell into a value.
    const auto collectSignatures = [&value](
                                       const graph_scope_ptr_t &graphs,
                                       const template_scope_ptr_t &templates,
                                       const std::string &name) -> bool {
        value.signatures.clear();
        if (graphs != nullptr) {
            if (auto found = graphs->get(name, false); found.has_value()) {
                for (const auto &graph : **found) {
                    if (graph != nullptr) {
                        value.signatures.push_back(graph->funcType());
                    }
                }
            }
        }
        if (value.signatures.empty() && templates != nullptr) {
            if (auto found = templates->get(name);
                found.has_value() && static_cast<bool>(found.value())) {
                // Every overload of the name is a template of its own; the union of their
                // declared types is what the name stands for until a call instantiates one.
                for (const auto &tmpl : *found.value().group()) {
                    if (tmpl == nullptr) {
                        continue;
                    }
                    const FunctionType *signature = tmpl->funcType;
                    if (signature == nullptr && tmpl->func != nullptr && tmpl->func->size() > 0 &&
                        tmpl->func->at(0) != nullptr) {
                        signature = tt::as_ptr<FunctionType>(tmpl->func->at(0)->dataType());
                    }
                    if (signature != nullptr) {
                        value.signatures.push_back(signature);
                    }
                }
            }
        }
        return !value.signatures.empty();
    };
    // The entity an import layer brings, read the same way.
    const auto collectFromEntity = [&value](const camel::core::module::entity &entity) -> bool {
        if (entity.isOperGroup()) {
            value.kind = CalleeValueKind::Operator;
            return true;
        }
        if (camel::core::module::detail::EntityAccess::isGraphSet(entity)) {
            for (const auto &graph : *asCompileGraphSet(
                     camel::core::module::detail::EntityAccess::graphSet(entity))) {
                if (graph != nullptr) {
                    value.signatures.push_back(graph->funcType());
                }
            }
            if (!value.signatures.empty()) {
                value.kind = CalleeValueKind::Function;
                return true;
            }
        }
        if (camel::core::module::detail::EntityAccess::isGenericTemplate(entity)) {
            for (const auto &tmpl : genericTemplatesOfEntity(entity)) {
                if (tmpl == nullptr) {
                    continue;
                }
                const FunctionType *signature = tmpl->funcType;
                if (signature == nullptr && tmpl->func != nullptr && tmpl->func->size() > 0 &&
                    tmpl->func->at(0) != nullptr) {
                    signature = tt::as_ptr<FunctionType>(tmpl->func->at(0)->dataType());
                }
                if (signature != nullptr) {
                    value.signatures.push_back(signature);
                }
            }
            if (!value.signatures.empty()) {
                value.kind = CalleeValueKind::Function;
                return true;
            }
        }
        return false; // a decorated one or an exported value: the type is not read here
    };
    const ModuleUnit *unit = currentModuleUnit();
    // This module's own functions, by the name the module declares them under (design principles
    // 10.2, layers 1 and 2; a name this body binds was answered before this is asked).
    if (unit != nullptr &&
        collectSignatures(
            unit->graphScope, unit->templateScope,
            moduleLevelName(written))) {
        value.kind = CalleeValueKind::Function;
        return value;
    }
    // A function declared in a body of this one, in the scopes between here and the module's top
    // level: those are functions the program made where it stands, and their declarations carry
    // the same types.
    if (unit != nullptr) {
        for (graph_scope_ptr_t scope = graphScope_; scope != nullptr && scope != unit->graphScope;
             scope                   = scope->outer()) {
            if (auto found = scope->get(written, false); found.has_value()) {
                for (const auto &graph : **found) {
                    if (graph != nullptr) {
                        value.signatures.push_back(graph->funcType());
                    }
                }
                if (!value.signatures.empty()) {
                    value.kind = CalleeValueKind::Function;
                    return value;
                }
            }
        }
    }
    // Layers 3 to 5, in the order visitDRefNode reads them: a name imported by name, then a name
    // a `using namespace` in force brings, then a spelling an import brings (operators take their
    // overloads from every layer), then the ambient ones.
    std::optional<camel::core::module::entity> entity;
    if (module_->hasImportedRef(written, ImportLayer::Named)) {
        entity = importedEntity(written, ImportLayer::Named);
    }
    if (!entity.has_value()) {
        if (auto match = throughUsing(written, const_cast<GCT::Node *>(callee));
            match.has_value()) {
            if (!match->ownName.empty()) {
                // A name of this module, opened by a `using namespace` of one of its namespaces.
                if (unit != nullptr &&
                    collectSignatures(
                        unit->graphScope, unit->templateScope,
                        match->ownName)) {
                    value.kind = CalleeValueKind::Function;
                    return value;
                }
            } else if (match->entity.has_value()) {
                if (collectFromEntity(*match->entity)) {
                    return value;
                }
            }
        }
    }
    if (!entity.has_value()) {
        if (isOperatorName(written) || module_->hasImportedRef(written)) {
            entity = importedEntity(written);
        } else if (module_->hasImportedRef(written, ImportLayer::Ambient)) {
            entity = importedEntity(written, ImportLayer::Ambient);
        }
    }
    if (!entity.has_value()) {
        return value;
    }
    collectFromEntity(*entity);
    return value;
}

bool Builder::argumentMayWriteBack(
    const GCT::Node *call, size_t index,
    const std::vector<std::unordered_map<std::string, const GCT::Node *>> &scopes) {
    // A `var` parameter is two-way: the callee reads the argument and writes into it, so the
    // argument cannot be a value. Which position that is, the callee's function type says -- the
    // `var` marks its parameters carry -- so the answer is read off the value the callee name
    // stands for and never off the spelling of the name.
    if (call == nullptr || index == 0) {
        return false; // the callee itself is named, not handed
    }
    if (call->type() != GCT::NodeKind::LINK && call->type() != GCT::NodeKind::WITH) {
        return false;
    }
    const bool byWith       = call->type() == GCT::NodeKind::WITH;
    const GCT::Node *callee = call->at(0);
    if (callee == nullptr) {
        return true; // nothing to name the callee by: answered as one that may write
    }
    if (callee->type() == GCT::NodeKind::TAPP && callee->size() > 0) {
        callee = callee->at(0); // `f::<T>(x)`: the function is still the first child
    }
    if (callee->type() == GCT::NodeKind::WITH && callee->size() > 0) {
        callee = callee->at(0); // `f<w>(x)`: the callee is under the `with` bindings
    }
    if (callee == nullptr || callee->type() != GCT::NodeKind::DREF) {
        return true; // a lambda written where the callee stands: its parameters are not read here
    }
    for (size_t i = scopes.size(); i-- > 0;) {
        if (scopes[i].contains(callee->name())) {
            return true; // a name this body binds: a function value whose parameters are unknown
        }
    }
    const CalleeValue value = calleeValueOf(callee);
    if (value.kind == CalleeValueKind::Unknown) {
        // Nothing of this program is named so in a way this scan can see, and this scan cannot say
        // what the callee reads and writes: answered as one that may write, because a write-back
        // lost to a value is a wrong program while a cell costs one unit.
        return true;
    }
    if (value.kind == CalleeValueKind::Operator) {
        return false;
    }
    const size_t at = index - 1;
    for (const FunctionType *signature : value.signatures) {
        if (signature == nullptr) {
            continue;
        }
        const size_t count = byWith ? signature->withTypesCount() : signature->normTypesCount();
        if (at < count && (byWith ? signature->withIsVarAt(at) : signature->normIsVarAt(at))) {
            return true;
        }
    }
    return false;
}

bool Builder::ssaDeclarationIsEligible(
    const GCT::node_ptr_t &decl, const std::string &name, bool aHandle) {
    // A module's top level is not a region of a body: its `var`s are cells every function of the
    // module reaches, and they stay cells. A declaration the scan never saw is not
    // one of the body's own, and is answered as a cell.
    if (regionBodies_.empty() || currGraph_ == rootGraph_ || decl == nullptr) {
        return false;
    }
    const GCT::Node *body = regionBodies_.back();
    auto scanned          = ssaScan_.find(body);
    if (scanned == ssaScan_.end()) {
        scanned = ssaScan_.emplace(body, scanBodyForSsa(body)).first;
    }
    const auto found = scanned->second.find(decl);
    if (found == scanned->second.end()) {
        return false;
    }
    return aHandle ? found->second.asHandle : found->second.asValue;
}

bool Builder::nameIsSsa(const std::string &name) const {
    return ssa_.find(nodeScope_, name) != nullptr;
}

} // namespace camel::compile::gir
