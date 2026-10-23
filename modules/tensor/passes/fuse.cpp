// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The tensor::fuse pass (see fuse.h): it writes the implementation choices of the tensor
 * operators as node attributes and changes no operator.
 */

#include "fuse.h"

#include "index_eval.h"

#include "../attrs.h"

#include "camel/core/node_attrs.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/execute/pass/params.h"
#include "camel/runtime/draft_session.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <format>
#include <string_view>
#include <vector>

namespace camel::tensor::passes {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::GraphDraft;
using camel::runtime::kInvalidNodeRef;
using camel::runtime::RuntimeGraphDraftSession;

namespace {

namespace attr = camel::core::attr;
namespace sem  = camel::core::sem;
namespace tag  = camel::core::tag;

/// The operator of an OPER node, kNoOper for any other node.
camel::core::OperId operOf(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Oper) {
        return camel::core::kNoOper;
    }
    return reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data())->oper;
}

/// The operators the patterns are made of, interned once.
struct PatternOperators {
    camel::core::OperId add    = camel::core::OperatorTable::instance().intern("tensor:add");
    camel::core::OperId matmul = camel::core::OperatorTable::instance().intern("tensor:matmul");
    camel::core::OperId linear = camel::core::OperatorTable::instance().intern("tensor:linear");
    camel::core::OperId relu   = camel::core::OperatorTable::instance().intern("tensor:relu");
};

const PatternOperators &patternOperators() {
    static const PatternOperators operators;
    return operators;
}

/// True when nothing outside the group observes `id`: its value feeds only `next` (as a norm
/// input) and its control users, if any, are `next` as well. The group's nodes stay separate
/// nodes of the graph (identity is preserved), so this is a property of the pattern, not a
/// precondition for merging: a value with another reader would simply not be private to a group.
bool isPrivateIntermediate(const GraphDraft &draft, gc_node_ref_t id, gc_node_ref_t next) {
    const auto normUsers = draft.normUsersOf(id);
    const auto ctrlUsers = draft.ctrlUsersOf(id);
    return normUsers.size() == 1 && normUsers[0] == next && draft.withUsersOf(id).empty() &&
           std::ranges::all_of(ctrlUsers, [&](gc_node_ref_t user) { return user == next; }) &&
           id != draft.outputNode() && id != draft.returnNode() && id != draft.exitNode();
}

/// Writes `text` (a Symbol) under `key` on `id`; false when the set already said that.
bool setSymbol(GraphDraft &draft, gc_node_ref_t id, attr::AttrKey key, std::string_view text) {
    const attr::AttrSetId before = draft.nodeAttrs(id);
    const attr::AttrSetId after  = attr::unite(before, attr::single(key, attr::symbol(text)));
    if (after == before) {
        return false;
    }
    draft.setNodeAttrs(id, after);
    return true;
}

/// The 7.3 decision for one matmul, as the core states it: the node's effective permissions
/// under this stage's policy. `reassociate` covers the library GEMM's own order and grouping of
/// the accumulation (a separately rounded multiply and add included, 2.17); nothing else is
/// needed. Implementation choices do not change the operator (9.6), so this writes an
/// attribute, not another operator.
std::string_view kernelChoice(const GraphDraft &draft, gc_node_ref_t id, const sem::NumericPolicy &policy) {
    const sem::PermissionMask effective = tag::effectivePermissions(draft, id, policy);
    return (effective & sem::kReassociate) != 0 ? kLibraryKernel : kCanonicalKernel;
}

/// Writes tensor.kernel on every tensor:matmul and tensor:linear of `draft`. `linear` keeps the
/// choice for its gradient: the forward kernel is the canonical x @ w + b, while the backward
/// matmuls it emits are the same GEMMs a matmul gradient runs, so the node records which licence
/// the rule may carry. Returns how many nodes it changed and counts the choices, so a run says
/// how many products run each way.
size_t markKernels(
    GraphDraft &draft, const sem::NumericPolicy &policy, size_t &library, size_t &canonical) {
    const PatternOperators &ops = patternOperators();
    size_t changed              = 0;
    const gc_node_ref_t slots   = draft.nodeSlotCount();
    for (gc_node_ref_t id = 0; id < slots; ++id) {
        const camel::core::OperId oper = operOf(draft, id);
        if (oper != ops.matmul && oper != ops.linear) {
            continue;
        }
        const std::string_view choice = kernelChoice(draft, id, policy);
        if (choice == kLibraryKernel) {
            ++library;
        } else {
            ++canonical;
        }
        if (setSymbol(draft, id, kernelAttrKey(), choice)) {
            ++changed;
        }
    }
    return changed;
}

/// Marks one group: `matmul` and `add` (and `relu` when it is its private reader) carry
/// tensor.fusion with the same value. False when a node already belongs to another group or all
/// of them already carry this value (a second run writes nothing).
bool markGroup(
    GraphDraft &draft, gc_node_ref_t matmul, gc_node_ref_t add, gc_node_ref_t relu,
    std::string_view fusion) {
    const attr::AttrKey key = fusionAttrKey();
    for (gc_node_ref_t member : {matmul, add, relu}) {
        if (member == kInvalidNodeRef) {
            continue;
        }
        const attr::AttrValueId value = attr::valueOf(draft.nodeAttrs(member), key);
        if (value != attr::kNoValue && attr::value(value).text != fusion) {
            return false;
        }
    }
    bool changed = false;
    for (gc_node_ref_t member : {matmul, add, relu}) {
        if (member != kInvalidNodeRef) {
            changed |= setSymbol(draft, member, key, fusion);
        }
    }
    return changed;
}

/// Marks every `x @ w + b` / `relu(x @ w + b)` group whose product nothing else observes.
/// Returns the number of groups written (a second run finds them marked and writes none).
size_t markGroups(GraphDraft &draft) {
    const PatternOperators &ops = patternOperators();
    const gc_node_ref_t slots   = draft.nodeSlotCount();
    size_t groups               = 0;
    for (gc_node_ref_t add = 0; add < slots; ++add) {
        if (operOf(draft, add) != ops.add) {
            continue;
        }
        const auto inputs = draft.normInputsOf(add);
        if (inputs.size() != 2) {
            continue;
        }
        // Float addition is commutative, so the product may be either operand; the first one
        // that is a private product decides the group (an add is the root of one group).
        gc_node_ref_t matmul = kInvalidNodeRef;
        for (size_t side = 0; side < 2; ++side) {
            const gc_node_ref_t candidate = inputs[side];
            if (operOf(draft, candidate) == ops.matmul &&
                draft.normInputsOf(candidate).size() == 2 &&
                isPrivateIntermediate(draft, candidate, add)) {
                matmul = candidate;
                break;
            }
        }
        if (matmul == kInvalidNodeRef) {
            continue;
        }
        gc_node_ref_t relu      = kInvalidNodeRef;
        std::string_view fusion = kLinearFusion;
        const auto users        = draft.normUsersOf(add);
        if (users.size() == 1 && operOf(draft, users[0]) == ops.relu &&
            isPrivateIntermediate(draft, add, users[0])) {
            relu   = users[0];
            fusion = kLinearReluFusion;
        }
        if (markGroup(draft, matmul, add, relu, fusion)) {
            ++groups;
        }
    }
    return groups;
}

} // namespace

const camel::runtime::Program *
TensorFusePass::apply(const camel::runtime::Program *program, std::ostream &) {
    // The stage's policy: the 7.3 defaults, or the canonical algorithms in safe mode (7.4).
    const sem::NumericPolicy policy =
        safeFp_ ? sem::NumericPolicy::strict() : sem::NumericPolicy::relaxed();
    RuntimeGraphDraftSession session(context_, program);
    size_t changed = 0, library = 0, canonical = 0, groups = 0;
    for (GCGraph *g : session.collectReachableRuntimeGraphs()) {
        GraphDraft &draft = session.edit(g);
        changed += markKernels(draft, policy, library, canonical);
        groups += markGroups(draft);
    }
    changed += groups;
    CAMEL_LOG_INFO_S(
        "tensor::fuse",
        "{} matmul(s) on the library GEMM, {} on the canonical algorithm, {} fused group(s){}",
        library,
        canonical,
        groups,
        safeFp_ ? " (safe mode)" : "");
    if (changed == 0) {
        return program;
    }
    return session.commit();
}

namespace {

/// A test of pass parameters and of the default pipeline (test/plans/feat/opt/pass_params): it
/// prints the parameters it received and changes nothing.
class ParamEchoTestPass : public GraphIRPass {
  public:
    ParamEchoTestPass(const core::context::context_ptr_t &ctx, PassArgs args)
        : GraphIRPass(ctx), args_(std::move(args)) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << std::format(
            "test_echo: n={} ratio={} label={} flag={}\n",
            args_.getInt("n"),
            args_.getFloat("ratio"),
            args_.getString("label"),
            args_.getBool("flag"));
        return program;
    }

  private:
    PassArgs args_;
};

void registerParamEchoTestPass() {
    PassDescriptor descriptor;
    // Documentation and parameters are the declaration in stdlib/tensor.cml.
    descriptor.params = {
        PassParam{"n", PassParamType::Int, int64_t{1}, "an int"},
        PassParam{"ratio", PassParamType::Float, 0.5, "a float"},
        PassParam{"label", PassParamType::String, std::string("echo"), "a string"},
        PassParam{"flag", PassParamType::Bool, false, "a bool"},
    };
    descriptor.make = [](const core::context::context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<ParamEchoTestPass>(ctx, args);
    };
    registerModulePass("tensor::test_echo", std::move(descriptor));
    registerPassAlias("tensor::echo", "tensor::test_echo");
}

} // namespace

void registerTensorPasses() {
    PassDescriptor descriptor;
    // The pass declares `safe_fp` in its module declaration (stdlib/tensor.cml).
    descriptor.params = {PassParam{
        "safe_fp",
        PassParamType::Bool,
        false,
        "keep the matmul on the canonical algorithm (design principles 7.4); the default lets a "
        "matmul the 7.3 rule permits use the library GEMM"}};
    descriptor.readsTags = true;
    descriptor.make       = [](const core::context::context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<TensorFusePass>(ctx, args.getBool("safe_fp"));
    };
    registerModulePass("tensor::fuse", std::move(descriptor));
    // The declaration evaluator is a test (index_eval.h): it is reachable by name and in no default
    // pipeline, which is the whole switch (H81 removed the environment variable that gated it).
    registerModulePass("tensor::test_index_eval", [](const core::context::context_ptr_t &ctx) {
        return std::make_unique<IndexEvalTestPass>(ctx);
    });
    registerParamEchoTestPass();
}

} // namespace camel::tensor::passes
