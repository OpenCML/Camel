// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The resources a built body touches: the measurement each body publishes and the check that
 * holds a declaration to the clause it wrote. Nothing here is read from the running program: a
 * node publishes its own signature -- an operator's from the operator table, a callee's from its
 * own measurement, a call's from the value it goes through -- and what those signatures mean is
 * camel/core/resource_semantics.h. What a violation sounds like is builder_diagnostics.cpp's.
 */

#include "builder.h"
#include "builder_helpers.h"

#include "camel/core/operator_names.h"
#include "camel/core/operator_table.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <array>
#include <deque>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
using namespace camel::core::type;

namespace sem = camel::core::sem;

namespace camel::compile::gir {

namespace {

using sem::ResourceSignature;
using Measurements = std::unordered_map<DraftGraphBuilder *, ResourceSignature>;

/// The classes a use the measurement cannot place is read as touching: every class a clause has
/// no name for. A clause that names resources does not cover them, so `covers` decides.
constexpr std::array<sem::ResourceClass, 5> kUnplacedClasses = {
    sem::ResourceClass::Variable,
    sem::ResourceClass::Object,
    sem::ResourceClass::Foreign,
    sem::ResourceClass::Ambient,
    sem::ResourceClass::Handle};

/// Walks the nodes of one body and adds up what they publish. A use it cannot place is not
/// dropped: either the whole measurement is refused (`Early`, which wants a complete answer or
/// none) or the use is read conservatively and the reason is kept for the report (`Settled`).
class FootprintWalker {
  public:
    FootprintWalker(DraftGraphBuilder *graph, const Measurements &callees, bool settled)
        : graph_(graph), draft_(graph->draft()), type_(graph->funcType()), callees_(callees),
          settled_(settled) {
        out_.mode = ResourceSignature::Mode::Concrete;
    }

    /// nullopt when the body did not build, or when it could not be measured and `Early` was
    /// asked for.
    std::optional<ResourceSignature> run(std::vector<std::string> *unplaced) {
        if (type_ == nullptr) {
            return std::nullopt; // the body did not build (an error was reported)
        }
        numberPorts();
        for (draft_node_ref_t node : draft_.closureNodes()) {
            captured_.insert(node);
        }
        for (draft_node_ref_t id = 0; id < draft_.nodeSlotCount(); ++id) {
            if (draft_.alive(id)) {
                measureNode(id);
            }
        }
        addVarParameters();
        if (!unplaced_.empty()) {
            if (!settled_) {
                return std::nullopt;
            }
            for (sem::ResourceClass cls : kUnplacedClasses) {
                out_.anyOf.push_back(cls);
            }
            out_.external = true;
            if (unplaced != nullptr) {
                *unplaced = unplaced_;
            }
        }
        sortAndDedupe();
        return out_;
    }

  private:
    // ---- the numbering of parameters ----

    /// Which parameter a port is, in the numbering a call uses: `with` parameters first, then the
    /// ordinary ones (a `constas` parameter is not a runtime value and has no port).
    void numberPorts() {
        const size_t withCount = type_->withTypesCount();
        const auto withPorts   = draft_.withPorts();
        const auto normPorts   = draft_.normPorts();
        size_t index = 0, withAt = 0, normAt = 0;
        for (size_t i = 0; i < withCount + type_->normTypesCount(); ++i) {
            if (i < withCount) {
                if (withAt < withPorts.size()) {
                    portIndex_.emplace(withPorts[withAt++], index);
                }
            } else {
                if (type_->normIsConstasAt(i - withCount)) {
                    continue; // not a runtime parameter
                }
                if (normAt < normPorts.size()) {
                    portIndex_.emplace(normPorts[normAt++], index);
                }
            }
            ++index;
        }
        // A `with` port the type does not count is a value the function captured from the one
        // around it: its resource is that function's, and is not a parameter of this one.
        for (; withAt < withPorts.size(); ++withAt) {
            captured_.insert(withPorts[withAt]);
        }
    }

    // ---- what is added up ----

    /// A use that cannot be placed: the reason is kept, and the result is read conservatively.
    void unplaced(std::string reason) {
        if (std::find(unplaced_.begin(), unplaced_.end(), reason) == unplaced_.end()) {
            unplaced_.push_back(std::move(reason));
        }
    }

    static sem::AccessMode uniteMode(sem::AccessMode a, sem::AccessMode b) {
        return sem::writes(a) || sem::writes(b) ? sem::AccessMode::ReadModifyWrite
                                                : sem::AccessMode::Read;
    }

    void addParam(
        size_t index, sem::ResourceClass cls, const std::string &name, sem::AccessMode mode) {
        if (index > std::numeric_limits<uint8_t>::max()) {
            // A signature names a parameter by one byte: a use through a later one has no place.
            unplaced(std::format("a use through parameter {}, past the 255th", index + 1));
            return;
        }
        const auto found =
            std::find_if(out_.params.begin(), out_.params.end(), [&](const auto &use) {
                return use.index == static_cast<uint8_t>(index);
            });
        if (found == out_.params.end()) {
            out_.params.push_back(ResourceSignature::ParamUse{
                .index = static_cast<uint8_t>(index), .cls = cls, .name = name, .mode = mode});
            return;
        }
        // One parameter, one resource: two uses of it are one use, with the wider mode.
        if (found->cls != cls) {
            found->cls = sem::ResourceClass::Unknown;
        }
        if (found->name.empty()) {
            found->name = name;
        }
        found->mode = uniteMode(found->mode, mode);
    }

    void addAmbient(const std::string &name, sem::AccessMode mode) {
        const auto found =
            std::find_if(out_.ambient.begin(), out_.ambient.end(), [&](const auto &use) {
                return use.name == name;
            });
        if (found == out_.ambient.end()) {
            out_.ambient.push_back(ResourceSignature::AmbientUse{.name = name, .mode = mode});
            return;
        }
        found->mode = uniteMode(found->mode, mode);
    }

    // ---- reading a signature ----

    /// The operand of a callee's parameter at this node, or nullopt when the node has fewer
    /// operands than the callee has parameters. A call through a value keeps the value itself as
    /// its first `with` input.
    std::optional<draft_node_ref_t> operandOf(
        draft_node_ref_t id, uint8_t calleeParam, size_t calleeWithCount, bool isCall) const {
        const auto norm = draft_.normInputsOf(id);
        const auto with = draft_.withInputsOf(id);
        if (calleeParam < calleeWithCount) {
            const size_t at = size_t(calleeParam) + (isCall ? 1 : 0);
            return at < with.size() ? std::optional(with[at]) : std::nullopt;
        }
        const size_t at = size_t(calleeParam) - calleeWithCount;
        return at < norm.size() ? std::optional(norm[at]) : std::nullopt;
    }

    /// What one node's signature amounts to for this body: its own parameters, in the numbering
    /// the callee uses, read back onto the parameters of this body where the operand is one of
    /// them. Returns why when the signature says something this measurement cannot place.
    std::optional<std::string> readSignature(
        const ResourceSignature &sig, size_t calleeWithCount, bool isCall, draft_node_ref_t id) {
        if (sig.mode != ResourceSignature::Mode::Concrete) {
            return "a callee whose resources are not known";
        }
        if (!sig.captures.empty()) {
            // A capture is a resource of the closure this body belongs to, and which capture of
            // the enclosing function it is cannot be read from here.
            return "a resource the callee captures from the function around it";
        }
        for (const auto &use : sig.params) {
            const auto operand = operandOf(id, use.index, calleeWithCount, isCall);
            if (!operand) {
                return "a call with fewer operands than the callee has parameters";
            }
            if (captured_.contains(*operand)) {
                return "a resource captured from the function around it, passed to the callee";
            }
            const auto port = portIndex_.find(*operand);
            if (port == portIndex_.end()) {
                // The operand is a value the body computed, not one of its caller's: a use
                // through it is not a use of a resource the caller still holds.
                continue;
            }
            addParam(port->second, use.cls, use.name, use.mode);
        }
        for (const auto &use : sig.ambient) {
            addAmbient(use.name, use.mode);
        }
        for (sem::ResourceClass cls : sig.anyOf) {
            out_.anyOf.push_back(cls);
        }
        if (sig.allocates) {
            out_.allocates = sig.allocates;
        }
        out_.external = out_.external || sig.external;
        return std::nullopt;
    }

    // ---- one node ----

    void measureNode(draft_node_ref_t id) {
        const node_handle_t node = draft_.ref(id);
        switch (nodeKindOf(node)) {
        case runtime::GCNodeKind::Oper:
            measureOperator(id, node);
            break;
        case runtime::GCNodeKind::Func:
            measureDirectCall(id, node);
            break;
        case runtime::GCNodeKind::Call:
            measureCallThroughValue(id);
            break;
        default:
            break;
        }
    }

    /// An operator's resources are in the operator table, not on its type (runtime/resources.cpp
    /// reads them the same way): what the table declares, and otherwise the conservative reading
    /// of its traits.
    sem::ResourceSpec specOf(draft_node_ref_t id, const std::string &uri) const {
        const auto &table = camel::core::OperatorTable::instance();
        if (const auto found = table.lookup(uri); found.has_value()) {
            if (const auto *facts = table.semantics(*found);
                facts != nullptr && facts->resources) {
                return *facts->resources;
            }
        }
        const auto traits = table.find(uri);
        return sem::conservativeResources(
            draft_.normInputsOf(id).size(),
            draft_.withInputsOf(id).size(),
            traits.has_value() && traits->pure,
            table.hasExternalEffect(uri));
    }

    void measureOperator(draft_node_ref_t id, node_handle_t node) {
        const auto &op = graph_->operTarget(node);
        if (op == nullptr) {
            unplaced("an operator whose target is not known");
            return;
        }
        const std::string uri(op->uri());
        const sem::ResourceSpec spec = specOf(id, uri);
        for (const sem::ResourceAccess &access : spec.accesses) {
            if (access.binding.kind == sem::ResourceBinding::Kind::Ambient) {
                addAmbient(access.resource.name, access.mode);
                continue;
            }
            if (access.binding.kind == sem::ResourceBinding::Kind::Capture) {
                // Which capture of this closure it is, is not readable from here.
                unplaced(std::format("a captured resource used by '{}'", uri));
                continue;
            }
            const bool isWith = access.binding.kind == sem::ResourceBinding::Kind::With;
            const auto inputs = isWith ? draft_.withInputsOf(id) : draft_.normInputsOf(id);
            // A call through a value keeps the value itself as its first `with` input, so an
            // operator value's `with` operands start after it.
            const size_t at = size_t(access.binding.index) + (isWith ? 1 : 0);
            if (at >= inputs.size()) {
                unplaced(std::format("an operand of '{}' that is not there", uri));
                continue;
            }
            if (captured_.contains(inputs[at])) {
                // A capture of the function around this one: which of its resources it is cannot
                // be read from this body.
                unplaced(std::format("a resource captured from the function around it, used by '{}'", uri));
                continue;
            }
            const auto port = portIndex_.find(inputs[at]);
            if (port == portIndex_.end()) {
                continue; // a value the body computed, not one of its caller's
            }
            addParam(port->second, access.resource.cls, access.resource.name, access.mode);
        }
        out_.external = out_.external || spec.externalEffect;
    }

    /// A call of a function known by name: what it runs is the callee's own measurement. Until
    /// that exists (`Early`), the body cannot be measured; in the fixed point every body has one,
    /// starting from the empty set, and a callee without one is a graph this build did not make.
    void measureDirectCall(draft_node_ref_t id, node_handle_t node) {
        const compile_graph_ptr_t callee = graph_->funcTarget(node);
        if (callee == nullptr) {
            unplaced("a call whose callee is not known");
            return;
        }
        const auto measured = callees_.find(callee.get());
        if (measured == callees_.end()) {
            unplaced(std::format("a call of '{}', which is not measured", callee->name()));
            return;
        }
        const FunctionType *calleeType = callee->funcType();
        if (calleeType == nullptr) {
            unplaced(std::format("a call of '{}', which has no type", callee->name()));
            return;
        }
        // What a caller believes of a function is its clause when it wrote one (the body is held
        // to it where it is declared), and the body's measurement when it wrote none.
        const ResourceSignature &calleeSignature = calleeType->resources().mode == ResourceSignature::Mode::Concrete
                                                  ? calleeType->resources()
                                                  : measured->second;
        if (const auto why =
                readSignature(calleeSignature, calleeType->withTypesCount(), false, id)) {
            unplaced(std::format("{} (in '{}')", *why, callee->name()));
        }
    }

    /// A call through a function value: what it runs is the value's signature, which is the first
    /// `with` input of the call (isEffectNode reads it the same way). A value that writes no
    /// clause promises nothing -- what it reaches is the instantiation's, and any instantiation
    /// may write a cell or print -- so nothing in this body proves the call pure and it is read
    /// conservatively (resource_semantics.h, `Generic`).
    void measureCallThroughValue(draft_node_ref_t id) {
        const auto callee = draft_.withInputsOf(id);
        if (callee.empty()) {
            unplaced("a call with no function value");
            return;
        }
        Type *calleeType = nodeTypeOf(draft_.ref(callee[0]));
        if (calleeType == nullptr || calleeType->code() != TypeCode::Function) {
            unplaced("a call through a value that is not known to be a function");
            return;
        }
        const auto *calleeFunc = tt::as_ptr<FunctionType>(calleeType);
        if (calleeFunc->resources().mode != ResourceSignature::Mode::Concrete) {
            for (sem::ResourceClass cls : kUnplacedClasses) {
                out_.anyOf.push_back(cls);
            }
            out_.external = true;
            return;
        }
        if (const auto why =
                readSignature(calleeFunc->resources(), calleeFunc->withTypesCount(), true, id)) {
            unplaced(*why + " (in a call through a function value)");
        }
    }

    // ---- the declaration's own parameters ----

    /// A `var` parameter is a resource by itself, whether or not the body reaches it (design
    /// principles 3.3).
    void addVarParameters() {
        const size_t withCount = type_->withTypesCount();
        size_t index           = 0;
        for (size_t i = 0; i < withCount + type_->normTypesCount(); ++i) {
            const bool isWith = i < withCount;
            if (!isWith && type_->normIsConstasAt(i - withCount)) {
                continue; // not a runtime parameter
            }
            const bool isVar = isWith ? type_->withIsVarAt(i) : type_->normIsVarAt(i - withCount);
            if (isVar) {
                addParam(index, sem::ResourceClass::Variable, "", sem::AccessMode::ReadModifyWrite);
            }
            ++index;
        }
    }

    void sortAndDedupe() {
        std::sort(out_.params.begin(), out_.params.end(), [](const auto &a, const auto &b) {
            return a.index < b.index;
        });
        std::sort(out_.ambient.begin(), out_.ambient.end(), [](const auto &a, const auto &b) {
            return a.name < b.name;
        });
        std::sort(out_.anyOf.begin(), out_.anyOf.end());
        out_.anyOf.erase(std::unique(out_.anyOf.begin(), out_.anyOf.end()), out_.anyOf.end());
    }

    DraftGraphBuilder *graph_;
    const runtime::GraphDraft &draft_;
    FunctionType *type_;
    const Measurements &callees_;
    const bool settled_;
    ResourceSignature out_;
    std::unordered_map<draft_node_ref_t, size_t> portIndex_;
    std::unordered_set<draft_node_ref_t> captured_;
    std::vector<std::string> unplaced_;
};

} // namespace

std::optional<camel::core::sem::ResourceSignature> Builder::footprintOf(
    DraftGraphBuilder *graph, FootprintMode mode, std::vector<std::string> *unplaced) const {
    return FootprintWalker(graph, inferredResources_, mode == FootprintMode::Settled)
        .run(unplaced);
}

void Builder::measureInferredResources() {
    // What a body touches depends on its callees, which may be written after their caller, in
    // another module, or be the body itself. The measurements are the least fixed point: every
    // body starts as touching nothing and a round adds what its callees have added, so the sets
    // only grow and the iteration ends. A body is recomputed only when a callee it calls changed.
    std::vector<DraftGraphBuilder *> bodies;
    std::unordered_map<DraftGraphBuilder *, std::vector<DraftGraphBuilder *>> callersOf;
    ResourceSignature empty;
    empty.mode = ResourceSignature::Mode::Concrete;
    for (const ModuleUnit *unit : unitOrder_) {
        for (const compile_graph_ptr_t &graph : unit->builtBodies) {
            if (graph->funcType() == nullptr) {
                continue; // the body did not build (an error was reported)
            }
            bodies.push_back(graph.get());
            inferredResources_[graph.get()] = empty;
            const runtime::GraphDraft &draft = graph->draft();
            for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
                if (draft.alive(id) && nodeKindOf(draft.ref(id)) == runtime::GCNodeKind::Func) {
                    if (const compile_graph_ptr_t callee = graph->funcTarget(draft.ref(id))) {
                        callersOf[callee.get()].push_back(graph.get());
                    }
                }
            }
        }
    }
    std::deque<DraftGraphBuilder *> work(bodies.begin(), bodies.end());
    std::unordered_set<DraftGraphBuilder *> queued(bodies.begin(), bodies.end());
    unplacedResources_.clear();
    while (!work.empty()) {
        DraftGraphBuilder *body = work.front();
        work.pop_front();
        queued.erase(body);
        std::vector<std::string> unplaced;
        const auto measured = footprintOf(body, FootprintMode::Settled, &unplaced);
        if (!measured.has_value()) {
            continue;
        }
        unplacedResources_[body] = std::move(unplaced);
        if (inferredResources_[body] == *measured) {
            continue;
        }
        inferredResources_[body] = *measured;
        for (DraftGraphBuilder *caller : callersOf[body]) {
            if (queued.insert(caller).second) {
                work.push_back(caller);
            }
        }
    }
}

void Builder::checkInferredResources() {
    using sem::ResourceSignature;
    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        const auto measured = inferredResources_.find(graph.get());
        if (measured == inferredResources_.end()) {
            continue;
        }
        FunctionType *type = graph->funcType();
        if (type == nullptr) {
            continue;
        }
        const ResourceSignature &promised = type->resources();
        if (promised.mode != ResourceSignature::Mode::Concrete) {
            continue; // the declaration wrote no clause: it promised nothing to break
        }
        if (sem::covers(promised, measured->second)) {
            continue;
        }
        reportResourceViolation(graph, promised, measured->second);
    }
}
} // namespace camel::compile::gir