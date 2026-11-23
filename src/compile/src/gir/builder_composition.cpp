// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The interval `a..b` and the composition `f <> g`: two constructs, told apart by the token the
 * program wrote (buildComposition, buildCompositionWith, composeFunctionValue, intervalValue), and
 * the link the pipe operator `|>` makes of the call it moves into (visitPipeLink, yieldToPipe).
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/data/primary.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/fault_isolation.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/other.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <set>
#include <stdexcept>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {
// The names the graph of a composition gives to its parameter and to the two functions it
// captures: no program writes them, and they are inserted in that graph's own scope.
constexpr std::string_view kCompParameter = "$comp_arg";
constexpr std::string_view kCompLhs       = "$comp_f";
constexpr std::string_view kCompRhs       = "$comp_g";
// The built-in `to_range(start, stop, step)`, which is what the interval reading of `..` is: the
// range as a value of the library's own type, not the array of the integers it names (design
// principles 4.2, 5.4). Referred to by the name the compiler writes for a built-in operator, made
// by `core::compilerNamedOperator`, the one place such a name is built.
const std::string kRangeCall = camel::core::compilerNamedOperator("to_range");
} // namespace

node_handle_t
Builder::buildComposition(const GCT::node_ptr_t &gct, Type *expected, bool intervalOnly) {
    // `a..b` is the interval and nothing else (design principles 4.2): `range(a, b, 1)` over two
    // integers, and an operand of any other type is refused by `buildCompositionWith`. It is not
    // a composition with a `..` spelling, so the type this position expects says nothing about
    // its operands.
    // `f <> g` is the composition and nothing else: the lambda `(x) => g(f(x))`, typed exactly as
    // that lambda is typed, so the function type the position expects gives the composed function
    // its input - the parameter of `f` - and its output - the result of `g` - and it instantiates
    // the operands where they stand, as the lambda's two calls are instantiated (design
    // principles 5.9, 5.5).
    FunctionType *expectedFunc = nullptr;
    if (!intervalOnly && expected != nullptr && expected->code() == TypeCode::Function) {
        auto *candidate = tt::as_ptr<FunctionType>(expected);
        // A composed function takes the one argument `f` takes. A position that expects some
        // other signature - two parameters, or a `with` parameter this value cannot carry - says
        // nothing about the operands, and the composition is left to the two of them.
        if (candidate->withTypesCount() == 0 && candidate->normTypesCount() == 1) {
            expectedFunc = candidate;
        }
    }
    // What `f` is expected to be: the one parameter the composed function takes. Its result is
    // deliberately not taken from the expectation: what `f` returns is what `g` takes, and only
    // `f` says what that is (the lambda reads it off the call `f(x)`, not off the declared type).
    FunctionType *lhsExpected = nullptr;
    if (expectedFunc != nullptr) {
        FunctionTypeFactory factory;
        factory.addNormArg(
            std::string(kCompParameter),
            expectedFunc->normTypeAt(0),
            expectedFunc->normIsVarAt(0));
        lhsExpected = factory.build();
    }
    // The two operands are values, and a function written by name is its value here as it is in
    // any argument position. The name of a generic function is instantiated by the type its
    // position gives it, exactly as an argument is (`checkValue`): with no such type the name
    // still stands for no instance, and the refusal below says so by name.
    const auto operandValue = [&](const GCT::node_ptr_t &expr, Type *wanted) {
        ExpectedValueScope scope(expectedValueType_, wanted);
        node_handle_t node = valueNodeOf(visit(expr));
        if (wanted != nullptr && genericDrefs_.contains(node)) {
            node = genericValueFor(node, wanted, expr);
        }
        return argumentNodeOf(node, expr);
    };
    node_handle_t lhs = operandValue(gct->at(1), lhsExpected);
    // What `g` is expected to be: it takes what `f` returns - known now - and gives what the
    // whole composition gives, the result the position expects.
    FunctionType *rhsExpected = nullptr;
    if (expectedFunc != nullptr) {
        Type *lhsBuiltType = nodeTypeOf(lhs);
        if (lhsBuiltType != nullptr && lhsBuiltType->code() == TypeCode::Function) {
            auto *lhsFunc = tt::as_ptr<FunctionType>(lhsBuiltType);
            if (lhsFunc->withTypesCount() == 0 && lhsFunc->normTypesCount() == 1 &&
                lhsFunc->hasExitType()) {
                FunctionTypeFactory factory;
                factory.addNormArg(std::string(kCompParameter), lhsFunc->exitType(), false);
                if (expectedFunc->hasExitType()) {
                    factory.setExitType(expectedFunc->exitType());
                }
                rhsExpected = factory.build();
            }
        }
    }
    node_handle_t rhs = operandValue(gct->at(2), rhsExpected);
    return buildCompositionWith(gct, lhs, rhs, intervalOnly, intervalOnly ? ".." : "<>");
}

bool Builder::isIntervalExpr(const GCT::node_ptr_t &expr) {
    return expr != nullptr && expr->size() == 3 && expr->at(0)->type() == GCT::NodeKind::DREF &&
           expr->at(0)->name() == camel::core::kCompositionCall;
}

node_handle_t Builder::buildCompositionWith(
    const GCT::node_ptr_t &gct, node_handle_t lhs, node_handle_t rhs, bool intervalOnly,
    std::string_view opText) {
    Type *lhsType            = nodeTypeOf(lhs);
    Type *rhsType            = nodeTypeOf(rhs);
    const bool bothFunctions = lhsType != nullptr && rhsType != nullptr &&
                               lhsType->code() == TypeCode::Function &&
                               rhsType->code() == TypeCode::Function;
    // The composition is what `<>` is and nothing else: `..` has no composition reading, so two
    // functions written with `..` are the interval's refusal below, not a composition.
    if (!intervalOnly && bothFunctions) {
        auto *lhsFunc = static_cast<FunctionType *>(lhsType);
        auto *rhsFunc = static_cast<FunctionType *>(rhsType);
        // `f` is unary: the composed function takes the one argument `f` takes (design
        // principles 4.2).
        if (lhsFunc->normTypesCount() != 1 || lhsFunc->withTypesCount() != 0 ||
            !lhsFunc->hasExitType()) {
            diags_->of(SemanticDiag::CompOperandNotUnary)
                .atOrigin(gct->origin())
                .commit(lhsFunc->toString(), std::to_string(lhsFunc->normTypesCount()));
            throw BuildAbortException();
        }
        return composeFunctionValue(lhs, rhs, lhsFunc, rhsFunc, gct);
    }
    // Two integers are the interval `[a, b)` (design principles 4.2). `int` is the language's
    // integer type (`Type::Int()` is `Int64`), and the interval is the array of them. `<>` does
    // not have this reading: an interval is written `a..b`.
    if (intervalOnly && lhsType != nullptr && rhsType != nullptr &&
        lhsType->code() == TypeCode::Int64 && rhsType->code() == TypeCode::Int64) {
        return intervalValue(lhs, rhs, gct);
    }
    // A name of a generic function that neither the position nor the other operand settles: the
    // composition is refused by that name, together with the two ways to settle it (
    // section generic-operands-of-composition). This is not CompOperandMismatch: the operand is
    // not a value of a type the composition cannot take, it is a name with no instance yet. The
    // interval has no such refusal: its operands are integers, and anything else is the mismatch
    // below.
    if (!intervalOnly) {
        for (const bool isLeft : {true, false}) {
            const node_handle_t operandNode = isLeft ? lhs : rhs;
            const auto found                = genericDrefs_.find(operandNode);
            if (found == genericDrefs_.end()) {
                continue;
            }
            const GCT::node_ptr_t &expr        = gct->at(isLeft ? 1 : 2);
            const generic_template_ptr_t &tmpl = found->second.one();
            std::string written                = sourceTextOf(expr);
            if (written.empty() && tmpl != nullptr) {
                written = tmpl->name;
            }
            diags_->of(SemanticDiag::CompOperandGenericWithoutType)
                .atOrigin(gct->origin())
                .commit(opText, isLeft ? "left" : "right", written);
            throw BuildAbortException();
        }
    }
    // The operand's type as the message names it. A value whose type is not settled is not a
    // type the program wrote, and the message says that rather than print `void` as one.
    const auto operandText = [](Type *type) -> std::string {
        if (type == nullptr) {
            return "nothing";
        }
        if (type->code() != TypeCode::Void) {
            return type->toString();
        }
        return "a value with no type yet";
    };
    diags_->of(SemanticDiag::CompOperandMismatch)
        .atOrigin(gct->origin())
        .commit(operandText(lhsType), operandText(rhsType));
    throw BuildAbortException();
}

node_handle_t Builder::composeFunctionValue(
    node_handle_t lhs, node_handle_t rhs, FunctionType *lhsType, FunctionType *rhsType,
    const GCT::node_ptr_t &gct) {
    // The composition is the lambda `(x) => g(f(x))` and nothing else (design principles 4.2):
    // it has the parameter
    // of `f`, the result of `g`, and the two functions as its captures. Its type, its generic
    // instantiation and its overloads are the ones that lambda has, because it is built as one.
    FunctionTypeFactory factory;
    factory.addNormArg(
        std::string(kCompParameter),
        lhsType->normTypeAt(0),
        lhsType->normIsVarAt(0));
    factory.setExitType(rhsType->exitType());
    FunctionType *type            = factory.build();
    compile_graph_ptr_t graph     = enterScope(type);
    const bool outerSynced        = synced_;
    const node_handle_t outerLast = lastSyncedNode_;
    const auto restore            = [&] {
        synced_                  = outerSynced;
        lastSyncedNode_          = outerLast;
        bodyStates_[graph.get()] = BodyState::Built;
    };
    try {
        bodyStates_[graph.get()] = BodyState::Open;
        const auto make          = [&](GCT::node_ptr_t node) {
            node->setOrigin(gct->origin());
            return node;
        };
        const auto nameNode = [&](std::string_view name) {
            return make(tree_->makeDRef(std::string(name)));
        };
        // The parameter's port, and the two functions: a name of an enclosing graph used in a
        // nested one is what a closure captures, and insertNode makes exactly that of them.
        size_t portCount = 0;
        for (draft_node_ref_t port : graph->view().normPorts()) {
            insertNode(std::string(kCompParameter), graph->view().ref(port));
            ++portCount;
        }
        GUARD(portCount == 1, "A composition has exactly one parameter.");
        insertNode(std::string(kCompLhs), lhs);
        insertNode(std::string(kCompRhs), rhs);
        GCT::node_ptr_t inner = make(tree_->makeLink(1));
        *inner << nameNode(kCompLhs) << nameNode(kCompParameter);
        GCT::node_ptr_t outer = make(tree_->makeLink(1));
        *outer << nameNode(kCompRhs) << inner;
        // The call is the value of the function, so it runs whenever the function is called.
        synced_             = true;
        lastSyncedNode_     = {};
        node_handle_t value = visitLinkNode(outer);
        setGraphReturnFromResult(graph, nodeIdOf(value), nodeIdOf(value), nodeTypeOf(value));
        restore();
        registerBuiltBody(graph);
    } catch (...) {
        restore();
        leaveScope();
        throw;
    }
    leaveScope();
    return createFuncDataNode(graph, true, false);
}

node_handle_t
Builder::intervalValue(node_handle_t lhs, node_handle_t rhs, const GCT::node_ptr_t &gct) {
    // `a..b` of two integers is the range from `a` up to, not including, `b` -- left closed and
    // right open, as the slice `t[a..b]` is (design principles 4.2). The value is the library's
    // named type `Range`, not an array (design principles 5.4), and it is the step-1 case of
    // `to_range`, written out as that call with the step: `to_range(a, b, 1)`. `range(a, b, 1)`
    // is the array of the same integers and is built only where one is asked for (`to_array`),
    // so the two spellings are no longer one node.
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(gct->origin());
        return node;
    };
    const auto nameNode = [&](const std::string &name) { return make(tree_->makeDRef(name)); };
    const std::string lhsName = bindTemporary(lhs);
    const std::string rhsName = bindTemporary(rhs);
    GCT::node_ptr_t call      = make(tree_->makeLink(3));
    *call << make(tree_->makeDRef(std::string(kRangeCall))) << nameNode(lhsName)
          << nameNode(rhsName)
          << make(tree_->makeData(
                 tt::as_shared<Data>(std::make_shared<PrimaryData<int64_t>>(int64_t{1}))));
    node_handle_t value = visitLinkNode(call);
    nodeScope_->erase(lhsName, false);
    nodeScope_->erase(rhsName, false);
    return value;
}

node_handle_t
Builder::visitPipeLink(const GCT::node_ptr_t &call, const GCT::node_ptr_t &replacement) {
    const auto unused = unusedStatement_;
    PipeState detect;
    detect.call        = call;
    detect.replacement = replacement;
    detect.previous    = pipe_;
    pipe_              = &detect;
    try {
        node_handle_t built = visitLinkNode(call);
        pipe_               = detect.previous;
        return built;
    } catch (const PipeTagged &) {
        pipe_ = detect.previous;
    } catch (...) {
        pipe_ = detect.previous;
        throw;
    }
    // The receiver is a `T?` or a `T!`. Does an overload take it as it is?
    node_handle_t receiver = detect.receiver;
    bool direct            = true;
    {
        PipeState scratch;
        scratch.call                     = call;
        scratch.receiver                 = receiver;
        scratch.mode                     = PipeMode::Scratch;
        scratch.previous                 = pipe_;
        unusedStatement_                 = nullptr;
        auto &draft                      = currGraph_->transaction().edit();
        const draft_node_ref_t firstNode = static_cast<draft_node_ref_t>(draft.nodeSlotCount());
        // Nothing the dropped arm built stays in the graph: a node of it that had an effect would
        // be reported as one that never runs, and one that did would run.
        const auto dropScratch = [&] {
            for (draft_node_ref_t id = static_cast<draft_node_ref_t>(draft.nodeSlotCount());
                 id > firstNode;) {
                --id;
                if (draft.alive(id)) {
                    draft.eraseNode(id);
                }
            }
        };
        try {
            pipe_ = &scratch;
            buildArm(call, [&] { return visitLinkNode(call); });
        } catch (const PipeNoMatch &) {
            direct = false;
        } catch (...) {
            pipe_ = scratch.previous;
            throw;
        }
        pipe_ = scratch.previous;
        dropScratch();
    }
    unusedStatement_ = unused;
    if (!direct) {
        return peelPipeReceiver(call, receiver);
    }
    PipeState real;
    real.call     = call;
    real.receiver = receiver;
    real.mode     = PipeMode::Direct;
    real.previous = pipe_;
    pipe_         = &real;
    try {
        node_handle_t built = visitLinkNode(call);
        pipe_               = real.previous;
        return built;
    } catch (...) {
        pipe_ = real.previous;
        throw;
    }
}

void Builder::yieldToPipe(const GCT::node_ptr_t &call) {
    if (pipe_ != nullptr && pipe_->call == call && pipe_->mode == PipeMode::Scratch) {
        throw PipeNoMatch();
    }
}

} // namespace camel::compile::gir
