// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * @file explain.cpp
 * @brief Reading a run's rewrite record from the debugger, and the two commands over it.
 *
 * The debug core does not have a second rewrite engine. A command reads the values of the stopped
 * frame, calls the module's own operator through the run-time dispatch the scheduler uses
 * (`ExecutorManager` and the operator's `operator_t`), and reads the `Proof` the module hands
 * back. The answer of `explain` and `why_not` is therefore the answer of the library on the same
 * record, not a reconstruction of it: the record is the authority, this file only shows it.
 *
 * Nothing here lets a nested run of the module (the `when` function `why_not` may run) be taken
 * for the program being debugged: the observation hooks are paused for the duration of the call,
 * as they are for a compiler pass (H150).
 */

#include "debug_core.h"

#include "proof.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/operator.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/other.h"
#include "camel/execute/executor.h"

#include <format>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace debugger::core {

using json = nlohmann::json;

namespace {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

using camel::core::OperatorTable;

/// A refusal the REPL prints as it prints any other failed core command.
json fail(std::string message) { return json{{"ok", false}, {"error", std::move(message)}}; }

/// The arguments of one call, as the operator sees them. The values are the frame's; the view may
/// replace one (`setSlot`) the way a kernel may, and the owner reads its vector afterwards.
class SlotArgs final : public ArgsView {
  public:
    SlotArgs(std::span<slot_t> slots, std::span<type::Type *> types)
        : slots_(slots), types_(types) {}

    size_t size() const override { return slots_.size(); }
    slot_t slot(size_t index) const override { return slots_[index]; }
    void setSlot(size_t index, slot_t value) override { slots_[index] = value; }
    type::TypeCode code(size_t index) const override {
        return types_[index] != nullptr ? types_[index]->code() : type::TypeCode::Void;
    }
    type::Type *type(size_t index) const override { return types_[index]; }

  private:
    std::span<slot_t> slots_;
    std::span<type::Type *> types_;
};

/// A nested run is not the program: while it runs, the observer must not index its graphs or
/// present its frames, exactly as for a compiler pass (H150).
class ObservationPause {
  public:
    explicit ObservationPause(bool &observing) : observing_(observing), saved_(observing) {
        observing_ = false;
    }
    ~ObservationPause() { observing_ = saved_; }
    ObservationPause(const ObservationPause &)            = delete;
    ObservationPause &operator=(const ObservationPause &) = delete;

  private:
    bool &observing_;
    bool saved_;
};

std::string counted(size_t n, const char *one, const char *many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

/// The text of a step as `%matched -> %produced`, the two node positions of the rewrite.
std::string positions(const camel::runtime::RewriteStep &step) {
    const std::string matched =
        step.matched.empty() ? std::string("%?") : "%" + std::to_string(step.matched.front());
    const std::string produced =
        step.produced == camel::runtime::kInvalidNodeRef ? std::string("%?")
                                                        : "%" + std::to_string(step.produced);
    return matched + " -> " + produced;
}

nlohmann::json proofJson(const camel::rewritemod::ProofObject &proof) {
    const auto &record = proof.record();
    nlohmann::json steps = nlohmann::json::array();
    for (const camel::runtime::RewriteStep &step : record.steps) {
        nlohmann::json matched = nlohmann::json::array();
        for (camel::runtime::gc_node_ref_t node : step.matched) {
            matched.push_back(node);
        }
        nlohmann::json erased = nlohmann::json::array();
        for (camel::runtime::gc_node_ref_t node : step.erased) {
            erased.push_back(node);
        }
        steps.push_back(
            nlohmann::json{
                {"rule", step.rule},
                {"round", step.round},
                {"graph", step.graph},
                {"matched", std::move(matched)},
                {"produced", step.produced},
                {"erased", std::move(erased)},
                {"matchedText", step.matchedText},
                {"producedText", step.producedText},
                {"erasedText", step.erasedText},
            });
    }
    nlohmann::json answer{
        {"stats",
         nlohmann::json{
             {"rewrites", record.steps.size()},
             {"rounds", proof.stats().rounds},
             {"visited", proof.stats().visited},
         }},
        {"steps", std::move(steps)},
        {"strict", record.strict},
        {"input", proof.input() != nullptr},
    };
    if (const auto &chain = proof.chain()) {
        nlohmann::json chains = nlohmann::json::array();
        for (const camel::rewritemod::ProofObject::ChainPath &path : chain->paths) {
            nlohmann::json links = nlohmann::json::array();
            for (const camel::rewritemod::ProofObject::Link &link : path) {
                if (link.step >= record.steps.size()) {
                    continue;
                }
                const camel::runtime::RewriteStep &step = record.steps[link.step];
                links.push_back(
                    nlohmann::json{
                        {"step", link.step},
                        {"reverse", link.reverse},
                        {"rule", step.rule},
                        {"round", step.round},
                        {"graph", step.graph},
                        {"matched", positions(step)},
                    });
            }
            chains.push_back(std::move(links));
        }
        answer["chain"] = nlohmann::json{
            {"from", chain->from},
            {"to", chain->to},
            {"same", chain->same},
            {"none", chain->none},
            {"paths", std::move(chains)},
        };
    }
    return answer;
}

/// The chain as `a = ... = b`, one line per rewrite, with the rule and both node positions. This
/// is the library's print with the positions made explicit; the library's own line is the same.
std::string chainText(const camel::rewritemod::ProofObject &proof) {
    const auto &chain = *proof.chain();
    if (chain.same) {
        return "the same value: " + chain.from +
               " is one node, so no rewrite is needed to connect it to itself";
    }
    if (!chain.none.empty()) {
        return chain.none;
    }
    std::string out;
    if (chain.paths.size() == 1) {
        out += "chain " + chain.from + " = " + chain.to + ": " +
               counted(chain.paths.front().size(), "rewrite", "rewrites") + "\n";
    } else {
        out += counted(chain.paths.size(), "chain", "chains") + " of the same length from " +
               chain.from + " to " + chain.to + ", in the order the rewrites were made\n";
    }
    const auto &steps = proof.record().steps;
    for (size_t p = 0; p < chain.paths.size(); ++p) {
        const auto &path = chain.paths[p];
        const std::string indent = chain.paths.size() == 1 ? "  " : "  " + std::to_string(p + 1) + ". ";
        out += indent + chain.from + "\n";
        for (const camel::rewritemod::ProofObject::Link &link : path) {
            if (link.step >= steps.size()) {
                continue;
            }
            const camel::runtime::RewriteStep &step = steps[link.step];
            out += indent + "  = " + camel::rewritemod::linkAfter(step, link.reverse) + "  by " +
                   step.rule;
            if (link.reverse) {
                out += "  (backwards)";
            }
            out += "  (round " + std::to_string(step.round) + ", graph " + step.graph + ")";
            out += "  (" + positions(step) + ")\n";
        }
    }
    return out;
}

/// The full record, every rewrite with its rule, round, graph and positions. The library prints
/// the same steps; the positions are repeated on their own so a reader need not parse the text.
std::string recordText(const camel::rewritemod::ProofObject &proof) {
    const auto &record = proof.record();
    const auto &stats  = proof.stats();
    std::string out;
    if (record.steps.empty()) {
        out = "no rewrite matched in " + counted(stats.rounds, "round", "rounds") +
              ": the program is unchanged";
        return out;
    }
    out = counted(record.steps.size(), "rewrite", "rewrites") + " in " +
          counted(stats.rounds, "round", "rounds") + ", " + std::to_string(stats.visited) +
          " nodes visited\n";
    for (const camel::runtime::RewriteStep &step : record.steps) {
        const std::string before =
            step.matchedText.empty() ? std::string("%?") : step.matchedText.front();
        out += "  " + step.graph + " round " + std::to_string(step.round) + ": " + before + "\n";
        out += "  = " + step.producedText + "  by " + step.rule + "  (" + positions(step) + ")\n";
        out += "    matched";
        for (const std::string &text : step.matchedText) {
            out += " " + text + ";";
        }
        out += "\n";
        if (!step.erasedText.empty()) {
            out += "    erased";
            for (const std::string &text : step.erasedText) {
                out += " " + text + ";";
            }
            out += "\n";
        }
    }
    return out;
}

} // namespace

/// The `Proof` a successful `explain`/`explain_all` returned, or why the value is not one.
const camel::rewritemod::ProofObject *
DebugCore::proofOf(const CallAnswer &answer, std::string &why) {
    const auto *tuple = dynamic_cast<const type::TupleType *>(answer.type);
    if (tuple == nullptr || !tuple->isFallible()) {
        why = "the operator did not answer a proof";
        return nullptr;
    }
    const type::Type *payload = tuple->payloadType();
    const auto *other         = dynamic_cast<const type::OtherType *>(payload);
    if (other == nullptr || other->typeName() != "rewrite::Proof") {
        why = "the operator answered " + (payload ? payload->toString() : std::string("nothing")) +
              ", not a proof";
        return nullptr;
    }
    return static_cast<const camel::rewritemod::ProofObject *>(
        rtdata::fromSlot<const void *>(rtdata::falliblePayload(answer.slot)));
}

std::string DebugCore::locatedDiagnostic(const ShadowFrame &f, const std::string &message) {
    const SourceLoc &loc = f.loc.valid() ? f.loc : SourceLoc{};
    if (!loc.valid() && (f.lastFile < 0 || f.lastLine <= 0)) {
        return message;
    }
    std::string path;
    int line   = loc.valid() ? loc.line : f.lastLine;
    int column = loc.valid() ? loc.column : 0;
    {
        std::lock_guard<IndexMutex> lock(indexMutex_);
        path = index_.filePath(loc.valid() ? loc.file : f.lastFile);
    }
    if (path.empty()) {
        return message;
    }
    return std::format("{}:{}:{}: {}", path, line, column, message);
}

json DebugCore::evaluationFailure(const ShadowFrame &f, const std::string &why) {
    const std::string text = locatedDiagnostic(f, why);
    return json{
        {"ok", false},
        {"error", text},
        {"diagnostic", json{{"name", "DebuggerEvaluation"}, {"message", why}, {"text", text}}},
    };
}

DebugCore::CallAnswer
DebugCore::callAtFrame(ShadowFrame &f, const std::string &uri, const std::vector<std::string> &arguments) {
    CallAnswer answer;
    auto context = index_.context();
    if (!context) {
        answer.diagnostic = locatedDiagnostic(f, "the program has no context to evaluate in");
        return answer;
    }
    // The operator first: finding it may import its module, which is work that must not run
    // under values read from the frame.
    std::optional<operator_t> kernel;
    try {
        kernel = context->execMgr().find(uri);
    } catch (const std::exception &e) {
        answer.diagnostic = locatedDiagnostic(f, e.what());
        return answer;
    }
    if (!kernel) {
        answer.diagnostic =
            locatedDiagnostic(f, "no operator '" + uri + "' is available in this program");
        return answer;
    }
    std::vector<ValueView> values;
    values.reserve(arguments.size());
    for (const std::string &argument : arguments) {
        ResolvedValue resolved;
        std::string why;
        if (!resolveValueAt(f, argument, resolved, why)) {
            answer.diagnostic = locatedDiagnostic(f, why);
            return answer;
        }
        if (!resolved.evaluated) {
            answer.diagnostic = locatedDiagnostic(
                f, "'" + resolved.variable.name + "' has no value yet: " + resolved.variable.note);
            return answer;
        }
        values.push_back(resolved.view);
    }
    // The result type comes from the module's own type rule, the one the compiler uses, so the
    // tool does not keep a second copy of the signature either.
    auto resolver = OperatorTable::instance().resolver(uri);
    if (!resolver) {
        answer.diagnostic = locatedDiagnostic(f, "the operator '" + uri + "' has no type rule");
        return answer;
    }
    type::type_vec_t norm;
    for (const ValueView &value : values) {
        norm.push_back(value.type);
    }
    type::FunctionType *signature = nullptr;
    try {
        signature = resolver->resolve({}, norm, ModifierSet{}).value_or(nullptr);
    } catch (const std::exception &e) {
        answer.diagnostic = locatedDiagnostic(f, e.what());
        return answer;
    }
    if (signature == nullptr || signature->exitType() == nullptr) {
        answer.diagnostic = locatedDiagnostic(
            f,
            "the arguments of '" + uri + "' do not fit its type rule (" +
                (signature ? signature->toString() : std::string("no overload")) + ")");
        return answer;
    }
    answer.type = signature->exitType();

    std::vector<slot_t> slots;
    std::vector<type::Type *> types;
    std::vector<std::unique_ptr<mm::RootHandle>> roots;
    slots.reserve(values.size());
    types.reserve(values.size());
    for (const ValueView &value : values) {
        slots.push_back(value.slot);
        types.push_back(value.type);
        if (value.type != nullptr && value.type->isGCTraced() && value.slot != NullSlot) {
            roots.push_back(std::make_unique<mm::RootHandle>(
                mm::autoSpace(), rtdata::fromSlot<rtdata::Object *>(value.slot), value.type,
                "debugger.argument"));
        }
    }
    // A collection may have run while the type rule was resolved (it allocates types): read the
    // arguments from their roots again, so none is used at its old address.
    size_t rooted = 0;
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i].type != nullptr && values[i].type->isGCTraced() &&
            values[i].slot != NullSlot) {
            slots[i] = rtdata::toSlot(roots[rooted++]->get());
        }
    }

    ObservationPause pause(observingRun_);
    try {
        SlotArgs with{std::span<slot_t>(), std::span<type::Type *>()};
        SlotArgs normal(slots, types);
        normal.setResultType(answer.type);
        answer.slot = (*kernel)(with, normal, *context);
        answer.ok   = true;
    } catch (const camel::core::error::RuntimeFault &fault) {
        std::string text = fault.what();
        try {
            if (auto reporter = context->runtimeErrorReporter()) {
                auto site = camel::core::error::makeGraphExecutionSite(
                    context->sourceContext(), f.graph, stack_.size());
                if (f.origin != 0) {
                    site.cachedOrigin = f.origin;
                }
                text = reporter->describe(fault, site).toText();
            }
        } catch (...) {
            // Keep the fault's own sentence.
        }
        answer.diagnostic = text;
    } catch (const std::exception &e) {
        answer.diagnostic = locatedDiagnostic(f, e.what());
    } catch (...) {
        answer.diagnostic = locatedDiagnostic(f, "the evaluation failed");
    }
    if (!answer.ok) {
        return answer;
    }
    const auto *tuple = dynamic_cast<const type::TupleType *>(answer.type);
    if (tuple != nullptr && tuple->isFallible() && rtdata::isErrorValue(answer.slot)) {
        answer.fallibleError = true;
        answer.errorText     = render(ValueView{answer.slot, answer.type}).value;
    }
    return answer;
}

json DebugCore::explainJson(const json &args) {
    if (stack_.empty()) {
        return fail("no frame");
    }
    ShadowFrame &f        = stack_.back();
    const std::string run = args.value("run", std::string());
    const std::string a   = args.value("a", std::string());
    const std::string b   = args.value("b", std::string());
    if (run.empty()) {
        return fail("explain: the run to read is required");
    }
    const bool all = a.empty() && b.empty();
    if (!all && (a.empty() || b.empty())) {
        return fail("explain: give the run, or the run and two values");
    }
    const CallAnswer answer = callAtFrame(
        f, all ? "rewrite:explain_all" : "rewrite:explain",
        all ? std::vector<std::string>{run} : std::vector<std::string>{run, a, b});
    if (!answer.ok) {
        return json{
            {"ok", false},
            {"error", answer.diagnostic},
            {"diagnostic", json{{"name", "DebuggerEvaluation"}, {"text", answer.diagnostic}}}};
    }
    std::unique_ptr<mm::RootHandle> root;
    if (answer.type != nullptr && answer.type->isGCTraced() && answer.slot != NullSlot) {
        root = std::make_unique<mm::RootHandle>(
            mm::autoSpace(), rtdata::fromSlot<rtdata::Object *>(answer.slot), answer.type,
            "debugger.proof");
    }
    if (answer.fallibleError) {
        return json{
            {"ok", true},
            {"text", (all ? "explain_all: " : "explain: ") + answer.errorText},
            {"error", answer.errorText},
        };
    }
    std::string why;
    const auto *proof = proofOf(answer, why);
    if (proof == nullptr) {
        return fail(why);
    }
    if (!all && !proof->chain()) {
        return fail("the proof has no chain");
    }
    return json{
        {"ok", true},
        {"text", (all ? "explain_all: " : "explain: ") +
                     (all ? recordText(*proof) : chainText(*proof))},
        {"proof", proofJson(*proof)},
    };
}

json DebugCore::whyNotJson(const json &args) {
    if (stack_.empty()) {
        return fail("no frame");
    }
    ShadowFrame &f                 = stack_.back();
    const std::string run          = args.value("run", std::string());
    const std::string rule         = args.value("rule", std::string());
    const std::string value        = args.value("value", std::string());
    if (run.empty() || rule.empty() || value.empty()) {
        return fail("why_not: the run, the rule and the value are required");
    }
    const CallAnswer answer =
        callAtFrame(f, "rewrite:why_not", std::vector<std::string>{run, rule, value});
    if (!answer.ok) {
        return json{
            {"ok", false},
            {"error", answer.diagnostic},
            {"diagnostic", json{{"name", "DebuggerEvaluation"}, {"text", answer.diagnostic}}}};
    }
    std::unique_ptr<mm::RootHandle> root;
    if (answer.type != nullptr && answer.type->isGCTraced() && answer.slot != NullSlot) {
        root = std::make_unique<mm::RootHandle>(
            mm::autoSpace(), rtdata::fromSlot<rtdata::Object *>(answer.slot), answer.type,
            "debugger.why_not");
    }
    if (answer.fallibleError) {
        return json{
            {"ok", true}, {"text", "why_not: " + answer.errorText}, {"error", answer.errorText}};
    }
    const auto *tuple = dynamic_cast<const type::TupleType *>(answer.type);
    if (tuple == nullptr || tuple->payloadType() != type::Type::String()) {
        return fail("the operator did not answer a reason");
    }
    const auto *text =
        rtdata::fromSlot<const String *>(rtdata::falliblePayload(answer.slot));
    if (text == nullptr) {
        return fail("the operator answered no reason");
    }
    return json{
        {"ok", true},
        {"text", "why_not: " + text->toString()},
        {"reason", text->toString()},
    };
}

} // namespace debugger::core