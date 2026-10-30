// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * std::defunctionalize: a function value is a tag with the environment it captured.
 *
 * The design gives a function value one form: a value of an `enum` type, one variant per function
 * the program takes as a value, and the values that function captured as that variant's data --
 * the tag is in the value's type, not beside it. This pass makes that form:
 *
 *  - every value node of a function type becomes a `:enum/make` of its family's enum: the tag of
 *    the function it names and the values the closure captured, in capture order;
 *  - every call through such a value becomes a direct call (a `FUNC` node) of that function. A
 *    closure's environment travels as the function's trailing arguments, so after the pass the
 *    captures of a function are its ordinary parameters and no closure is left in the program;
 *  - the tag is known at build time when the callee is a value this graph builds: the value it
 *    holds is that one function, so no match is built -- `:enum/field` takes the captures out of
 *    the value where the callee is a parameter and the family has one variant, and nothing is
 *    taken where the variant has no data;
 *  - a tag only known at run time is a match on it: `:enum/tag` reads the tag, one arm of a
 *    `BRCH` per variant takes that variant's captures out of the value and calls the function,
 *    and the `JOIN` hands back what the taken arm returned. The arms exclude each other, so the
 *    value is still the one the program wrote, and no call through a value is left.
 *
 * A *family* is one value's type: the signatures with the same parameters and the same result,
 * with the effect qualifiers left out (design principles 5.2: an effect qualifier is a subtype
 * relation, not another type, so `(x: int) => int pure` and `(x: int) => int` are one family).
 * One family is one enum with one variant per function; the effect information is not lost, it is
 * what each variant (the concrete function) is.
 *
 * It refuses what it cannot see whole, and says how many families it refused, with the reason for
 * each:
 *
 *  - a value read by a node that is not a call: a higher-order operator such as `map` calls the
 *    value inside the operator, where this pass cannot see the call, and the enum would be handed
 *    to something that expects a function;
 *  - a function that is called directly somewhere: its captures are not arguments of that call, so
 *    turning them into parameters would break it;
 *  - a call whose callee waits on something, a callee that is neither a value nor a parameter, a
 *    value handed to a parameter declared as another function type (a lambda that returns its write
 *    where the parameter says `void`, say), a type that is an argument of the entry function, or a
 *    type that holds another family left as written: the pass refuses what it cannot see rather
 *    than leaving half the program on the old type;
 *  - a type the program gives no value to: there is nothing to build.
 *
 * A family that is no value's business is left out of the count rather than refused: the entry
 * function's own value is the shell's, which builds it and calls it, so no program gets its entry
 * point re-encoded.
 *
 * The rewrite is one session, and a session whose commit would break an invariant leaves the program
 * as it was: a lowering step that cannot lower a program must not break it. Those are the programs
 * where a call the pass made direct meets an order edge the graph keeps on an effectful node, and
 * the rewritten node carries the enum, which has no resources, so the graph would be one no
 * scheduler may run (`test/cases/modules/autodiff` has two of them). That is a defect of this pass
 * and not of the program: the fix is to keep the effect on the node, not to catch it here.
 *
 * The type substitution and the node rewrite are one commit. `retype.h` says why: after the
 * rebuild the graph says the value is the enum while the static area still holds the function
 * object, so the graph would not run until the nodes that build and call the value are rewritten.
 * The other order is worse: between the two halves the graph hands an enum value to a parameter
 * that still says function, which is not a program.
 *
 * One thing the substitution must not do is retype a graph's own signature, because a graph is a
 * function: a function taken as a value has one type object for the value and for the signature,
 * and `GraphRetypeOptions::leaveSignatures` keeps the signature a function while its parameters
 * follow the substitution. Without it the graph `inc` becomes a value of the enum and every pass
 * that hands the graph somewhere as a function (`std::opt` hands it to `rules::simplify`) has no
 * instance for it.
 */

#include "defunctionalize.h"

#include "camel/core/error/diagnostics/messages/runtime.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/other.h"
#include "camel/core/type/text.h"
#include "camel/execute/pass/warnings.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"
#include "camel/runtime/program.h"
#include "camel/runtime/retype.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace camel::runtime;
namespace ty = camel::core::type;

using ty::TypeCode;
using ty::TypeId;

/// One function the program takes as a value: the enum variant that stands for it, its tag in that
/// enum, and the values the function captured -- the variant's fields, in capture order.
struct Variant {
    GraphId graph = GraphId::Invalid;
    std::string name;
    std::vector<ty::Type *> captures;
    size_t tag = 0;
};

/// One function type the program gives a value, and what the program does with it.
struct TypePlan {
    ty::Type *type = nullptr;
    std::string refuse;
    size_t values = 0;
    size_t calls  = 0;
    std::map<GraphId, Variant> variants;
};

/// The signatures one value is: the members have the same parameters and result, so one function
/// value is written as any of them and the run time holds one thing. Effects are left out of that
/// comparison (design principles 5.2), so a member may promise `pure` where another promises
/// nothing -- and, being the same type, they are one family whichever direction a value flows.
struct Family {
    std::vector<TypePlan *> members;
    std::vector<Variant> variants;
    std::string refuse;
    size_t values = 0;
    size_t calls  = 0;
};

/// One call through a value of a family: the node, and the function it reaches -- `GraphId::Invalid`
/// when only the run time knows it.
struct CallSite {
    gc_node_ref_t ref = kInvalidNodeRef;
    GraphId variant   = GraphId::Invalid;
    bool known        = false;
};

/// A value of one function type handed to a parameter of another where both types build values of
/// their own: the two stay two families and the value is wrapped in a variant of the parameter's
/// family, whose one field is the value family's enum and whose function is the wrapper.
struct WrapEdge {
    gc_node_ref_t node  = kInvalidNodeRef; // the node the value is handed to
    gc_node_ref_t value = kInvalidNodeRef; // the value, as it was written
    ty::Type *into      = nullptr;         // the parameter's declared type
    ty::Type *from      = nullptr;         // the value's type
};

/// The wrapper a widening is written with: the family it is a variant of, the family it dispatches
/// over, the graph that does the dispatch, and the variant's tag in the parameter's enum.
struct Wrapper {
    Family *into       = nullptr;
    const Family *from = nullptr;
    GraphId graph      = GraphId::Invalid;
    size_t tag         = 0;
};

::Function *staticFunctionOf(const GCGraph &graph, const GCNode &node) {
    if (node.dataIndex >= 0 || !graph.staticArea()) {
        return nullptr;
    }
    const size_t index = size_t(-node.dataIndex);
    if (index >= graph.staticSlots().size()) {
        return nullptr;
    }
    const slot_t slot = graph.staticArea()->get<slot_t>(index);
    if (slot == NullSlot) {
        return nullptr;
    }
    auto *function = camel::core::rtdata::fromSlot<::Function *>(slot);
    return function != nullptr && function->graph() != nullptr ? function : nullptr;
}

/// Whether `type` holds `target` in its signature: as itself, or inside a function type or an
/// aggregate it names. A value handed to an aggregate is taken apart by a node that is not a call
/// -- which is what the reader check refuses -- and a family whose signature holds a function type
/// this pass leaves as written cannot be lowered on its own (see (5b)).
bool mentions(const ty::Type *type, const ty::Type *target, int depth) {
    if (type == nullptr || target == nullptr || depth > 8) {
        return false;
    }
    if (type == target || type->typeId() == target->typeId() ||
        type->equals(const_cast<ty::Type *>(target))) {
        return true;
    }
    if (type->code() == TypeCode::Tuple) {
        for (const ty::Type *member : static_cast<const ty::TupleType *>(type)->types()) {
            if (mentions(member, target, depth + 1)) {
                return true;
            }
        }
        return false;
    }
    if (type->code() == TypeCode::Array) {
        return mentions(static_cast<const ty::ArrayType *>(type)->elemType(), target, depth + 1);
    }
    if (type->code() != TypeCode::Function) {
        return false;
    }
    const auto *function = static_cast<const ty::FunctionType *>(type);
    for (const ty::Type *param : function->withTypesSpan()) {
        if (mentions(param, target, depth + 1)) {
            return true;
        }
    }
    for (const ty::Type *param : function->normTypesSpan()) {
        if (mentions(param, target, depth + 1)) {
            return true;
        }
    }
    return mentions(function->exitType(), target, depth + 1);
}

std::string identifierOf(std::string_view name, std::string_view fallback) {
    std::string out;
    for (char c : name) {
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_');
    }
    if (out.empty()) {
        out = std::string(fallback);
    }
    if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

std::span<const std::byte> payloadOf(const void *body, size_t size) {
    return std::span<const std::byte>(static_cast<const std::byte *>(body), size);
}

/// Whether two signatures are one value's type: the same parameters and the same result, in
/// structure, with the effect qualifiers left out. `pure` and a resource clause say which
/// executions a value may take part in, and a `pure` value goes where the plain signature is
/// expected without a conversion, so they do not make another type (design principles 5.2).
bool sameFamily(ty::Type *left, ty::Type *right) {
    return left != nullptr && right != nullptr && left->code() == TypeCode::Function &&
           right->code() == TypeCode::Function && ty::typeEqualsIgnoringResources(left, right);
}

/// The plan entry of one function type, made once.
TypePlan &planOf(std::map<TypeId, TypePlan> &plans, ty::Type *type) {
    TypePlan &plan = plans[type->typeId()];
    plan.type      = type;
    return plan;
}

void refuseIfUnsaid(TypePlan &plan, std::string why) {
    if (plan.refuse.empty()) {
        plan.refuse = std::move(why);
    }
}

/// The variant of `target` in `plan`, made once with the values the function captures: a function
/// the program takes as a value is one variant of the family's enum, however many closure sites
/// build a value of it.
Variant &variantOf(TypePlan &plan, const GCGraph &target) {
    auto found = plan.variants.find(target.id());
    if (found != plan.variants.end()) {
        return found->second;
    }
    Variant variant;
    variant.graph = target.id();
    variant.name  = identifierOf(target.name(), "f");
    for (gc_node_ref_t capture : target.closureNodes()) {
        const GCNode *port = target.node(capture);
        if (port != nullptr && port->dataType() != nullptr) {
            variant.captures.push_back(port->dataType());
        }
    }
    return plan.variants.emplace(target.id(), std::move(variant)).first->second;
}

// The draft's own type is not named here: the editable graph is going away, and the ledger that
// counts the places still wired straight to it may only fall (`test/cases/runtime/
// graph_draft_ledger`). Every helper below reads the draft it is handed, whatever the session
// hands out.
/// The node a draft holds for a node of the frozen graph the session decoded it from.
template <class Draft> gc_node_ref_t draftIdOf(const Draft &draft, gc_node_ref_t sourceRef) {
    return draft.draftIdOfSourceRef(sourceRef);
}

/// A snapshot of every list a rewrite keeps: `rewriteNode` replaces the input lists it is given, and
/// a statement whose chains are dropped loses its place in the block.
template <class Draft> DraftNodeInit snapshotOf(const Draft &draft, gc_node_ref_t id) {
    const DraftNodeHeader *header = draft.header(id);
    DraftNodeInit init;
    init.dataIndex    = header->dataIndex;
    init.dataType     = header->dataType;
    init.kind         = header->kind;
    init.runtimeFlags = header->runtimeFlags;
    init.origin       = header->origin;
    init.permissions  = header->permissions;
    init.attrs        = header->attrs;
    init.region       = header->region;
    init.name         = header->name;
    init.payload      = draft.payloadOf(id);
    init.normInputs   = draft.normInputsOf(id);
    init.withInputs   = draft.withInputsOf(id);
    init.ctrlInputs   = draft.ctrlInputsOf(id);
    init.orderInputs  = draft.orderInputsOf(id);
    init.syncInputs   = draft.syncInputsOf(id);
    return init;
}

/// A function's captures become its trailing ordinary parameters: the pass calls such a function
/// with the values the closure captured, so the body reads them where it read its ports.
template <class Draft> void parameterizeCaptures(Draft &draft) {
    const std::vector<gc_node_ref_t> captures(
        draft.closureNodes().begin(), draft.closureNodes().end());
    if (captures.empty()) {
        return;
    }
    ty::FunctionType *type = draft.funcType();
    if (type == nullptr) {
        return;
    }
    // The signature is rebuilt rather than replaced so that it keeps what a signature means: the
    // names of its parameters, their kinds, and the effects it publishes.
    ty::FunctionTypeFactory factory;
    const bool named = type->hasArgNames();
    factory.setHasCompileInfo(named);
    factory.setResources(type->resources());
    for (size_t i = 0; i < type->withTypesCount(); ++i) {
        const std::string name = named ? std::string(type->argNameAt(i)) : std::string();
        if (type->hasWithRest() && i + 1 == type->withTypesCount()) {
            factory.addWithRest(name);
        } else if (type->withIsConstasAt(i)) {
            factory.addWithConstasArg(name, type->withTypeAt(i));
        } else {
            factory.addWithArg(
                name, type->withTypeAt(i), type->withIsVarAt(i), type->withNeedsValueAt(i));
        }
    }
    const size_t withCount = type->withTypesCount();
    for (size_t i = 0; i < type->normTypesCount(); ++i) {
        const std::string name = named ? std::string(type->argNameAt(withCount + i)) : std::string();
        if (type->hasRest() && i + 1 == type->normTypesCount()) {
            ty::Type *pack = type->normTypeAt(i) != nullptr &&
                                     type->normTypeAt(i)->code() == TypeCode::TypeVar
                                 ? type->normTypeAt(i)
                                 : nullptr;
            factory.addRestArg(name, pack);
        } else if (type->normIsConstasAt(i)) {
            factory.addConstasArg(name, type->normTypeAt(i));
        } else {
            factory.addNormArg(name, type->normTypeAt(i), type->normIsVarAt(i));
        }
    }
    for (gc_node_ref_t capture : captures) {
        const DraftNodeHeader *header = draft.header(capture);
        if (header == nullptr || header->dataType == nullptr) {
            continue;
        }
        draft.removeClosureNode(capture);
        draft.appendNormPort(capture);
        factory.addNormArg(std::string(), header->dataType, false);
    }
    factory.setExitType(type->hasExitType() ? type->exitType() : ty::Type::Void());
    draft.setFuncType(factory.build());
    draft.setClosureType(nullptr);
}

/// A call through a value of `family` where the variant is not known until the program runs: the
/// tag is read with `:enum/tag`, one arm of a `BRCH` per variant takes that variant's values out of
/// the value with `:enum/field` and calls the function the variant names, and the `JOIN` hands back
/// what the taken arm gave. One variant needs no branch: its values come out of the value and the
/// call names its function.
///
/// Every call it writes carries `resultType`, so a family whose signature declares `void` drops what
/// its functions give by the call alone (type/composite/func.h, `acceptsAsResult`); that is what
/// "drops the result" means here, and it is also how a wrapper of a `void` parameter passes on a
/// value that returns something.
///
/// `place` is the node the dispatch is written in place of -- the call the program wrote. A caller
/// that has no such node (a graph the pass writes itself) passes `kInvalidNodeRef` and gets the node
/// it made back as the result. The graphs the calls name are the caller's to put in its reference
/// table: this only names them in the payloads.
template <class Draft> gc_node_ref_t emitCallThroughValue(
    Draft &draft, const Family &family,
    std::span<const gc_node_ref_t> arguments, std::span<const gc_node_ref_t> withArguments,
    gc_node_ref_t value, ty::Type *resultType, uint32_t region, gc_node_ref_t place) {
    const gc_cnt_t count = static_cast<gc_cnt_t>(family.variants.size());
// A value this pass dispatches on has to be the enum of its function type, and that is checked
    // here, where the tag is about to be read. `:enum/tag` and `:enum/field` read a tagged pair; a
    // value that is still a plain reference to a function has no tag at those offsets, and reading
    // one out of it is undefined behaviour that shows up as a crash far from here. Such a value
    // means some site that builds it was not lowered -- a family lowered on one site and left
    // written on another -- so it is named and raised: the rule of this pass is that a family is
    // either lowered whole or left whole and named, never half of each (docs/design-principles.cn.md
    // on whole families), and a dispatch on a value of the wrong form is the pass's own defect, not
    // the program's.
    if (const DraftNodeHeader *dispatched = draft.header(value); dispatched != nullptr) {
        ty::Type *dispatchedType = dispatched->dataType;
        if (dispatchedType == nullptr || dispatchedType->code() != ty::EnumType::typeCode()) {
            const std::string familyName =
                !family.members.empty() && family.members.front()->type != nullptr
                    ? family.members.front()->type->toString()
                    : (!family.variants.empty() ? family.variants.front().name : std::string("?"));
            throw std::runtime_error(std::format(
                "the call through a value of '{}' dispatches on a value of type '{}', which is not "
                "the enum of its function type: the site that builds that value was not lowered, so "
                "it has no tag to read (a defect of the pass, not of the program)",
                familyName,
                dispatchedType != nullptr ? dispatchedType->toString() : std::string("<none>")));
        }
    }
    // TEMPORARY DIAGNOSTIC
    if (std::getenv("CAMEL_DEFUN_DEBUG") != nullptr) {
        std::fprintf(
            stderr,
            "[defun] dispatch members=%zu variants=%zu args=%zu result=%s\n",
            family.members.size(),
            family.variants.size(),
            arguments.size(),
            resultType != nullptr ? resultType->toString().c_str() : "<null>");
        for (const Variant &v : family.variants) {
            std::string caps;
            for (ty::Type *c : v.captures) {
                caps += (caps.empty() ? "" : ",") + (c ? c->toString() : std::string("<null>"));
            }
            std::fprintf(
                stderr,
                "[defun]   variant tag=%zu name=%s graph=%llu captures=[%s]\n",
                v.tag,
                v.name.c_str(),
                static_cast<unsigned long long>(camel::runtime::toValue(v.graph)),
                caps.c_str());
        }
    }
    // The values a variant captured, taken out of the value: the tag and the field's index are
    // what `:enum/field` reads.
    //
    // The field's type is the variant's own capture type, and that is the only source of it: the
    // enum a value of this family is declares its fields from these same captures (the declaration
    // is built from them below), so asking the enum rather than the variant is one more thing that
    // can be out of step with the function the call reaches and with the parameters that function
    // takes. It was: the dispatch asked the *call's* type for the enum, found none (a call's type is
    // its result, not the value it dispatches on), and fell back to `int` for every field -- so a
    // variant whose capture was a struct was called with an `int` where the struct belongs, and the
    // commit refused a call the pass had just written (benchmarks/mlp, `run_training`).
    const auto argumentsOf = [&](const Variant &variant) {
        std::vector<gc_node_ref_t> norm(arguments.begin(), arguments.end());
        for (size_t field = 0; field < variant.captures.size(); ++field) {
            const gc_node_ref_t variantTag = draft.materializeStaticValue(
                camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(
                    static_cast<camel::core::rtdata::Int64>(variant.tag)),
                ty::Type::Int64());
            const gc_node_ref_t fieldIndex = draft.materializeStaticValue(
                camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(
                    static_cast<camel::core::rtdata::Int64>(field)),
                ty::Type::Int64());
            ASSERT(
                variant.captures[field] != nullptr,
                "A variant of a function family has a capture whose type is not known.");
            ty::Type *taken = variant.captures[field];
            const gc_node_ref_t take = draft.addOperNode(taken, nullptr, ":enum/field");
            const std::vector<gc_node_ref_t> operands{variantTag, fieldIndex, value};
            draft.setNormInputs(take, operands);
            norm.push_back(take);
        }
        return norm;
    };

    if (count == 1) {
        // One variant: there is nothing to branch on. The values come out of the value and the call
        // names its function, as a folded call does when the callee is a value this graph builds.
        const Variant &variant                = family.variants.front();
        const std::vector<gc_node_ref_t> norm = argumentsOf(variant);
        if (place == kInvalidNodeRef) {
            const gc_node_ref_t call = draft.addFuncNode(variant.graph, resultType, 0, 0, 0);
            draft.setNormInputs(call, norm);
            draft.setWithInputs(call, withArguments);
            return call;
        }
        GCFuncRef callee{.ref = draft.payloadGraphIndexOf(variant.graph)};
        DraftNodeInit init = snapshotOf(draft, place);
        init.kind          = GCNodeKind::Func;
        init.payload       = payloadOf(&callee, sizeof(callee));
        init.normInputs    = norm;
        init.withInputs    = withArguments;
        draft.rewriteNode(place, init);
        return place;
    }

    // The `BRCH` names the `JOIN` and the `JOIN` names the `BRCH`: when the caller has no node for
    // the result, the `JOIN` is made first so the branch can name it, and its payload is written
    // once the branch exists.
    const gc_node_ref_t join =
        place != kInvalidNodeRef ? place : draft.addJoinNode(resultType, kInvalidNodeRef, count);
    const gc_node_ref_t tag = draft.addOperNode(ty::Type::Int64(), nullptr, ":enum/tag");
    draft.setNormInputs(tag, std::span<const gc_node_ref_t>(&value, 1));
    std::vector<gc_node_ref_t> cases;
    for (size_t k = 0; k + 1 < family.variants.size(); ++k) {
        cases.push_back(draft.materializeStaticValue(
            camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(
                static_cast<camel::core::rtdata::Int64>(k)),
            ty::Type::Int64()));
    }
    const gc_node_ref_t brch = draft.addBrchNode(ty::Type::Int64(), join, count);
    draft.setNormInputs(brch, std::span<const gc_node_ref_t>(&tag, 1));
    draft.setWithInputs(brch, cases);
    std::vector<gc_node_ref_t> armResults;
    for (size_t k = 0; k < family.variants.size(); ++k) {
        const Variant &variant   = family.variants[k];
        const uint32_t armRegion = draft.addRegion(GCRegionKind::Arm, region, brch, k);
        draft.setInsertRegion(armRegion);
        const std::vector<gc_node_ref_t> norm = argumentsOf(variant);
        const gc_node_ref_t call              = draft.addFuncNode(variant.graph, resultType, 0, 0, 0);
        draft.setNormInputs(call, norm);
        draft.setWithInputs(call, withArguments);
        draft.setRegionResult(armRegion, call);
        draft.setInsertRegion(region);
        armResults.push_back(call);
    }
    GCJoinBody body{.brch = brch, .armCount = count};
    DraftNodeInit init = snapshotOf(draft, join);
    init.kind          = GCNodeKind::Join;
    init.payload       = payloadOf(&body, sizeof(body));
    init.normInputs    = std::span<const gc_node_ref_t>(&brch, 1);
    init.withInputs    = armResults;
    draft.rewriteNode(join, init);
    return join;
}

/// The wrapper a widening is written with: a graph of the parameter's type whose last parameter is
/// the enum of the family the value belongs to. Its body dispatches over that enum exactly as a call
/// through a value does, and its own exit type is the parameter's -- so a parameter that says `void`
/// drops what the function it reaches gives, by the call alone.
template <class Draft> GCGraph *writeWrapperGraph(
    Draft &draft, RuntimeGraphDraftSession &session, const Family &from,
    ty::FunctionType *signature, const std::string &name) {
    draft.setProgram(&session.work());
    std::vector<gc_node_ref_t> ports;
    for (ty::Type *type : signature->normTypesSpan()) {
        const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
        draft.appendNormPort(port);
        ports.push_back(port);
    }
    // The functions the dispatch calls belong to the value's family: this graph names them, so they
    // are in its reference table as well as in the payloads the dispatch writes.
    for (const Variant &variant : from.variants) {
        draft.addDependency(variant.graph);
    }
    const std::vector<gc_node_ref_t> arguments(ports.begin(), ports.end() - 1);
    const gc_node_ref_t value      = ports.back();
    const gc_node_ref_t result     = emitCallThroughValue(
        draft, from, arguments, std::span<const gc_node_ref_t>{}, value,
        signature->exitType(), 0, kInvalidNodeRef);
    draft.setExitNode(result);
    GCGraph *encoded = draft.encode(name, name);
    session.adoptGraph(encoded);
    return encoded;
}

} // namespace

const camel::runtime::Program *
DefunctionalizeRewritePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;

    if (program == nullptr || program->root() == nullptr) {
        return program;
    }

    // ---------------------------------------------------------------------------------------
    // (1) The static values a closure is built on, so they are not taken for values of their own.
    // ---------------------------------------------------------------------------------------
    std::map<uint64_t, std::set<gc_node_ref_t>> closureSources; // graph id -> the value under one
    for (const GCGraph *graph : program->graphs()) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const GCNode &node = **it;
            if (node.kind != GCNodeKind::Fill ||
                node.dataType() == nullptr ||
                node.dataType()->code() != TypeCode::Function) {
                continue;
            }
            const auto *fill = graph->nodeBodyAs<GCFillBody>(it.ref());
            if (fill == nullptr || fill->fillKind != GCFillKind::FunctionClosure) {
                continue;
            }
            const auto norms = graph->normInputsOf(it.ref());
            if (!norms.empty()) {
                closureSources[toValue(graph->id())].insert(norms.front());
            }
        }
    }

    // ---------------------------------------------------------------------------------------
    // (2) What the program holds: values of a function type, and the function each one names.
    // ---------------------------------------------------------------------------------------
    std::map<TypeId, TypePlan> plans;
    std::map<uint64_t, std::set<gc_node_ref_t>> valueRefs;  // graph id -> the nodes that hold one
    std::map<uint64_t, std::set<gc_node_ref_t>> fillRefs;   // graph id -> the closures built there
    std::map<uint64_t, std::vector<CallSite>> callsites;    // graph id -> its calls through a value

    for (const GCGraph *graph : program->graphs()) {
        const auto &sources = closureSources[toValue(graph->id())];
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const GCNode &node = **it;
            ty::Type *type     = node.dataType();
            if (type == nullptr || type->code() != TypeCode::Function) {
                continue;
            }
            // A parameter of the type is where a value arrives; it is not a value the program
            // builds, and it is rewritten with the signature.
            if (node.kind == GCNodeKind::Port) {
                planOf(plans, type);
                continue;
            }
            TypePlan &plan = planOf(plans, type);
            if (node.kind == GCNodeKind::Fill) {
                const auto *fill = graph->nodeBodyAs<GCFillBody>(it.ref());
                if (fill == nullptr || fill->fillKind != GCFillKind::FunctionClosure) {
                    refuseIfUnsaid(plan, "a value is not a plain reference to a function");
                    continue;
                }
                const auto norms   = graph->normInputsOf(it.ref());
                const GCNode *base = norms.empty() ? nullptr : graph->node(norms.front());
                ::Function *function = base != nullptr ? staticFunctionOf(*graph, *base) : nullptr;
                if (function == nullptr) {
                    refuseIfUnsaid(
                        plan, "a value holds something that is not a function of the program");
                    continue;
                }
                const GCGraph *target = function->graph();
                const auto withs      = graph->withInputsOf(it.ref());
                if (fill->slots().size() != withs.size() ||
                    target->closureNodes().size() != withs.size()) {
                    refuseIfUnsaid(
                        plan, "the closure does not fill every value the function captures");
                    continue;
                }
                variantOf(plan, *target);
                valueRefs[toValue(graph->id())].insert(it.ref());
                fillRefs[toValue(graph->id())].insert(it.ref());
                ++plan.values;
                continue;
            }
            if (node.kind == GCNodeKind::Data && node.dataIndex < 0) {
                if (sources.contains(it.ref())) {
                    continue; // the static function a closure is built on: not a value of its own
                }
                ::Function *function = staticFunctionOf(*graph, node);
                if (function == nullptr) {
                    refuseIfUnsaid(
                        plan, "a value holds something that is not a function of the program");
                    continue;
                }
                const GCGraph *target = function->graph();
                if (!target->closureNodes().empty()) {
                    refuseIfUnsaid(
                        plan, "the value does not carry the values the function captures");
                    continue;
                }
                variantOf(plan, *target);
                valueRefs[toValue(graph->id())].insert(it.ref());
                ++plan.values;
                continue;
            }
            // A function the program names as a value is a `FUNC` node: it calls nothing, it *is*
            // the value. Without captures it is the function itself, which is one of the variants;
            // with captures there is nothing here to carry them, and that is another shape (a
            // closure written as a direct reference), not this pass's to guess at.
            if (node.kind == GCNodeKind::Func) {
                const GCGraph *target = program->calleeOf(*graph, it.ref());
                if (target == nullptr) {
                    refuseIfUnsaid(
                        plan, "a value holds a function the program does not have");
                    continue;
                }
                if (!target->closureNodes().empty()) {
                    refuseIfUnsaid(
                        plan, "unresolved capture, not this pass's shape: the value is a direct "
                              "reference to a function that captures");
                    continue;
                }
                variantOf(plan, *target);
                valueRefs[toValue(graph->id())].insert(it.ref());
                fillRefs[toValue(graph->id())].insert(it.ref()); // rewritten as a Fill of the tag
                ++plan.values;
                continue;
            }
            refuseIfUnsaid(plan, "a value is not a plain reference to a function");
        }
    }

    if (plans.empty()) {
        if (report_) {
            os << "DEFUNCTIONALIZE types=0 values=0 calls=0 refused=0\n";
        }
        return program;
    }

    // ---------------------------------------------------------------------------------------
    // (3) What else reads a value, and every call through the type.
    // ---------------------------------------------------------------------------------------
    // A value handed to a parameter of another function type widens at the call: the two are one
    // family and one enum, so the parameter's type joins the family the value belongs to and is
    // substituted with its enum like any other member. Keyed by the widened type's id, naming the
    // type of the value that flows into it.
    std::map<TypeId, TypeId> mergedInto; // widened parameter type id -> the value's type id
    // A widening that is written as a wrapper instead: the parameter's type id -> the types of the
    // values handed to it. Both types stay families, so the value keeps its enum and the parameter's
    // family gains a variant that holds it.
    std::map<TypeId, std::set<TypeId>> wrapsInto;
    // The edges that carry such a value, per graph, to be wrapped once the enums exist.
    std::map<uint64_t, std::vector<WrapEdge>> wrapEdges;
    for (const GCGraph *graph : program->graphs()) {
        // The static function a closure is built on is not a value: the closure packs it into a
        // tag, and the node is dropped with the closure.
        const auto &sources = closureSources[toValue(graph->id())];
        std::map<gc_node_ref_t, const GCNode *> nodes;
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            nodes.emplace(it.ref(), *it);
        }
        const auto typeOf = [&](gc_node_ref_t ref) -> ty::Type * {
            const auto found = nodes.find(ref);
            return found == nodes.end() ? nullptr : found->second->dataType();
        };
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const GCNode &node      = **it;
            const gc_node_ref_t ref = it.ref();
            if (node.kind == GCNodeKind::Call) {
                const auto withs = graph->withInputsOf(ref);
                if (withs.empty()) {
                    continue;
                }
                ty::Type *calleeType = typeOf(withs.front());
                if (calleeType == nullptr || calleeType->code() != TypeCode::Function) {
                    continue;
                }
                TypePlan &plan = planOf(plans, calleeType);
                ++plan.calls;
                const GCNode *callee = nodes.at(withs.front());
                if (!graph->waitInputsOf(withs.front()).empty()) {
                    refuseIfUnsaid(plan, "a call's callee waits on something");
                }
                CallSite site;
                site.ref = ref;
                if (callee->kind == GCNodeKind::Data && callee->dataIndex < 0) {
                    if (::Function *function = staticFunctionOf(*graph, *callee)) {
                        site.variant = function->graph()->id();
                        site.known   = true;
                    }
                } else if (callee->kind == GCNodeKind::Fill) {
                    const auto *fill = graph->nodeBodyAs<GCFillBody>(withs.front());
                    const auto norms = graph->normInputsOf(withs.front());
                    const GCNode *base =
                        norms.empty() ? nullptr : graph->node(norms.front());
                    if (fill != nullptr && fill->fillKind == GCFillKind::FunctionClosure &&
                        base != nullptr) {
                        if (::Function *function = staticFunctionOf(*graph, *base)) {
                            site.variant = function->graph()->id();
                            site.known   = true;
                        }
                    }
                } else if (callee->kind != GCNodeKind::Port) {
                    refuseIfUnsaid(
                        plan, "a call reads a callee that is neither a value nor a parameter");
                }
                callsites[toValue(graph->id())].push_back(site);
            }
            // Nothing else may read a value of the family -- a parameter as much as a value this
            // graph builds: the rewrite changes what the type is, and a reader that does not take
            // the tag (a higher-order operator such as `map`, which calls the value inside the
            // operator) would be handed an enum where it expects a function.
            const auto checkReader = [&](std::span<const gc_node_ref_t> inputs) {
                for (gc_node_ref_t input : inputs) {
                    if (sources.contains(input)) {
                        continue;
                    }
                    ty::Type *inputType = typeOf(input);
                    if (inputType == nullptr || inputType->code() != TypeCode::Function) {
                        continue;
                    }
                    const auto planned = plans.find(inputType->typeId());
                    if (planned == plans.end()) {
                        continue;
                    }
                    const auto withs = graph->withInputsOf(ref);
                    const bool readsAsCallee =
                        node.kind == GCNodeKind::Call && !withs.empty() && withs.front() == input;
                    // A value handed to another function is read where the callee declares it, and
                    // that declaration is the same type: it is substituted with the value's. A call
                    // writes its arguments in the norm inputs whether the callee is named (a `FUNC`
                    // node) or is a value the graph reads (a `CALL` node, whose callee is its first
                    // with input): both are the callee's own parameters, and a parameter of a
                    // function type is where a value of another family arrives.
                    const auto norms = graph->normInputsOf(ref);
                    const bool callsSomeone = node.kind == GCNodeKind::Func || node.kind == GCNodeKind::Call;
                    bool readsAsArgument =
                        callsSomeone && std::find(norms.begin(), norms.end(), input) != norms.end();
                    if (!readsAsArgument && node.kind == GCNodeKind::Call) {
                        readsAsArgument =
                            std::find(withs.begin() + (withs.empty() ? 0 : 1), withs.end(), input) !=
                            withs.end();
                    }
                    if (!readsAsCallee && !readsAsArgument) {
                        refuseIfUnsaid(
                            planned->second, "the value is read by a node that is not a call");
                    }
                    // A value handed to a parameter declared as another function type is a widening
                    // at the call: a lambda that returns the value of its write is written where the
                    // parameter says `void`, which is a function type of its own. Two ways to write
                    // it, and which one depends on what the parameter's own type is:
                    //
                    // * the parameter's type builds no value: the value flows from its family into
                    //   that type and the two are one family -- the parameter's type becomes the
                    //   enum of the family the value belongs to, so no second form of a function
                    //   value is left;
                    // * the parameter's type builds values of its own: folding the two into one enum
                    //   would call those values with a tag that is not theirs, so the two stay two
                    //   families and the value is wrapped -- the parameter's family gains a variant
                    //   whose one field is the value family's enum, and the wrapper that variant
                    //   names calls the value and drops what it gives.
                    if (readsAsArgument) {
                        const ty::FunctionType *signature = nullptr;
                        if (node.kind == GCNodeKind::Call && !withs.empty()) {
                            ty::Type *calleeType = typeOf(withs.front());
                            if (calleeType != nullptr && calleeType->code() == TypeCode::Function) {
                                signature = static_cast<const ty::FunctionType *>(calleeType);
                            }
                        } else if (node.kind == GCNodeKind::Func) {
                            if (const GCGraph *callee = program->calleeOf(*graph, ref)) {
                                signature = callee->funcType();
                            }
                        }
                        if (signature != nullptr) {
                            bool declared = false;
                            for (const auto &span :
                                 {signature->withTypesSpan(), signature->normTypesSpan()}) {
                                for (const ty::Type *param : span) {
                                    auto *written = const_cast<ty::Type *>(param);
                                    declared = declared || written->equals(inputType) ||
                                               sameFamily(written, inputType);
                                }
                            }
                            if (!declared) {
                                for (const auto &span :
                                     {signature->withTypesSpan(), signature->normTypesSpan()}) {
                                    for (const ty::Type *param : span) {
                                        auto *written = const_cast<ty::Type *>(param);
                                        if (written->code() != TypeCode::Function ||
                                            written->equals(inputType) ||
                                            sameFamily(written, inputType)) {
                                            continue;
                                        }
                                        // A function type of the parameter's own kind that
                                        // builds a value somewhere is a family of its own: the
                                        // widening would have to be written as a conversion, and
                                        // that needs a wrapper this pass cannot make. Stopping
                                        // names the program, which is what a defect of the compiler
                                        // owes the reader -- not a half-lowered graph.
                                        bool buildsOwn = false;
                                        for (const auto &[otherId, other] : plans) {
                                            buildsOwn = buildsOwn ||
                                                        (other.values > 0 &&
                                                         (other.type->equals(written) ||
                                                          sameFamily(other.type, written)));
                                        }
                                        if (buildsOwn) {
                                            // The parameter's own type builds values, so it stays a
                                            // family of its own: folding the two into one enum would
                                            // call those values with a tag that is not theirs. The
                                            // widening is written instead -- the parameter's family
                                            // gains a variant whose one field is the value family's
                                            // enum, and the wrapper that variant names calls the
                                            // value and drops what it gives. This edge is the place
                                            // the value is wrapped, once the enums exist.
                                            wrapsInto[written->typeId()].insert(
                                                planned->second.type->typeId());
                                            planOf(plans, written);
                                            wrapEdges[toValue(graph->id())].push_back(
                                                WrapEdge{
                                                    .node  = ref,
                                                    .value = input,
                                                    .into  = written,
                                                    .from  = planned->second.type});
                                            return;
                                        }
                                        mergedInto[written->typeId()] =
                                            planned->second.type->typeId();
                                        // The widened type is a member of the family from here on,
                                        // so it is a plan like any other: its calls are counted
                                        // there, and its objects follow the substitution.
                                        planOf(plans, written);
                                    }
                                }
                            }
                        }
                    }
                }
            };
            checkReader(graph->normInputsOf(ref));
            checkReader(graph->withInputsOf(ref));
            checkReader(graph->ctrlInputsOf(ref));
        }
    }

    // A function whose captures become parameters cannot also be called directly: that call does
    // not pass them.
    std::set<GraphId> directlyCalled;
    for (const GCGraph *graph : program->graphs()) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if ((**it).kind != GCNodeKind::Func) {
                continue;
            }
            if (const GCGraph *callee = program->calleeOf(*graph, it.ref())) {
                directlyCalled.insert(callee->id());
            }
        }
    }

    // ---------------------------------------------------------------------------------------
    // (4) One family per type: the signatures that are one value's type, effect qualifiers aside.
    // ---------------------------------------------------------------------------------------
    std::vector<Family> families;
    std::set<TypeId> assigned;
    for (auto &[id, plan] : plans) {
        // A widened type is not a family of its own: it joins the family the value that flows into
        // it belongs to, and its calls are that family's calls.
        if (assigned.count(id) != 0 || mergedInto.count(id) != 0) {
            continue;
        }
        families.emplace_back();
        Family &family = families.back();
        for (auto &[otherId, other] : plans) {
            if (assigned.count(otherId) != 0) {
                continue;
            }
            // A widened type joins the family of the value that flows into it. The value's own type
            // may be another object of that family, so the relation is to its signature and not to
            // the id the widening was recorded with.
            bool same = sameFamily(plan.type, other.type);
            if (!same) {
                const auto merge  = mergedInto.find(otherId);
                const auto target = merge != mergedInto.end() ? plans.find(merge->second)
                                                             : plans.end();
                same = target != plans.end() && sameFamily(plan.type, target->second.type);
            }
            if (!same) {
                continue;
            }
            assigned.insert(otherId);
            family.members.push_back(&other);
        }
    }
    for (Family &family : families) {
        for (TypePlan *member : family.members) {
            family.values += member->values;
            family.calls += member->calls;
            if (family.refuse.empty()) {
                family.refuse = member->refuse;
            }
            for (const auto &[graphId, variant] : member->variants) {
                const bool known = std::any_of(
                    family.variants.begin(), family.variants.end(),
                    [&](const Variant &seen) { return seen.graph == variant.graph; });
                if (!known) {
                    family.variants.push_back(variant);
                }
            }
        }
        // The tag of a variant is its position, and one enum names each variant once: two
        // functions of one family can be written with no name of their own (two lambdas).
        std::sort(
            family.variants.begin(), family.variants.end(),
            [](const Variant &left, const Variant &right) {
                return left.name != right.name ? left.name < right.name : left.graph < right.graph;
            });
        std::set<std::string> used;
        for (Variant &variant : family.variants) {
            std::string name = variant.name;
            size_t suffix    = 2;
            while (used.contains(name)) {
                name = variant.name + "_" + std::to_string(suffix++);
            }
            used.insert(name);
            variant.name = name;
        }
        for (size_t tag = 0; tag < family.variants.size(); ++tag) {
            family.variants[tag].tag = tag;
        }
        if (family.refuse.empty()) {
            for (const Variant &variant : family.variants) {
                if (!variant.captures.empty() && directlyCalled.contains(variant.graph)) {
                    family.refuse = "the function is called directly where its captures are not "
                                    "arguments";
                    break;
                }
            }
        }
        if (family.values == 0 && family.refuse.empty()) {
            family.refuse = "the type has no value the program builds";
        }
    }

    // ---------------------------------------------------------------------------------------
    // (5) The entry function.
    // ---------------------------------------------------------------------------------------
    if (const auto *rootFunction = program->root()->funcType()) {
        auto *rootFunctionAsType = const_cast<ty::Type *>(static_cast<const ty::Type *>(rootFunction));
        for (const ty::Type *param : rootFunction->normTypesSpan()) {
            for (Family &family : families) {
                for (TypePlan *member : family.members) {
                    if (family.refuse.empty() && mentions(param, member->type, 0)) {
                        family.refuse = "the type is an argument of the entry function";
                    }
                }
            }
        }
        // The entry function's own value is the runtime's: the shell builds it, hands it its
        // arguments and calls it, so there is no program value here to give a form. The family is
        // not one this pass has anything to say about -- leaving it out rather than refusing it
        // keeps the pass from re-encoding an entry point and from reporting the program's own
        // entry as something it gave up on.
        std::erase_if(families, [&](const Family &family) {
            return sameFamily(family.members.front()->type, rootFunctionAsType);
        });
    }

    // ---------------------------------------------------------------------------------------
    // (5b) Families that hold one another.
    //
    // A lowered family may not hold, or be held by, a family left as written: its signature would
    // still say the other's function type -- `(int, () => int..)`, say -- while a value of it became
    // a tag, and a graph that carries both forms is one no later pass can type. A family whose
    // signature names another is one value's signature: the inner function type in the outer
    // signature becomes the inner family's enum, which is the same rule as the widening above -- a
    // value flowing from one family into another, one form and one enum for both.
    //
    // The nested lowering is one rewrite: a family is lowered together with every family its
    // signature holds, and every enum is declared before any graph is retyped, so the signature of a
    // function of the outer type says the inner family's enum where it wrote the inner function
    // type, and a call it makes reaches a tag it can dispatch on. So this rule now says one thing
    // only: a family is left as written when a family its signature holds is left as written,
    // because then there is no inner enum to write in its place. It is not an allowance for
    // lowering half a nesting -- that state is the one the rule exists to prevent.
    //
    // `tee_result_is_tuple.cml` does not reach this rule today: its inner family is refused first,
    // because a stream operator reads the function value inside the operator rather than calling it
    // where this pass can see the call. That belongs to the operator-dispatch side (`map` is the
    // same shape), not to this pass.
    for (bool changed = true; changed;) {
        changed = false;
        for (Family &family : families) {
            if (!family.refuse.empty()) {
                continue;
            }
            const ty::Type *own = family.members.front()->type;
            for (const Family &other : families) {
                const ty::Type *theirs = other.members.front()->type;
                if (other.refuse.empty() || &other == &family ||
                    !(mentions(own, theirs, 0) || mentions(theirs, own, 0))) {
                    continue;
                }
                family.refuse = std::format(
                    "its type and '{}' name one another, and that one is left as written",
                    theirs->toString());
                changed = true;
                break;
            }
        }
    }

    std::vector<Family *> accepted;
    for (Family &family : families) {
        if (family.refuse.empty() && family.values != 0 && !family.variants.empty()) {
            accepted.push_back(&family);
        }
    }
    // A pass that silently gives up looks exactly like one that found nothing to do: every family
    // it leaves as written is named with the reason, on the report line and as a warning. A pass the
    // pipeline inserted says nothing: its report would be written on the program's own output, and
    // the program did not ask for it.
    const auto reportRefused = [&] {
        if (!report_) {
            return;
        }
        for (Family &family : families) {
            if (family.refuse.empty()) {
                continue;
            }
            const std::string type = family.members.front()->type->toString();
            os << "DEFUNCTIONALIZE refused='" << type << "' reason='" << family.refuse << "'\n";
            if (context_ != nullptr) {
                // The diagnostic's own text is "Defunctionalization left {0} as written.", so the
                // argument is the type and the reason as one clause, not a second sentence.
                raisePassWarning(
                    *context_,
                    camel::core::error::RuntimeDiag::DefunctionalizationIncomplete,
                    camel::source::kInvalidOriginId,
                    std::format("'{}' ({})", type, family.refuse));
            }
        }
    };
    if (accepted.empty()) {
        if (report_) {
            os << "DEFUNCTIONALIZE types=0 values=0 calls=0 refused=" << families.size() << "\n";
        }
        reportRefused();
        return program;
    }

    // ---------------------------------------------------------------------------------------
    // (5c) The families a widening wraps, and the order their enums are declared in.
    //
    // A wrapper variant holds the enum of the family it wraps, so that enum has to exist first: the
    // relation is acyclic or the widening cannot be written, and a cycle is named rather than half
    // lowered. A value whose family is left as written is refused here too -- an enum cannot hold a
    // function value.
    // ---------------------------------------------------------------------------------------
    std::map<TypeId, Family *> familyOfType; // a member type's id -> its family
    for (Family *family : accepted) {
        for (const TypePlan *member : family->members) {
            familyOfType[member->type->typeId()] = family;
        }
    }
    std::map<const Family *, std::vector<const Family *>> wraps; // family -> the families it wraps
    for (const auto &[intoId, fromIds] : wrapsInto) {
        const auto intoFound = familyOfType.find(intoId);
        if (intoFound == familyOfType.end()) {
            continue;
        }
        Family *into = intoFound->second;
        for (TypeId fromId : fromIds) {
            const auto fromFound = familyOfType.find(fromId);
            if (fromFound == familyOfType.end()) {
                // The value's family is not lowered: a variant cannot hold a function value, so the
                // parameter's family is left as written and named.
                if (into->refuse.empty()) {
                    into->refuse = "its parameter takes a value of a function type that is left as "
                                   "written";
                }
                continue;
            }
            const Family *from = fromFound->second;
            if (from == into) {
                continue; // the two are one family already
            }
            const auto *intoType = static_cast<const ty::FunctionType *>(into->members.front()->type);
            const auto *fromType = static_cast<const ty::FunctionType *>(from->members.front()->type);
            // The wrapper is one function of the parameter's type: its with-parameters would have to
            // be handed on to the value, and a graph the pass writes has no with-parameter of its
            // own. A signature that declares one is named instead of written wrongly -- and both
            // sides are named, because leaving only one of them lowered is the half-lowered graph
            // this step exists to remove.
            if (intoType->withTypesCount() != 0 || fromType->withTypesCount() != 0) {
                if (into->refuse.empty()) {
                    into->refuse = "the widening needs a wrapper and the signature declares "
                                   "with-parameters";
                }
                if (from->refuse.empty()) {
                    const_cast<Family *>(from)->refuse =
                        "a value of it is widened into a signature that declares with-parameters";
                }
                continue;
            }
            const auto seen = wraps.find(into);
            if (seen == wraps.end() || std::find(seen->second.begin(), seen->second.end(), from) ==
                                           seen->second.end()) {
                wraps[into].push_back(from);
            }
        }
    }
    if (!wraps.empty()) {
        // The enums are declared depth first: a family's wrappers are declared before it.
        std::vector<Family *> ordered;
        std::set<const Family *> done, active;
        const auto visit = [&](Family *family, const auto &self) -> void {
            if (done.contains(family)) {
                return;
            }
            if (active.contains(family)) {
                // Defensive, and not reachable today: an edge of the wrapping relation comes from a
                // value widened into a parameter, which needs the parameter's signature to declare a
                // `void` result and both signatures' parameters to be the same (`acceptsAsResult`,
                // type/composite/func.h). Every family in a would-be cycle therefore has the same
                // parameters and a `void` result -- one family -- and that pair is skipped above
                // before it is recorded (`from == into`), so no cycle is ever built. It is raised as
                // a defect because a cycle would mean the pass wrote a wrapper that calls itself,
                // and a named defect beats a graph nobody can type; there is no test for it because
                // no program could reach it.
                throw std::runtime_error(std::format(
                    "the widening of '{}' needs a wrapper that would call itself (a defect of the "
                    "pass, not of the program)",
                    family->members.front()->type->toString()));
            }
            active.insert(family);
            const auto found = wraps.find(family);
            if (found != wraps.end()) {
                for (const Family *from : found->second) {
                    self(const_cast<Family *>(from), self);
                }
            }
            active.erase(family);
            done.insert(family);
            ordered.push_back(family);
        };
        for (Family *family : accepted) {
            visit(family, visit);
        }
        accepted = std::move(ordered);
        // A family refused while the wrapper relation was read is not lowered after all.
        std::erase_if(accepted, [](const Family *family) { return !family->refuse.empty(); });
        if (accepted.empty()) {
            if (report_) {
                os << "DEFUNCTIONALIZE types=0 values=0 calls=0 refused=" << families.size() << "\n";
            }
            reportRefused();
            return program;
        }
    }

    // ---------------------------------------------------------------------------------------
    // (6) The enum per family, and the table that retypes every object equal to a member.
    //
    // A function type is structural: the same signature may be a different object with a different
    // id, one per place it is written. The table names ids, so every object equal to a member goes
    // in it -- from node types and from the signature of every graph. Missing one leaves half the
    // program on the old type, which is the state the rebuild cannot run in.
    // ---------------------------------------------------------------------------------------
    std::map<TypeId, std::set<ty::Type *>> equivalents; // member type id -> equal objects

    const auto noteType = [&](ty::Type *type) {
        if (type == nullptr) {
            return;
        }
        for (Family *family : accepted) {
            for (TypePlan *member : family->members) {
                // A family is its signature with every resource clause it is written with (design
                // principles 5.2), so an object that differs from a member only in the clause is a
                // member too: a call would otherwise hand the tag to a signature that still says the
                // function type.
                if (type->equals(member->type) || sameFamily(type, member->type)) {
                    equivalents[member->type->typeId()].insert(type);
                }

            }
        }
    };
    for (const GCGraph *graph : program->graphs()) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            noteType((**it).dataType());
        }
        const auto *signature = graph->funcType();
        if (signature != nullptr) {
            for (ty::Type *type : signature->withTypesSpan()) {
                noteType(type);
            }
            for (ty::Type *type : signature->normTypesSpan()) {
                noteType(type);
            }
            noteType(signature->exitType());
        }
    }

    GraphTypeSubstitution types;
    std::map<TypeId, Family *> familyOfEnum;         // enum type id -> the family it stands for
    std::map<GraphId, const Family *> familyOfGraph; // a variant's function -> its family
    std::map<const Family *, ty::EnumType *> enumOfFamily;
    std::vector<Wrapper> wrappers;
    size_t index = 0;
    for (Family *family : accepted) {
        const std::string name = "FnValue_" +
                                 identifierOf(family->variants.front().name, "f") + "_" +
                                 std::to_string(index++);
        ty::EnumDecl *decl = ty::EnumDecl::create(name, {}, std::string());
        std::vector<ty::EnumVariant> variants;
        for (const Variant &variant : family->variants) {
            ty::EnumVariant declared;
            declared.name = variant.name;
            if (variant.captures.empty()) {
                declared.shape = ty::VariantShape::Unit;
            } else {
                declared.shape      = ty::VariantShape::Tuple;
                declared.fieldTypes = variant.captures;
                declared.fieldOrigins.assign(
                    variant.captures.size(), camel::source::kInvalidOriginId);
            }
            variants.push_back(std::move(declared));
        }
        // One more variant for each widening this family wraps: its one field is the enum of the
        // family the value belongs to, and its function is the wrapper that dispatches over it. The
        // wrapper's graph is written in the commit below, so the variant is added to the family now
        // and names its graph then; until it is named no call can reach it.
        const auto wrappedHere = wraps.find(family);
        if (wrappedHere != wraps.end()) {
            for (const Family *from : wrappedHere->second) {
                const auto fromEnum = enumOfFamily.find(from);
                if (fromEnum == enumOfFamily.end() || family->refuse.length() != 0) {
                    continue;
                }
                ty::EnumVariant declared;
                const size_t tag = variants.size();
                declared.name    = std::format("wrap${}", tag);
                declared.shape   = ty::VariantShape::Tuple;
                declared.fieldTypes = {fromEnum->second};
                declared.fieldOrigins.assign(1, camel::source::kInvalidOriginId);
                variants.push_back(std::move(declared));
                Variant wrapped;
                wrapped.name     = std::format("wrap${}", tag);
                wrapped.tag      = tag;
                wrapped.captures = {fromEnum->second};
                family->variants.push_back(wrapped);
                wrappers.push_back(Wrapper{.into = family, .from = from, .tag = tag});
            }
        }
        decl->define(std::move(variants));
        ty::EnumType *enumType = decl->prototype();
        for (TypePlan *member : family->members) {
            for (ty::Type *type : equivalents[member->type->typeId()]) {
                types.replace(type, enumType);
            }

            types.replace(member->type, enumType);
        }
        familyOfEnum.emplace(enumType->typeId(), family);
        enumOfFamily.emplace(family, enumType);
        for (const Variant &variant : family->variants) {
            // A wrapper variant names its graph in the commit below; until then it has none.
            if (variant.graph != GraphId::Invalid) {
                familyOfGraph.emplace(variant.graph, family);
            }
        }
    }

    GraphRetypeOptions options;
    options.leaveStaticObjects = true; // the value's object is replaced by the node rewrite below
    // A graph stays a function: the pass replaces the *values* of the function type, and the
    // signature of a function that is one of them keeps its shape.
    options.leaveSignatures = true;


    // ---------------------------------------------------------------------------------------
    // (7) One commit: the type of every value, its constructor and its call change together.
    // ---------------------------------------------------------------------------------------
    const auto tagOf = [](const Family &family, GraphId graph) -> const Variant * {
        for (const Variant &variant : family.variants) {
            if (variant.graph == graph) {
                return &variant;
            }
        }
        return nullptr;
    };
    const auto familyAt = [&](ty::Type *type) -> const Family * {
        if (type == nullptr) {
            return nullptr;
        }
        const auto found = familyOfEnum.find(type->typeId());
        return found == familyOfEnum.end() ? nullptr : found->second;
    };

    size_t rewroteValues = 0, rewroteCalls = 0;
    // The rewrite is one session, so nothing of an attempt that cannot be committed reaches the
    // program. What breaks inside it -- a graph the encoder refuses, a signature that does not match
    // its ports -- is the pass's own work and not the program's, so it is raised rather than caught:
    // a pass that throws is reported as `PassInternalError` (execute/pass.cpp), while catching here
    // would turn a defect into a warning and hand the pipeline the program as written, which is the
    // one state this step exists to keep out of a graph.
    {
        RuntimeGraphDraftSession session(context_, program);
        // The wrappers the widenings need, written before anything is rewritten: a variant of the
        // parameter's family names one, and every call through that family reaches it. Each takes
        // the parameter's own parameters and, last, the enum of the family the value belongs to.
        std::set<GraphId> wrappedGraphs;
        for (Wrapper &wrapper : wrappers) {
            const auto fromEnum = enumOfFamily.find(wrapper.from);
            if (fromEnum == enumOfFamily.end() || wrapper.into->members.empty()) {
                continue;
            }
            const auto *intoType =
                static_cast<const ty::FunctionType *>(wrapper.into->members.front()->type);
            if (intoType == nullptr || intoType->code() != TypeCode::Function) {
                continue;
            }
            // The signature is rebuilt rather than replaced, as the captured parameters are: it
            // keeps the names of the parameters, their kinds and the effects it publishes.
            ty::FunctionTypeFactory factory;
            const bool named = intoType->hasArgNames();
            factory.setHasCompileInfo(named);
            factory.setResources(intoType->resources());
            const size_t withCount = intoType->withTypesCount();
            for (size_t i = 0; i < intoType->normTypesCount(); ++i) {
                const std::string paramName =
                    named ? std::string(intoType->argNameAt(withCount + i)) : std::string();
                factory.addNormArg(paramName, intoType->normTypeAt(i), intoType->normIsVarAt(i));
            }
            factory.addNormArg(std::string(), fromEnum->second, false);
            factory.setExitType(intoType->hasExitType() ? intoType->exitType()
                                                        : ty::Type::Void());
            ty::FunctionType *signature = factory.build();
            auto transaction = GraphTransaction::open(
                session.context()->graphIds().allocate(), signature);
            GCGraph *made = writeWrapperGraph(
                transaction->edit(), session, *wrapper.from, signature,
                std::format("{}$wrap", wrapper.into->variants.front().name));
            wrapper.graph = made->id();
            wrappedGraphs.insert(made->id());
            familyOfGraph.emplace(made->id(), wrapper.into);
            for (Variant &variant : wrapper.into->variants) {
                if (variant.tag == wrapper.tag && variant.graph == GraphId::Invalid) {
                    variant.graph = made->id();
                }
            }
        }
        for (GCGraph *graph : session.program()->graphs()) {
            // A graph the pass wrote is already in the form the program is being lowered to.
            if (wrappedGraphs.contains(graph->id())) {
                continue;
            }
            auto &transaction = session.edit(graph);
            auto &draft       = transaction.edit();
            retypeDraft(transaction, types, options);

            const auto known = familyOfGraph.find(graph->id());
            const Family *family =
                known != familyOfGraph.end() ? known->second : nullptr;
            // A function the program takes as a value has its captures as trailing parameters from
            // here on: every call the pass writes passes them.
            const bool captured = family != nullptr && [&] {
                for (const Variant &variant : family->variants) {
                    if (variant.graph == graph->id()) {
                        return !variant.captures.empty();
                    }
                }
                return false;
            }();
            if (captured) {
                parameterizeCaptures(draft);
            }

            // The values this graph builds, and the calls through them.
            const auto &values = valueRefs[toValue(graph->id())];
            const auto &fills  = fillRefs[toValue(graph->id())];
            const auto &sites  = callsites[toValue(graph->id())];

            // What each call through a value hands to the function it names: the values a closure
            // captured, in capture order. Read before the values are rewritten -- a closure's
            // payload is gone once it is a tag.
            std::map<gc_node_ref_t, std::vector<gc_node_ref_t>> capturesOfCall;
            for (const CallSite &site : sites) {
                if (!site.known) {
                    continue;
                }
                const gc_node_ref_t id = draftIdOf(draft, site.ref);
                if (!draft.alive(id)) {
                    continue;
                }
                const auto withs = draft.withInputsOf(id);
                if (withs.empty()) {
                    continue;
                }
                const auto *fill = draft.header(withs.front())->kind == GCNodeKind::Fill
                                       ? graph->nodeBodyAs<GCFillBody>(withs.front())
                                       : nullptr;
                if (fill == nullptr || fill->fillKind != GCFillKind::FunctionClosure) {
                    continue;
                }
                const auto slots   = fill->slots();
                const auto values  = draft.withInputsOf(withs.front());
                std::vector<gc_node_ref_t> captures(slots.size(), kInvalidNodeRef);
                for (size_t k = 0; k < slots.size() && k < values.size(); ++k) {
                    const size_t slot = static_cast<size_t>(slots[k]);
                    if (slot < captures.size()) {
                        captures[slot] = values[k];
                    }
                }
                capturesOfCall.emplace(site.ref, std::move(captures));
            }

            for (gc_node_ref_t ref : values) {
                const gc_node_ref_t id = draftIdOf(draft, ref);
                if (!draft.alive(id)) {
                    continue;
                }
                const bool isClosure = fills.contains(ref);
                GraphId variant      = GraphId::Invalid;
                std::vector<gc_node_ref_t> captures;
                if (isClosure) {
                    const auto norms = graph->normInputsOf(ref);
                    const GCNode *base =
                        norms.empty() ? nullptr : graph->node(norms.front());
                    ::Function *function = base != nullptr ? staticFunctionOf(*graph, *base) : nullptr;
                    if (function == nullptr) {
                        continue;
                    }
                    variant = function->graph()->id();
                    const auto slots =
                        graph->nodeBodyAs<GCFillBody>(ref)->slots();
                    const auto withs = draft.withInputsOf(id);
                    captures.assign(slots.size(), kInvalidNodeRef);
                    for (size_t k = 0; k < slots.size() && k < withs.size(); ++k) {
                        const size_t slot = static_cast<size_t>(slots[k]);
                        if (slot < captures.size()) {
                            captures[slot] = withs[k];
                        }
                    }
                } else {
                    const GCNode *plain = graph->node(ref);
                    // A function the program names as a value: the node *is* the value, and its
                    // graph is the variant. It has no captures, so nothing is taken from the node.
                    if (plain != nullptr && plain->kind == GCNodeKind::Func) {
                        const GCGraph *named = program->calleeOf(*graph, ref);
                        if (named == nullptr) {
                            continue;
                        }
                        variant = named->id();
                    } else {
                        ::Function *function = plain != nullptr ? staticFunctionOf(*graph, *plain)
                                                                : nullptr;
                        if (function == nullptr) {
                            continue;
                        }
                        variant = function->graph()->id();
                    }
                }
                const DraftNodeHeader *header = draft.header(id);
                const Family *owner           = familyAt(header->dataType);
                if (owner == nullptr) {
                    continue;
                }
                const Variant *target = tagOf(*owner, variant);
                if (target == nullptr) {
                    continue;
                }
                const gc_node_ref_t tag = draft.materializeStaticValue(
                    camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(
                        static_cast<camel::core::rtdata::Int64>(target->tag)),
                    ty::Type::Int64());
                std::vector<gc_node_ref_t> operands{tag};
                operands.insert(operands.end(), captures.begin(), captures.end());
                const auto enumType = enumOfFamily.find(owner);
                if (enumType == enumOfFamily.end()) {
                    continue;
                }
                GCOperBody body;
                body.op   = nullptr;
                body.oper = camel::core::OperatorTable::instance().intern(":enum/make");
                DraftNodeInit init = snapshotOf(draft, id);
                init.kind          = GCNodeKind::Oper;
                // The node's type and the slot's type are one answer, and the value being written is
                // the enum: the node's type says what the value is, the slot's says what the frame
                // holds, and the collector traces the frame by the slot. Leaving either at the
                // function type is what made a traced `Variant` be read as a function (the crash at
                // Variant::updateRefs: a value of one form in a slot that declared another).
                init.dataType  = enumType->second;
                init.dataIndex = draft.header(id)->dataIndex;
                init.payload   = payloadOf(&body, sizeof(body));
                init.normInputs = operands;
                init.withInputs = std::span<const gc_node_ref_t>{};
                draft.rewriteNode(id, init);
                ++rewroteValues;
                // The static function under a closure is not a value any more: the closure packed
                // it into a tag. It is dropped rather than left holding a type it no longer has.
                if (isClosure) {
                    const auto norms = graph->normInputsOf(ref);
                    if (!norms.empty()) {
                        const gc_node_ref_t base = draftIdOf(draft, norms.front());
                        if (draft.alive(base) && draft.normUsersOf(base).empty() &&
                            draft.withUsersOf(base).empty() && draft.ctrlUsersOf(base).empty() &&
                            draft.orderUsersOf(base).empty() && draft.syncUsersOf(base).empty()) {
                            draft.eraseNode(base);
                        }
                    }
                }
            }

            // A value of another family handed to a parameter of this one: the parameter's family
            // takes it wrapped in the variant that names the wrapper. The edge itself changes -- a
            // node inserted beside it would leave the value in two forms in one graph, which no
            // later pass can type. The value is rewritten by now, so what is wrapped is the enum.
            for (const WrapEdge &edge : wrapEdges[toValue(graph->id())]) {
                const auto intoFound = familyOfType.find(edge.into->typeId());
                const auto fromFound = familyOfType.find(edge.from->typeId());
                if (intoFound == familyOfType.end() || fromFound == familyOfType.end()) {
                    continue;
                }
                const Family *into = intoFound->second;
                const Family *from = fromFound->second;
                const auto wrapped  = std::find_if(
                    wrappers.begin(), wrappers.end(), [&](const Wrapper &one) {
                        return one.into == into && one.from == from;
                    });
                const auto intoEnum = enumOfFamily.find(into);
                if (wrapped == wrappers.end() || wrapped->graph == GraphId::Invalid ||
                    intoEnum == enumOfFamily.end()) {
                    continue;
                }
                const gc_node_ref_t node  = draftIdOf(draft, edge.node);
                const gc_node_ref_t value = draftIdOf(draft, edge.value);
                if (!draft.alive(node) || !draft.alive(value)) {
                    continue;
                }
                const gc_node_ref_t tagNode = draft.materializeStaticValue(
                    camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(
                        static_cast<camel::core::rtdata::Int64>(wrapped->tag)),
                    ty::Type::Int64());
                const uint32_t region    = draft.nodeRegion(node);
                const uint32_t insertAt  = draft.insertRegion();
                if (region != insertAt) {
                    draft.setInsertRegion(region);
                }
                const gc_node_ref_t made =
                    draft.addOperNode(intoEnum->second, nullptr, ":enum/make");
                const std::vector<gc_node_ref_t> operands{tagNode, value};
                draft.setNormInputs(made, operands);
                if (region != insertAt) {
                    draft.setInsertRegion(insertAt);
                }
                std::vector<gc_node_ref_t> norms(draft.normInputsOf(node).begin(),
                                                 draft.normInputsOf(node).end());
                const auto inNorms = std::find(norms.begin(), norms.end(), value);
                if (inNorms != norms.end()) {
                    *inNorms = made;
                    draft.setNormInputs(node, norms);
                    continue;
                }
                std::vector<gc_node_ref_t> withs(draft.withInputsOf(node).begin(),
                                                 draft.withInputsOf(node).end());
                const auto inWiths = std::find(withs.begin(), withs.end(), value);
                if (inWiths != withs.end()) {
                    *inWiths = made;
                    draft.setWithInputs(node, withs);
                }
            }

            for (const CallSite &site : sites) {
                const gc_node_ref_t id = draftIdOf(draft, site.ref);
                if (!draft.alive(id)) {
                    continue;
                }
                const DraftNodeHeader *header = draft.header(id);
                if (header == nullptr || header->dataType == nullptr) {
                    continue;
                }
                const auto withs = draft.withInputsOf(id);
                if (withs.empty()) {
                    continue;
                }
                // The family is the type of the value the call dispatches on, not the type of what
                // the call returns.
                const DraftNodeHeader *calleeHeader = draft.header(withs.front());
                const Family *owner =
                    calleeHeader != nullptr ? familyAt(calleeHeader->dataType) : nullptr;
                if (owner == nullptr) {
                    continue;
                }
                const std::vector<gc_node_ref_t> arguments(
                    draft.normInputsOf(id).begin(), draft.normInputsOf(id).end());
                const std::vector<gc_node_ref_t> withArguments(
                    withs.begin() + 1, withs.end());

                if (site.known) {
                    const Variant *target = tagOf(*owner, site.variant);
                    if (target == nullptr) {
                        continue;
                    }
                    std::vector<gc_node_ref_t> norm = arguments;
                    if (!target->captures.empty()) {
                        const auto found = capturesOfCall.find(site.ref);
                        if (found == capturesOfCall.end()) {
                            continue;
                        }
                        norm.insert(norm.end(), found->second.begin(), found->second.end());
                    }
                    GCFuncRef callee{.ref = draft.payloadGraphIndexOf(target->graph)};
                    DraftNodeInit init      = snapshotOf(draft, id);
                    init.kind               = GCNodeKind::Func;
                    init.payload            = payloadOf(&callee, sizeof(callee));
                    init.normInputs         = norm;
                    init.withInputs         = withArguments;
                    draft.rewriteNode(id, init);
                    ++rewroteCalls;
                    continue;
                }

                // Only the run time knows the tag: `:enum/tag`, one arm per variant, `JOIN`.
                emitCallThroughValue(
                    draft, *owner, arguments, withArguments, withs.front(), header->dataType,
                    draft.nodeRegion(id), id);
                ++rewroteCalls;
            }
        }
        const Program *done = session.commit();
        if (done == nullptr) {
            // The session refuses to commit the rewrite. That is the pass's own work failing, not a
            // shape of the program: it is raised so the pipeline reports it as `PassInternalError`
            // and names the program, instead of running the program as written and leaving the
            // defect for the next reader of a warning to find.
            throw std::runtime_error(
                "the rewritten program does not commit (a defect of the pass, not of the program)");
        }
        if (!report_) {
            return done;
        }
        os << "DEFUNCTIONALIZE types=" << accepted.size() << " values=" << rewroteValues
           << " calls=" << rewroteCalls << " refused=" << (families.size() - accepted.size())
           << "\n";
        reportRefused();
        for (Family *family : accepted) {
            os << "DEFUNCTIONALIZE type='" << family->members.front()->type->toString() << "' ";
            if (family->variants.size() == 1) {
                os << "variant='" << family->variants.front().name << "'";
            } else {
                std::string names;
                for (const Variant &variant : family->variants) {
                    names += names.empty() ? variant.name : "," + variant.name;
                }
                os << "variants='" << names << "'";
            }
            os << " values=" << family->values << " calls=" << family->calls << "\n";
        }
        return done;
    }
}
