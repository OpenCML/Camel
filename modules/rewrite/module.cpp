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
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */


/*
 * The rewrite module: rules as values, and the engines that apply them to a program.
 *
 * `rule name(params) { left => right }` is read as two functions of the same parameters and
 * `let name = __rule('name', ...)` (graph store 5.2: a rule is the shorthand of a pair of
 * functions); the front end writes the import of this module itself. The engines, `rewrite`,
 * `saturate` and `extract`, take the rules as an array.
 */

#include "rule.h"

#include "camel/core/context/context.h"
#include "camel/core/module/builtin.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/program.h"
#include "camel/runtime/rules.h"
#include "camel/runtime/writings.h"
#include "src/passes/sched/nodevm/nodevm.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::rtdata;
using namespace camel::core::type;
using namespace camel::core::rtdata;
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
        static_cast<uint8_t>(norm.get<rtdata::Int64>(2)),
        static_cast<uint64_t>(norm.get<rtdata::Int64>(3)),
        norm.get<::Function *>(4),
        norm.get<::Function *>(5),
        nullptr,
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

/// The rule a rule function makes: the function is called (a rule is the function of a
/// declaration, with no parameter).
RuleObject *callRule(Context &ctx, ::Function *maker) {
    NodeVMSchedPass vm(ctx.shared_from_this());
    const slot_t made = vm.callGraph(maker->program(), maker->graph(), {});
    return static_cast<RuleObject *>(fromSlot<Object *>(made));
}

/// The engine's rules for the rule functions of an array.
///
/// A function is called for each rule, which can move the objects (a safepoint): the array is read
/// again from the argument, which the collector keeps up to date, each time.
std::vector<camel::runtime::Rule> engineRules(Context &ctx, ArgsView &norm, size_t argument) {
    std::vector<camel::runtime::Rule> out;
    uint16_t next = 1;
    const size_t count = norm.get<Array *>(argument)->size();
    for (size_t i = 0; i < count; ++i) {
        const RuleObject *rule =
            callRule(ctx, norm.get<Array *>(argument)->get<::Function *>(i));
        camel::runtime::RuleSource source;
        source.name          = std::string(rule->name());
        source.id            = next++;
        source.needs         = rule->needs();
        source.bidirectional = rule->twoWay();
        source.program       = rule->lhs()->program();
        source.lhs           = rule->lhs()->graphId();
        source.rhs           = rule->rhs()->graphId();
        for (int bit = 0; bit < 64; ++bit) {
            source.constParams.push_back((rule->constParams() >> bit) & 1);
        }
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
    return name.ends_with("__lhs") || name.ends_with("__rhs");
}

/// `rewrite(p, rules)`: the program with the rules applied one way, in place, to a fixpoint
/// (rewrite design 3). An error value when a rule cannot be a rule, holds both ways, or the
/// result is not a program.
slot_t rewriteKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const TupleType *resultType = TupleType::fallible(norm.type(0));
    const auto *base = norm.get<const camel::runtime::Program *>(0);
    try {
        const auto compiled = engineRules(ctx, norm, 1);
        camel::runtime::RuntimeGraphDraftSession session(ctx.shared_from_this(), base);
        for (camel::runtime::GCGraph *graph : base->graphs()) {
            if (isRuleSide(*graph)) {
                continue;
            }
            camel::runtime::GraphDraft &draft = session.edit(graph);
            camel::runtime::DraftWritings writings(
                draft, camel::core::sem::NumericPolicy::relaxed());
            writings.rewrite(compiled);
        }
        const camel::runtime::Program *committed = session.commitChecked();
        return makeOk(toSlot(const_cast<camel::runtime::Program *>(committed)), resultType);
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

class RewriteExecutor final : public Executor {
  public:
    explicit RewriteExecutor(context_ptr_t ctx) : Executor(std::move(ctx), kernels()) {}

  private:
    static std::unordered_map<std::string, operator_t> kernels() {
        return {
            {"rule", ruleKernel},
            {"name", nameKernel},
            {"two_way", twoWayKernel},
            {"needs", needsKernel},
            {"consts", constsKernel},
            {"rewrite", rewriteKernel},
        };
    }
};

oper_group_ptr_t ruleReader(const char *name, Type *result) {
    return OperatorGroup::create(
        name,
        {{std::string("rewrite:") + name,
          StaticFuncTypeResolver::create({}, {{RuleType::get(), false}}, result)}});
}

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
                      "(name: string, two_way: bool, needs: int, consts: int, lhs: F, rhs: F) => Rule",
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
            "rewrite",
            OperatorGroup::create(
                "rewrite",
                {{"rewrite:rewrite",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {2, {false, false}}},
                      "(p: Program, rules: (() => Rule)[]) => Program!",
                      [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          // The Program type belongs to the module graph; it is told by its name.
                          if (norm[0]->toString() != "Program" ||
                              norm[1]->code() != TypeCode::Array)
                              return std::nullopt;
                          const Type *element = static_cast<ArrayType *>(norm[1])->elemType();
                          if (element == nullptr || element->code() != TypeCode::Function)
                              return std::nullopt;
                          const auto *maker = static_cast<const FunctionType *>(element);
                          if (maker->normTypesCount() != 0 || maker->withTypesCount() != 0 ||
                              !RuleType::is(maker->exitType()))
                              return std::nullopt;
                          return TupleType::fallible(norm[0]);
                      })}}));
        exportEntity("rule_name", ruleReader("name", Type::String()));
        exportEntity("rule_two_way", ruleReader("two_way", Type::Bool()));
        exportEntity("rule_needs", ruleReader("needs", Type::Int64()));
        exportEntity("rule_consts", ruleReader("consts", Type::Int64()));
    }

    bool load() override {
        if (loaded_)
            return true;
        context_->registerExecutorFactory("rewrite", [ctx = context_]() {
            return std::make_shared<RewriteExecutor>(ctx);
        });
        // The readers are pure. `__rule` is not: constant folding would build the rule at
        // compile time and keep it in a static area, where the collector does not trace the
        // functions it holds.
        camel::core::OperatorTable::instance().declare(
            "rewrite",
            {{"rule", false},
             {"name", true},
             {"two_way", true},
             {"needs", true},
             {"consts", true},
             {"rewrite", true}});
        loaded_ = true;
        return true;
    }
};

} // namespace

extern "C" Module *camel_module_create(Context *ctx) {
    return new RewriteModule(ctx->shared_from_this());
}
