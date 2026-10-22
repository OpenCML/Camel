// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The rewrite module: rules as values, and the engines that apply them to a program.
 *
 * `rule name(params) { left => right }` is read as two functions of the same parameters and
 * `let name = __rule('name', ...)` (graph store 5.2: a rule is the shorthand of a pair of
 * functions; rewrite design 2 (second version) 8.5: the rule is a value); the front end writes
 * the import of this module itself. The engines, `rewrite`, `run`, `saturate` and `extract`, take
 * the rules as an array.
 *
 * The callable operators are declared in `stdlib/rewrite.cml` (handover H183; `run`, H226); this
 * file keeps their kernels, the executor and their purity. The three reserved groups the
 * language's
 * `rule` and `analysis` declarations compile to (`__rule`, `__rule_when`, `__analysis`) have no
 * callable Camel name, so they stay exported from here.
 */

#include "rule.h"

#include "analysis.h"

#include "../graph/handle.h"

#include "camel/core/context/context.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/module/builtin.h"
#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "camel/runtime/core_facts.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/program.h"
#include "camel/runtime/rules.h"
#include "camel/runtime/writings.h"
#include "src/passes/sched/nodevm/nodevm.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <random>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::rtdata;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::graphmod::Handle;
using camel::graphmod::HandleKind;
using camel::graphmod::HandleType;
using camel::rewritemod::RuleObject;
using camel::rewritemod::RuleType;

namespace {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;

slot_t ruleKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto name = camel::core::internSymbol(norm.get<String *>(0)->view());
    auto *object    = RuleObject::create(
        name,
        norm.get<rtdata::Bool>(1),
        static_cast<camel::core::sem::PermissionMask>(norm.get<rtdata::Int64>(2)),
        static_cast<uint64_t>(norm.get<rtdata::Int64>(3)),
        norm.get<::Function *>(4),
        norm.get<::Function *>(5),
        nullptr,
        mm::autoSpace());
    return toSlot(object);
}

slot_t ruleWhenKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto name = camel::core::internSymbol(norm.get<String *>(0)->view());
    auto *object    = RuleObject::create(
        name,
        norm.get<rtdata::Bool>(1),
        static_cast<camel::core::sem::PermissionMask>(norm.get<rtdata::Int64>(2)),
        static_cast<uint64_t>(norm.get<rtdata::Int64>(3)),
        norm.get<::Function *>(4),
        norm.get<::Function *>(5),
        norm.get<::Function *>(6),
        mm::autoSpace());
    return toSlot(object);
}

slot_t nameKernel(ArgsView &, ArgsView &norm, Context &) {
    return toSlot(String::from(std::string(norm.get<RuleObject *>(0)->name()), mm::autoSpace()));
}

slot_t twoWayKernel(ArgsView &, ArgsView &norm, Context &) {
    return toSlot<rtdata::Bool>(norm.get<RuleObject *>(0)->twoWay());
}

slot_t needsKernel(ArgsView &, ArgsView &norm, Context &) {
    return toSlot<rtdata::Int64>(norm.get<RuleObject *>(0)->needs());
}

slot_t constsKernel(ArgsView &, ArgsView &norm, Context &) {
    return toSlot<rtdata::Int64>(static_cast<int64_t>(norm.get<RuleObject *>(0)->constParams()));
}

/// The state a condition closure needs: the rule object, rooted so that the closure can read the
/// condition function from it after every safepoint, and the node VM the conditions of one engine
/// call share.
struct WhenCall {
    std::shared_ptr<mm::RootHandle> rule;
    std::shared_ptr<NodeVMSchedPass> vm;
};

/// Whether a rule's condition holds at this match. Its parameters are Node handles of the draft
/// being rewritten, one per parameter of the rule; a parameter the left side does not use names
/// no node and faults when it is read.
bool callWhen(const WhenCall &call, const camel::runtime::Match &match) {
    const size_t arity = static_cast<size_t>(
        call.rule->getAs<RuleObject>()->when()->graph()->funcType()->normTypesCount());
    camel::runtime::GraphDraft &draft = match.writings.draft();
    std::vector<std::unique_ptr<mm::RootHandle>> roots;
    std::vector<slot_t> args;
    roots.reserve(arity);
    args.reserve(arity);
    for (size_t i = 0; i < arity; ++i) {
        const camel::runtime::gc_node_ref_t ref =
            i < match.vars.size() ? match.vars[i] : camel::runtime::kInvalidNodeRef;
        Handle *handle = Handle::createInDraft(draft, ref, mm::autoSpace());
        roots.push_back(
            std::make_unique<mm::RootHandle>(
                mm::autoSpace(),
                handle,
                HandleType::get(HandleKind::Node),
                "rewrite.whenArg"));
    }
    // Making a handle can collect: the arguments are read from the roots after the last one, so
    // each slot names the handle where the collector put it.
    for (const auto &root : roots) {
        args.push_back(toSlot(root->getAs<Handle>()));
    }
    // A call can move the objects: the condition is read from the rooted rule after the last
    // allocation and immediately before the call.
    ::Function *when    = call.rule->getAs<RuleObject>()->when();
    const slot_t result = call.vm->callGraph(when->program(), when->graph(), args);
    return fromSlot<rtdata::Bool>(result);
}

/// What a rule object says, as the compiler of rules takes it. `call` is set when the rule has a
/// condition; the closure shares it, and with it the rooted rule object.
camel::runtime::RuleSource
sourceOf(const RuleObject &rule, uint16_t id, const std::shared_ptr<WhenCall> &call) {
    camel::runtime::RuleSource source;
    source.name          = std::string(rule.name());
    source.id            = id;
    source.needs         = rule.needs();
    source.bidirectional = rule.twoWay();
    source.program       = rule.lhs()->program();
    source.lhs           = rule.lhs()->graphId();
    source.rhs           = rule.rhs()->graphId();
    for (int bit = 0; bit < 64; ++bit) {
        source.constParams.push_back((rule.constParams() >> bit) & 1);
    }
    if (rule.when() != nullptr) {
        source.whenArity =
            static_cast<uint32_t>(rule.when()->graph()->funcType()->normTypesCount());
        source.when = [call](const camel::runtime::Match &match) { return callWhen(*call, match); };
    }
    return source;
}

/// The engine's rules for the rule values of an array.
///
/// Compiling a rule makes constants and can collect: the array is read again from the argument,
/// which the collector keeps up to date, each time.
std::vector<camel::runtime::Rule> engineRules(Context &ctx, ArgsView &norm, size_t argument) {
    std::vector<camel::runtime::Rule> out;
    auto vm            = std::make_shared<NodeVMSchedPass>(ctx.shared_from_this());
    uint16_t next      = 1;
    const size_t count = norm.get<Array *>(argument)->size();
    for (size_t i = 0; i < count; ++i) {
        RuleObject *rule = norm.get<Array *>(argument)->get<RuleObject *>(i);
        std::shared_ptr<WhenCall> call;
        if (rule->when() != nullptr) {
            call       = std::make_shared<WhenCall>();
            call->rule = std::make_shared<mm::RootHandle>(
                mm::autoSpace(),
                rule,
                RuleType::get(),
                "rewrite.rule");
            call->vm = vm;
        }
        const camel::runtime::RuleSource source = sourceOf(*rule, next++, call);
        for (camel::runtime::Rule &compiled : camel::runtime::compileRule(ctx, source)) {
            out.push_back(std::move(compiled));
        }
    }
    return out;
}

/// Whether a graph is one side of a rule: those are the patterns, not part of the program the
/// rules are applied to.
bool isRuleSide(const camel::runtime::GCGraph &graph) {
    const std::string &name = graph.name();
    return name.ends_with("__lhs") || name.ends_with("__rhs") || name.ends_with("__when");
}

/// One application of rules: the program it produced and what the engine counted while applying
/// them (the rewrites and the nodes are summed over the graphs; the rounds are the rounds the
/// whole application made, each graph advancing in the same round).
struct Application {
    const camel::runtime::Program *program = nullptr;
    camel::runtime::RewriteStats stats;
};

/// Applies `compiled` one way, in place, to a fixpoint over at most `maxRounds` rounds, to every
/// graph of `base` that is not a side of a rule. A condition is Camel code, and so is a safepoint:
/// every draft the session has opened must keep its static values relocatable until the commit
/// reads them again, not only the one being rewritten now.
Application applyRewrite(
    Context &ctx, const camel::runtime::Program *base,
    std::span<const camel::runtime::Rule> compiled, camel::core::sem::NumericPolicy policy,
    size_t maxRounds) {
    camel::runtime::RuntimeGraphDraftSession session(ctx.shared_from_this(), base);
    camel::runtime::RuntimeGraphDraftSession::RootScope draftRoots(session);
    camel::runtime::RewriteStats total;
    total.fixpoint = true;
    for (camel::runtime::GCGraph *graph : base->graphs()) {
        if (isRuleSide(*graph)) {
            continue;
        }
        camel::runtime::GraphDraft &draft = session.edit(graph);
        camel::runtime::DraftWritings writings(draft, policy);
        const camel::runtime::RewriteStats stats = writings.rewrite(compiled, maxRounds);
        total.rounds = std::max(total.rounds, stats.rounds);
        total.visited += stats.visited;
        total.applied += stats.applied;
        total.erased += stats.erased;
        total.fixpoint = total.fixpoint && stats.fixpoint;
    }
    return {session.commitChecked(), total};
}

/// `rewrite(p, rules)` and `rewrite(p, rules, strict)`: the program with the rules applied one way,
/// in place, to a fixpoint (rewrite design 3). The third parameter chooses the numeric policy of
/// the pass itself (design principles 7.4, 7.5): `strict` grants no permission, as
/// `std::opt(safe_fp=true)` does, so only the laws that need none fire.
/// An error value when a rule cannot be a rule, holds both ways, or the result is not a program.
slot_t rewriteKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const TupleType *resultType = TupleType::fallible(norm.type(0));
    const auto *base            = norm.get<const camel::runtime::Program *>(0);
    const bool strict           = norm.size() > 2 && norm.get<Bool>(2);
    try {
        const auto compiled = engineRules(ctx, norm, 1);
        const Application application = applyRewrite(
            ctx, base, compiled,
            strict ? camel::core::sem::NumericPolicy::strict()
                   : camel::core::sem::NumericPolicy::relaxed(),
            16);
        return makeOk(
            toSlot(const_cast<camel::runtime::Program *>(application.program)), resultType);
    } catch (const camel::runtime::RuleCompileError &e) {
        return makeError("RuleInvalid", e.what(), "<rewrite::rewrite>");
    } catch (const std::invalid_argument &e) {
        return makeError("RuleTwoWay", e.what(), "<rewrite::rewrite>");
    } catch (const mm::MemoryExhausted &) {
        throw;
    } catch (const std::exception &e) {
        return makeError("RewriteInvalid", e.what(), "<rewrite::rewrite>");
    }
}

/// `run(p, rules, rounds, strict)`: one application of the rules, with what it cost (H226). It
/// answers the `Run<Program>` that `stdlib/rewrite.cml` declares: a struct with the program, the
/// rewrites applied, the rounds, the nodes visited, the nodes erased, whether the last round
/// reached a fixpoint, and the wall time in seconds. The fields are built in the declaration's
/// order, which is the only place their names and positions are decided together with `Run`.
slot_t runKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const auto *base     = norm.get<const camel::runtime::Program *>(0);
    const int64_t rounds = norm.get<Int64>(2);
    const bool strict    = norm.size() > 3 && norm.get<Bool>(3);
    StructTypeFactory factory;
    factory.add("result", norm.type(0));
    factory.add("rewrites", Type::Int());
    factory.add("rounds", Type::Int());
    factory.add("visited", Type::Int());
    factory.add("erased", Type::Int());
    factory.add("fixpoint", Type::Bool());
    factory.add("seconds", Type::Float64());
    StructType *runType         = factory.build();
    const TupleType *resultType = TupleType::fallible(runType);
    try {
        const auto start    = std::chrono::steady_clock::now();
        const auto compiled = engineRules(ctx, norm, 1);
        const Application application = applyRewrite(
            ctx, base, compiled,
            strict ? camel::core::sem::NumericPolicy::strict()
                   : camel::core::sem::NumericPolicy::relaxed(),
            rounds < 0 ? 0 : static_cast<size_t>(rounds));
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        // The struct is rooted while the fallible box is made: that allocation can collect.
        Struct *run = Struct::create(7, mm::autoSpace());
        run->set(0, toSlot(const_cast<camel::runtime::Program *>(application.program)));
        run->set(1, toSlot<Int64>(static_cast<int64_t>(application.stats.applied)));
        run->set(2, toSlot<Int64>(static_cast<int64_t>(application.stats.rounds)));
        run->set(3, toSlot<Int64>(static_cast<int64_t>(application.stats.visited)));
        run->set(4, toSlot<Int64>(static_cast<int64_t>(application.stats.erased)));
        run->set(5, toSlot<Bool>(application.stats.fixpoint));
        run->set(6, toSlot<Float64>(seconds));
        mm::RootHandle root(mm::autoSpace(), run, runType, "rewrite.run");
        return makeOk(toSlot(root.getAs<Struct>()), resultType);
    } catch (const camel::runtime::RuleCompileError &e) {
        return makeError("RuleInvalid", e.what(), "<rewrite::run>");
    } catch (const std::invalid_argument &e) {
        return makeError("RuleTwoWay", e.what(), "<rewrite::run>");
    } catch (const mm::MemoryExhausted &) {
        throw;
    } catch (const std::exception &e) {
        return makeError("RewriteInvalid", e.what(), "<rewrite::run>");
    }
}

/// `saturate(p, rules, rounds)` and `saturate(p, rules, rounds, strict)`: the program with the
/// rules applied both ways to the writings of every value, adding writings until nothing new is
/// found or `rounds` rounds were made (rewrite design 3, graph store 3.5). The last parameter is
/// the pass's numeric policy, as for `rewrite`. The result has values with several writings:
/// `extract` (or the pipeline, before a pass that runs or translates it) chooses one.
slot_t saturateKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const TupleType *resultType = TupleType::fallible(norm.type(0));
    const auto *base            = norm.get<const camel::runtime::Program *>(0);
    const int64_t rounds        = norm.get<Int64>(2);
    const bool strict           = norm.size() > 3 && norm.get<Bool>(3);
    try {
        const auto compiled = engineRules(ctx, norm, 1);
        camel::runtime::RuntimeGraphDraftSession session(ctx.shared_from_this(), base);
        session.setGraphState(camel::runtime::GraphState::Tabled);
        // A condition is Camel code, and so is a safepoint: every draft this session has opened
        // must keep its static values relocatable until the commit reads them again, not only the
        // one the engine is saturating now.
        camel::runtime::RuntimeGraphDraftSession::RootScope draftRoots(session);
        camel::runtime::Budget budget;
        budget.rounds = rounds < 0 ? 0 : static_cast<size_t>(rounds);
        for (camel::runtime::GCGraph *graph : base->graphs()) {
            if (isRuleSide(*graph)) {
                continue;
            }
            camel::runtime::GraphDraft &draft = session.edit(graph);
            camel::runtime::DraftWritings writings(
                draft,
                strict ? camel::core::sem::NumericPolicy::strict()
                       : camel::core::sem::NumericPolicy::relaxed());
            writings.build();
            writings.saturate(compiled, budget);
            writings.save();
        }
        const camel::runtime::Program *committed = session.commitChecked();
        return makeOk(toSlot(const_cast<camel::runtime::Program *>(committed)), resultType);
    } catch (const camel::runtime::RuleCompileError &e) {
        return makeError("RuleInvalid", e.what(), "<rewrite::saturate>");
    } catch (const mm::MemoryExhausted &) {
        throw;
    } catch (const std::exception &e) {
        return makeError("RewriteInvalid", e.what(), "<rewrite::saturate>");
    }
}

/// `extract(p, cost)`: one writing for every value, chosen by the cost model of that name.
slot_t extractKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const TupleType *resultType = TupleType::fallible(norm.type(0));
    const auto *base            = norm.get<const camel::runtime::Program *>(0);
    const std::string cost(norm.get<String *>(1)->view());
    const auto model = camel::runtime::findCostModel(cost);
    if (!model) {
        return makeError(
            "NoCostModel",
            "There is no cost model '" + cost + "'.",
            "<rewrite::extract>");
    }
    try {
        const camel::runtime::Program *extracted =
            camel::runtime::extractWritings(ctx.shared_from_this(), base, *model);
        return makeOk(toSlot(const_cast<camel::runtime::Program *>(extracted)), resultType);
    } catch (const mm::MemoryExhausted &) {
        throw;
    } catch (const std::exception &e) {
        return makeError("RewriteInvalid", e.what(), "<rewrite::extract>");
    }
}

// ---- the check of a rule (graph store decision 5) ----

/// A tensor sample: its elements as bytes, so it outlives the calls that follow. A collection may
/// move the Array a value was made in, and the next call reads the sample again; the copy has no
/// references into the GC heap.
struct TensorSample {
    TypeCode dtype = TypeCode::Float32;
    std::vector<int64_t> shape;
    std::vector<uint8_t> bytes;
};

/// A value of a parameter type, as the operators read it. `tensor` is set for a tensor type, whose
/// slot is made afresh right before each call (`Sampler::materialize`).
struct Sample {
    TypeCode code       = TypeCode::Void;
    slot_t slot         = 0;
    double asDouble     = 0.0;
    std::shared_ptr<TensorSample> tensor;
};

/// The element type a tensor dtype holds.
const Type *tensorElementType(TypeCode dtype) {
    switch (dtype) {
    case TypeCode::Float32:
        return Type::Float32();
    case TypeCode::Int64:
        return Type::Int64();
    case TypeCode::Bool:
        return Type::Bool();
    default:
        return nullptr;
    }
}

/// Random values of one parameter type (the scalar ones and the tensors the shape reader knows).
/// Floats include the special values the permissions of the rule do not rule out.
class Sampler {
  public:
    Sampler(uint64_t seed, camel::core::sem::PermissionMask needs) : rng_(seed), needs_(needs) {}

    Sample value(const Type *type, bool constant) {
        const auto shape = camel::runtime::fact::shapeOfType(type);
        if (shape) {
            return tensor(*shape, constant);
        }
        switch (type->code()) {
        case TypeCode::Int64: {
            const int64_t v = pickInt(constant);
            return {TypeCode::Int64, toSlot<Int64>(v), static_cast<double>(v), nullptr};
        }
        case TypeCode::Int32: {
            const int32_t v = static_cast<int32_t>(pickInt(constant));
            return {TypeCode::Int32, toSlot<Int32>(v), static_cast<double>(v), nullptr};
        }
        case TypeCode::Float64: {
            const double v = pickFloat(constant, TypeCode::Float64);
            return {TypeCode::Float64, toSlot<Float64>(v), v, nullptr};
        }
        case TypeCode::Float32: {
            const float v = static_cast<float>(pickFloat(constant, TypeCode::Float32));
            return {TypeCode::Float32, toSlot<Float32>(v), static_cast<double>(v), nullptr};
        }
        case TypeCode::Bool: {
            const bool v = (rng_() & 1) != 0;
            return {TypeCode::Bool, toSlot<Bool>(v), v ? 1.0 : 0.0, nullptr};
        }
        default:
            throw std::invalid_argument("a parameter of a rule has a type the check cannot make");
        }
    }

    /// The sample as an argument slot. A scalar keeps its slot; a tensor is an Array made now,
    /// from the bytes of the sample. Call it right before the call that reads the argument: no
    /// allocation may run in between (the Array can move).
    slot_t materialize(Sample &sample) {
        if (sample.tensor == nullptr) {
            return sample.slot;
        }
        const TensorSample &t = *sample.tensor;
        const Type *element   = tensorElementType(t.dtype);
        if (element == nullptr) {
            throw std::invalid_argument("a tensor parameter has an element type the check cannot make");
        }
        auto *arrayType = ArrayType::create(const_cast<Type *>(element));
        Array *array    = Array::create(mm::autoSpace(), arrayType, t.shape, true);
        if (array->byteSize() != t.bytes.size()) {
            throw std::logic_error("a tensor sample and its array have different sizes");
        }
        if (!t.bytes.empty()) {
            std::memcpy(array->dataAs<uint8_t>(), t.bytes.data(), t.bytes.size());
        }
        sample.slot = toSlot(array);
        return sample.slot;
    }

  private:
    /// A tensor of the type's element type. The shape is the one the type carries; an unknown
    /// rank is a vector of three and an unknown extent is two. A type with a large known shape is
    /// sampled in the same rank at two elements per extent (elementwise laws do not depend on
    /// the extents, and the check stays small).
    Sample tensor(const camel::runtime::fact::TensorShape &shape, bool constant) {
        auto sample  = std::make_shared<TensorSample>();
        sample->dtype = shape.dtype.value_or(TypeCode::Float32);
        const TypeCode dtype = sample->dtype;
        const Type *element  = tensorElementType(dtype);
        if (element == nullptr) {
            throw std::invalid_argument(
                "a tensor parameter has an element type the check cannot make");
        }
        std::vector<int64_t> dims;
        if (shape.dims) {
            dims = *shape.dims;
        } else {
            dims = {3};
        }
        int64_t elements = 1;
        for (int64_t &extent : dims) {
            if (extent < 0) {
                extent = 2;
            }
            elements *= extent;
        }
        if (elements > 1024) {
            for (int64_t &extent : dims) {
                extent = 2;
            }
            elements = 1;
            for (int64_t extent : dims) {
                elements *= extent;
            }
        }
        sample->shape = dims;
        switch (dtype) {
        case TypeCode::Float32: {
            // The bytes of a vector of bytes are not aligned for a float: fill a typed buffer and
            // copy it in (the same on the way out, `tensorElement`).
            std::vector<float> values(static_cast<size_t>(elements));
            for (float &value : values) {
                value = static_cast<float>(pickFloat(false, TypeCode::Float32));
            }
            sample->bytes.resize(values.size() * sizeof(float));
            std::memcpy(sample->bytes.data(), values.data(), sample->bytes.size());
            break;
        }
        case TypeCode::Int64: {
            std::vector<int64_t> values(static_cast<size_t>(elements));
            for (int64_t &value : values) {
                value = pickInt(false);
            }
            sample->bytes.resize(values.size() * sizeof(int64_t));
            std::memcpy(sample->bytes.data(), values.data(), sample->bytes.size());
            break;
        }
        case TypeCode::Bool: {
            sample->bytes.resize(static_cast<size_t>(elements));
            for (int64_t i = 0; i < elements; ++i) {
                sample->bytes[static_cast<size_t>(i)] = (rng_() & 1) != 0 ? 1 : 0;
            }
            break;
        }
        default:
            throw std::invalid_argument(
                "a tensor parameter has an element type the check cannot make");
        }
        Sample result;
        result.code   = shape.dtype.value_or(TypeCode::Float32);
        result.tensor = std::move(sample);
        return result;
    }

    int64_t pickInt(bool constant) {
        static const int64_t edges[] = {
            0,
            1,
            -1,
            2,
            3,
            7,
            -5,
            100,
            255,
            1000,
            -1000,
            std::numeric_limits<int32_t>::max(),
            std::numeric_limits<int32_t>::min()};
        const uint64_t roll = rng_() % 8;
        if (roll < 4) {
            return edges[rng_() % (sizeof(edges) / sizeof(edges[0]))];
        }
        const int64_t range = constant ? 64 : 100000;
        return static_cast<int64_t>(rng_() % (2 * range + 1)) - range;
    }

    double pickFloat(bool constant, TypeCode code) {
        const bool noNans  = (needs_ & camel::core::sem::kNoNans) != 0;
        const bool noInfs  = (needs_ & camel::core::sem::kNoInfs) != 0;
        const bool noZeros = (needs_ & camel::core::sem::kNoSignedZeros) != 0;
        std::vector<double> specials{0.0, 1.0, -1.0, 2.0, -2.0, 0.5, -0.5, 3.0, -3.0, 4.0, 0.1};
        // A special has to be a value of the parameter's own type: 1e300 is finite as a float64
        // and an infinity as a float32, which would smuggle an infinity past `no_infs` and make
        // every float32 rule that assumes finiteness fail its own check.
        if (code == TypeCode::Float32) {
            specials.push_back(1e-30);
            specials.push_back(1e30);
            specials.push_back(1e-40); // subnormal
        } else {
            specials.push_back(1e-300);
            specials.push_back(1e300);
            specials.push_back(1e-310); // subnormal
        }
        if (!noZeros) {
            specials.push_back(-0.0);
        }
        if (!noInfs && !constant) {
            specials.push_back(std::numeric_limits<double>::infinity());
            specials.push_back(-std::numeric_limits<double>::infinity());
        }
        if (!noNans && !constant) {
            specials.push_back(std::numeric_limits<double>::quiet_NaN());
        }
        const uint64_t roll = rng_() % 8;
        if (roll < 3) {
            return specials[rng_() % specials.size()];
        }
        if (constant) {
            // A coefficient a law reads is normally a value its element type holds exactly, and
            // the guards of the coefficient laws are about those values. Small integers are exact
            // in every element type; the inexact specials above still reach the guards.
            return static_cast<double>(static_cast<int64_t>(rng_() % 129) - 64);
        }
        const double range = 1000.0;
        return (static_cast<double>(rng_() % 2000001) / 1000000.0 - 1.0) * range;
    }

    std::mt19937_64 rng_;
    camel::core::sem::PermissionMask needs_;
};

/// Whether two float results agree: NaN agrees with NaN, and otherwise bit for bit when the rule
/// needs no permission, or within the numeric contract below when it does.
bool agreeFloat(double x, double y, bool single, const camel::core::sem::RewriteErrorBound &bound,
    double scale) {
    if (std::isnan(x) || std::isnan(y)) {
        return std::isnan(x) && std::isnan(y);
    }
    if (!bound.checkable) {
        return true; // no contract to compare against (reported as unchecked)
    }
    if (bound.ulps == 0.0 && bound.relative == 0.0) {
        return single ? std::bit_cast<uint32_t>(static_cast<float>(x)) ==
                                std::bit_cast<uint32_t>(static_cast<float>(y)) &&
                            std::signbit(x) == std::signbit(y)
                      : std::bit_cast<uint64_t>(x) == std::bit_cast<uint64_t>(y);
    }
    if (std::isinf(x) || std::isinf(y)) {
        return x == y;
    }
    const double eps       = single ? 1.2e-7 : 2.3e-16;
    const double magnitude = std::abs(x) + std::abs(y) + scale;
    // The smallest subnormal: a rounding in the subnormal range is absolute, not relative.
    const double smallest  = single ? 1.4e-45 : 4.9e-324;
    const double allowed   = (bound.ulps * eps + bound.relative) * magnitude + bound.ulps * smallest;
    return std::abs(x - y) <= allowed;
}

/// One element of a tensor sample, as a double (the bytes of a sample are not aligned for the
/// element type).
double tensorElement(const TensorSample &t, size_t at) {
    switch (t.dtype) {
    case TypeCode::Float32: {
        float value = 0.0F;
        std::memcpy(&value, t.bytes.data() + at * sizeof(float), sizeof(float));
        return value;
    }
    case TypeCode::Int64: {
        int64_t value = 0;
        std::memcpy(&value, t.bytes.data() + at * sizeof(int64_t), sizeof(int64_t));
        return static_cast<double>(value);
    }
    default:
        return t.bytes[at] != 0 ? 1.0 : 0.0;
    }
}

/// Whether two tensor results agree elementwise (the scalar rule above, per element): a NaN agrees
/// with a NaN, an exact rule compares bits (a signed zero included), a permission-bound rule uses
/// its bound per element.
bool agreeTensor(const TensorSample &a, const TensorSample &b,
    const camel::core::sem::RewriteErrorBound &bound, double scale) {
    if (a.dtype != b.dtype || a.shape != b.shape || a.bytes.size() != b.bytes.size()) {
        return false;
    }
    if (a.dtype != TypeCode::Float32) {
        return a.bytes == b.bytes;
    }
    const size_t count = a.bytes.size() / sizeof(float);
    for (size_t i = 0; i < count; ++i) {
        if (!agreeFloat(tensorElement(a, i), tensorElement(b, i), true, bound, scale)) {
            return false;
        }
    }
    return true;
}

/// Whether two results agree: bit for bit when the rule needs no permission (a NaN is a NaN),
/// otherwise within the numeric contract of the rule's permissions and the operators' declared
/// bounds (camel/core/operator_semantics.h, rewriteErrorBound).
bool agree(
    const Sample &a, const Sample &b, const camel::core::sem::RewriteErrorBound &bound,
    double scale) {
    if (a.tensor != nullptr || b.tensor != nullptr) {
        return a.tensor != nullptr && b.tensor != nullptr && agreeTensor(*a.tensor, *b.tensor, bound, scale);
    }
    if (a.code == TypeCode::Float64 || a.code == TypeCode::Float32) {
        return agreeFloat(a.asDouble, b.asDouble, a.code == TypeCode::Float32, bound, scale);
    }
    if (a.code == TypeCode::Int32) {
        return fromSlot<Int32>(a.slot) == fromSlot<Int32>(b.slot);
    }
    return a.slot == b.slot;
}

std::string sampleText(const Sample &s) {
    if (s.tensor != nullptr) {
        const TensorSample &t = *s.tensor;
        std::string text       = std::format("tensor<{}>", typeCodeToString(t.dtype));
        text += " [";
        for (size_t i = 0; i < t.shape.size(); ++i) {
            text += (i == 0 ? "" : ", ") + std::to_string(t.shape[i]);
        }
        text += "] {";
        const size_t count = t.dtype == TypeCode::Float32 ? t.bytes.size() / sizeof(float)
            : t.dtype == TypeCode::Int64                    ? t.bytes.size() / sizeof(int64_t)
                                                            : t.bytes.size();
        const size_t shown = std::min<size_t>(count, 8);
        for (size_t i = 0; i < shown; ++i) {
            if (i != 0) {
                text += ", ";
            }
            switch (t.dtype) {
            case TypeCode::Float32:
                text += std::format("{}", static_cast<float>(tensorElement(t, i)));
                break;
            case TypeCode::Int64:
                text += std::to_string(static_cast<int64_t>(tensorElement(t, i)));
                break;
            default:
                text += t.bytes[i] != 0 ? "true" : "false";
                break;
            }
        }
        if (count > shown) {
            text += ", ...";
        }
        return text + "}";
    }
    switch (s.code) {
    case TypeCode::Int64:
        return std::to_string(fromSlot<Int64>(s.slot));
    case TypeCode::Int32:
        return std::to_string(fromSlot<Int32>(s.slot));
    case TypeCode::Bool:
        return fromSlot<Bool>(s.slot) ? "true" : "false";
    default:
        return std::format("{}", s.asDouble);
    }
}

Sample resultOf(slot_t slot, const Type *type) {
    if (camel::runtime::fact::shapeOfType(type)) {
        const auto *array = fromSlot<Array *>(slot);
        auto tensor       = std::make_shared<TensorSample>();
        tensor->dtype     = array->dtype();
        tensor->shape.assign(array->shape(), array->shape() + array->rank());
        if (array->byteSize() != 0) {
            const auto *bytes = array->dataAs<uint8_t>();
            tensor->bytes.assign(bytes, bytes + array->byteSize());
        }
        Sample sample;
        sample.code   = type->code();
        sample.slot   = slot;
        sample.tensor = std::move(tensor);
        return sample;
    }
    switch (type->code()) {
    case TypeCode::Int64:
        return {TypeCode::Int64, slot, static_cast<double>(fromSlot<Int64>(slot)), nullptr};
    case TypeCode::Int32:
        return {TypeCode::Int32, slot, static_cast<double>(fromSlot<Int32>(slot)), nullptr};
    case TypeCode::Float32:
        return {TypeCode::Float32, slot, static_cast<double>(fromSlot<Float32>(slot)), nullptr};
    case TypeCode::Float64:
        return {TypeCode::Float64, slot, fromSlot<Float64>(slot), nullptr};
    default:
        return {type->code(), slot, fromSlot<Bool>(slot) ? 1.0 : 0.0, nullptr};
    }
}

/// Counts the operators of a function graph (the steps that round).
size_t stepsOf(const camel::runtime::GCGraph &graph) {
    size_t steps = 0;
    for (camel::runtime::gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        steps += graph.node(ref)->kind == camel::runtime::GCNodeKind::Oper ? 1 : 0;
    }
    return steps;
}

/// The ulps the operators of one side declare between them (operator_semantics.h, stepUlps).
double declaredUlpsOf(const camel::runtime::GCGraph &graph) {
    const auto &table = camel::core::OperatorTable::instance();
    double ulps       = 0.0;
    for (camel::runtime::gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        const camel::runtime::GCNode *node = graph.node(ref);
        if (node->kind != camel::runtime::GCNodeKind::Oper || !node->hasPayload()) {
            continue;
        }
        const camel::core::sem::OperatorSemantics *declared =
            table.semantics(graph.nodeBodyAs<camel::runtime::GCOperBody>(ref)->oper);
        ulps += camel::core::sem::stepUlps(
            declared != nullptr ? declared->numeric : camel::core::sem::NumericSemantics{});
    }
    return ulps;
}

/// `check(rules, samples, seed)`: for every rule, `samples` random instances (random constants
/// for the const parameters, random values for the others) of its two sides, which must agree.
/// Returns the report; a line starting with FAIL is a rule that does not hold.
slot_t checkKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const int64_t samples = std::max<int64_t>(1, norm.get<Int64>(1));
    const uint64_t seed   = static_cast<uint64_t>(norm.get<Int64>(2));
    std::string report;
    const size_t count = norm.get<Array *>(0)->size();
    for (size_t index = 0; index < count; ++index) {
        // The array moves when a function runs: it is read again each time.
        RuleObject *made = norm.get<Array *>(0)->get<RuleObject *>(index);
        // A condition is read again from the rule object after every call, so the object is
        // rooted while the check runs.
        std::shared_ptr<mm::RootHandle> ruleRoot;
        std::shared_ptr<WhenCall> call;
        if (made->when() != nullptr) {
            ruleRoot = std::make_shared<mm::RootHandle>(
                mm::autoSpace(),
                made,
                RuleType::get(),
                "rewrite.checkRule");
            call       = std::make_shared<WhenCall>();
            call->rule = ruleRoot;
            call->vm   = std::make_shared<NodeVMSchedPass>(ctx.shared_from_this());
        }
        const RuleObject *rule = ruleRoot ? ruleRoot->getAs<RuleObject>() : made;
        const bool conditional = rule->when() != nullptr;
        // The rule object is read now and not again: a function runs below, and objects move.
        const camel::runtime::RuleSource source = sourceOf(*rule, 1, call);
        const std::string name(rule->name());
        const camel::core::sem::PermissionMask needs = rule->needs();
        const uint64_t constParams              = rule->constParams();
        const camel::runtime::Program *program  = rule->lhs()->program();
        const camel::runtime::GraphId lhsId     = rule->lhs()->graphId();
        const camel::runtime::GraphId rhsId     = rule->rhs()->graphId();
        const camel::runtime::GCGraph *lhsGraph = program->graph(lhsId);
        const auto *lhsType                     = lhsGraph->funcType();
        const size_t params                     = lhsType->normTypesCount();
        const Type *resultType                  = lhsType->exitType();
        const size_t steps = stepsOf(*lhsGraph) + stepsOf(*program->graph(rhsId));
        const double declaredUlps =
            declaredUlpsOf(*lhsGraph) + declaredUlpsOf(*program->graph(rhsId));
        // The bound is the numeric contract of the permissions the rule needs, read together
        // with the operators' declarations (not one uniform rounding bound).
        const camel::core::sem::RewriteErrorBound bound =
            camel::core::sem::rewriteErrorBound(needs, steps, declaredUlps);
        Sampler sampler(seed * 1000003u + index, needs);
        NodeVMSchedPass vm(ctx.shared_from_this());
        std::string failure;
        if (!bound.checkable) {
            // `nondeterministic` has no numeric contract: there is nothing to compare against,
            // and reporting `ok` would say the rule was checked. It was not.
            report += std::format(
                "unchecked {}: the permissions it needs have no numeric bound\n", name);
            continue;
        }
        // A condition is part of the equation: the two sides need not agree on values the
        // condition rejects, and the tool cannot ask the condition about sampled values. Such a
        // rule is checked by the engine path below only.
        for (int64_t sample = 0; !conditional && sample < samples && failure.empty(); ++sample) {
            std::vector<Sample> values;
            std::vector<slot_t> args;
            double scale = 1.0;
            for (size_t i = 0; i < params; ++i) {
                const bool isConst = i < 64 && ((constParams >> i) & 1) != 0;
                values.push_back(sampler.value(lhsType->normTypeAt(i), isConst));
                scale = std::max(scale, std::abs(values.back().asDouble) + 1.0);
            }
            // A tensor argument is an Array: it is made right before the call that reads it, so no
            // allocation can move it in between (and the call below is a safepoint).
            const auto fillArgs = [&] {
                args.resize(values.size());
                for (size_t i = 0; i < values.size(); ++i) {
                    args[i] = sampler.materialize(values[i]);
                }
            };
            fillArgs();
            const Sample left =
                resultOf(vm.callGraph(program, program->graph(lhsId), args), resultType);
            fillArgs();
            const Sample right =
                resultOf(vm.callGraph(program, program->graph(rhsId), args), resultType);
            if (!agree(left, right, bound, std::min(scale * scale, std::numeric_limits<double>::max()))) {
                std::string at;
                for (size_t i = 0; i < values.size(); ++i) {
                    at += (i == 0 ? "" : ", ") + sampleText(values[i]);
                }
                failure = std::format(
                    "FAIL {}: the two sides differ at ({}): {} and {}",
                    name,
                    at,
                    sampleText(left),
                    sampleText(right));
            }
        }
        // The engine applies the rule to the left side, with random constants for the const
        // parameters: it must fire, and the function it gives must agree too.
        size_t fired = 0, tried = 0;
        if (failure.empty()) {
            auto compiled            = camel::runtime::compileRule(ctx, source);
            camel::runtime::Rule one = std::move(compiled.front());
            one.twoWay               = false;
            const camel::core::sem::NumericPolicy policy{needs, true};
            // A condition selects its instances: a rule whose condition accepts few constants
            // needs more draws before it can be seen firing.
            const int64_t instances = std::min<int64_t>(samples, conditional ? 64 : 8);
            for (int64_t instance = 0; instance < instances && failure.empty(); ++instance) {
                ++tried;
                std::vector<Sample> consts;
                // A session over the left side alone: its result is a program of its own, the
                // program the rule is in is left as it is.
                camel::runtime::RuntimeGraphDraftSession session(
                    ctx.shared_from_this(),
                    program,
                    const_cast<camel::runtime::GCGraph *>(lhsGraph));
                camel::runtime::GraphDraft &draft =
                    session.edit(const_cast<camel::runtime::GCGraph *>(lhsGraph));
                const std::vector<camel::runtime::gc_node_ref_t> ports(
                    draft.normPorts().begin(),
                    draft.normPorts().end());
                std::vector<slot_t> args;
                std::vector<Sample> values;
                double scale = 1.0;
                for (size_t i = 0; i < params; ++i) {
                    const bool isConst = i < 64 && ((constParams >> i) & 1) != 0;
                    Type *type         = lhsType->normTypeAt(i);
                    Sample value       = sampler.value(type, isConst);
                    values.push_back(value);
                    scale = std::max(scale, std::abs(value.asDouble) + 1.0);
                    if (isConst) {
                        const auto constant = draft.materializeStaticValue(value.slot, type);
                        draft.replaceAllValueUses(ports[i], constant);
                    }
                }
                camel::runtime::DraftWritings writings(draft, policy);
                const auto stats = writings.rewrite(std::span<const camel::runtime::Rule>(&one, 1));
                if (stats.applied == 0) {
                    continue;
                }
                ++fired;
                const camel::runtime::Program *rewritten = session.commitChecked();
                const auto fillArgs = [&] {
                    args.resize(values.size());
                    for (size_t i = 0; i < values.size(); ++i) {
                        args[i] = sampler.materialize(values[i]);
                    }
                };
                fillArgs();
                const Sample before =
                    resultOf(vm.callGraph(program, program->graph(lhsId), args), resultType);
                fillArgs();
                const Sample after =
                    resultOf(vm.callGraph(rewritten, rewritten->graph(lhsId), args), resultType);
                if (!agree(before, after, bound, std::min(scale * scale, std::numeric_limits<double>::max()))) {
                    std::string at;
                    for (const Sample &value : values) {
                        at += (at.empty() ? "" : ", ") + sampleText(value);
                    }
                    failure = std::format(
                        "FAIL {}: the rewritten left side differs at ({}): {} and {} "
                        "(bound {} ulps, {} relative, scale {})",
                        name,
                        at,
                        sampleText(before),
                        sampleText(after),
                        bound.ulps,
                        bound.relative,
                        scale * scale);
                }
            }
            if (failure.empty() && fired == 0) {
                failure = std::format(
                    "FAIL {}: the rule never fired on its own left side in {} tries",
                    name,
                    tried);
            }
        }
        report +=
            failure.empty()
                ? (conditional ? std::format(
                                     "ok {}: the condition selects the instances, fired {} of "
                                     "{}\n",
                                     name,
                                     fired,
                                     tried)
                               : std::format(
                                     "ok {}: {} samples agree, fired {} of {}\n",
                                     name,
                                     samples,
                                     fired,
                                     tried))
                : failure + "\n";
    }
    return toSlot(String::from(report, mm::autoSpace()));
}

class RewriteExecutor final : public Executor {
  public:
    explicit RewriteExecutor(context_ptr_t ctx) : Executor(std::move(ctx), kernels()) {}

  private:
    static std::unordered_map<std::string, operator_t> kernels() {
        return {
            {"rule", ruleKernel},
            {"rule_when", ruleWhenKernel},
            {"name", nameKernel},
            {"two_way", twoWayKernel},
            {"needs", needsKernel},
            {"consts", constsKernel},
            {"rewrite", rewriteKernel},
            {"run", runKernel},
            {"saturate", saturateKernel},
            {"extract", extractKernel},
            {"check", checkKernel},
            {"analysis", camel::rewritemod::analysisKernel},
        };
    }
};

class RewriteModule final : public BuiltinModule {
  public:
    explicit RewriteModule(context_ptr_t ctx) : BuiltinModule("rewrite", std::move(ctx)) {
        exportType(Reference("Rule"), RuleType::get());
        exportEntity(
            "__rule",
            OperatorGroup::create(
                "__rule",
                {{"rewrite:rule",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {6, {false, false, false, false, false, false}}},
                      "(name: string, two_way: bool, needs: int, consts: int, lhs: F, rhs: F) => "
                      "Rule",
                      [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (norm[0]->code() != TypeCode::String ||
                              norm[1]->code() != TypeCode::Bool ||
                              norm[2]->code() != TypeCode::Int64 ||
                              norm[3]->code() != TypeCode::Int64 ||
                              norm[4]->code() != TypeCode::Function ||
                              norm[5]->code() != TypeCode::Function)
                              return std::nullopt;
                          return RuleType::get();
                      })}}));
        exportEntity(
            "__rule_when",
            OperatorGroup::create(
                "__rule_when",
                {{"rewrite:rule_when",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {7, {false, false, false, false, false, false, false}}},
                      "(name: string, two_way: bool, needs: int, consts: int, lhs: F, rhs: F, "
                      "when: F) => Rule",
                      [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (norm[0]->code() != TypeCode::String ||
                              norm[1]->code() != TypeCode::Bool ||
                              norm[2]->code() != TypeCode::Int64 ||
                              norm[3]->code() != TypeCode::Int64 ||
                              norm[4]->code() != TypeCode::Function ||
                              norm[5]->code() != TypeCode::Function ||
                              norm[6]->code() != TypeCode::Function)
                              return std::nullopt;
                          const auto *when = static_cast<const FunctionType *>(norm[6]);
                          if (when->exitType() == nullptr ||
                              when->exitType()->code() != TypeCode::Bool)
                              return std::nullopt;
                          return RuleType::get();
                      })}}));
        // `__analysis` (rewrite design 4): the compiled form of an `analysis` declaration. Its
        // arity is 4 + k + 3n: name, initial, join, widen, the k pattern variables the query was
        // called with (one node, `n`, in the one-value form), then for each `on` clause its
        // pattern function, its fact function and the mask of its `const` parameters.
        exportEntity(
            "__analysis",
            OperatorGroup::create(
                "__analysis",
                {{"rewrite:analysis",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {-1, {}}},
                      "(name: string, initial: F, join: F, widen: F, node: Node, ...) => T",
                      [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (norm.size() < 5 || norm[0]->code() != TypeCode::String) {
                              return std::nullopt;
                          }
                          for (size_t i = 1; i < 4; ++i) {
                              if (norm[i]->code() != TypeCode::Function) {
                                  return std::nullopt;
                              }
                          }
                          // The query's pattern variables come first, one `Node` each; the `on`
                          // descriptions follow as triples that begin with a function.
                          size_t at = 4;
                          while (at < norm.size() && HandleType::is(norm[at], HandleKind::Node)) {
                              ++at;
                          }
                          if (at == 4 || at >= norm.size() || (norm.size() - at) % 3 != 0) {
                              return std::nullopt;
                          }
                          for (size_t i = at; i < norm.size(); i += 3) {
                              if (norm[i]->code() != TypeCode::Function ||
                                  norm[i + 1]->code() != TypeCode::Function ||
                                  norm[i + 2]->code() != TypeCode::Int64) {
                                  return std::nullopt;
                              }
                          }
                          const auto *initial =
                              static_cast<const FunctionType *>(norm[1]);
                          if (initial->exitType() == nullptr) {
                              return std::nullopt;
                          }
                          return initial->exitType();
                      })}}));
        // The callable operators (`rewrite`, `saturate`, `extract`, `check`, `run` and the four
        // rule readers) are declared in `stdlib/rewrite.cml`; the declarations are the one source
        // of their signatures, so the module exports none of them itself (handover H183).
    }

    bool load() override {
        if (loaded_)
            return true;
        context_->registerExecutorFactory("rewrite", [ctx = context_]() {
            return std::make_shared<RewriteExecutor>(ctx);
        });
        // The readers are pure. `__rule` is not: constant folding would build the rule at
        // compile time and keep it in a static area, where the collector does not trace the
        // functions it holds. `__analysis` compiles an `analysis` declaration at macro time and
        // installs its facts, so it is effectful as well; it was the one operator of the module
        // whose purity was never declared, which nothing noticed while no program imported the
        // module (handover H173).
        camel::core::OperatorTable::instance().declare(
            "rewrite",
            {{"rule", false},
             {"rule_when", false},
             {"name", true},
             {"two_way", true},
             {"needs", true},
             {"consts", true},
             {"rewrite", false},
             {"run", false},
             {"saturate", false},
             {"extract", false},
             {"check", false},
             {"analysis", false}});
        loaded_ = true;
        return true;
    }
};

} // namespace

extern "C" Module *camel_module_create(Context *ctx) {
    return new RewriteModule(ctx->shared_from_this());
}

