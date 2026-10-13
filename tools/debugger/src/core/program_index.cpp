/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "program_index.h"
#include "values.h"

#include "camel/core/source/manager.h"
#include "camel/core/type/composite/func.h"

#include <algorithm>
#include <deque>
#include <filesystem>

namespace debugger::core {

using namespace camel::runtime;
namespace fs = std::filesystem;

const char *kindName(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data:
        return "DATA";
    case GCNodeKind::Port:
        return "PORT";
    case GCNodeKind::Cast:
        return "CAST";
    case GCNodeKind::Copy:
        return "COPY";
    case GCNodeKind::Fill:
        return "FILL";
    case GCNodeKind::Accs:
        return "ACCS";
    case GCNodeKind::Brch:
        return "BRCH";
    case GCNodeKind::Join:
        return "JOIN";
    case GCNodeKind::Call:
        return "CALL";
    case GCNodeKind::Bind:
        return "BIND";
    case GCNodeKind::Func:
        return "FUNC";
    case GCNodeKind::Oper:
        return "OPER";
    case GCNodeKind::Sync:
        return "SYNC";
    case GCNodeKind::Gate:
        return "GATE";
    case GCNodeKind::Dref:
        return "DREF";
    }
    return "NODE";
}

namespace {

/// Positions of ordinary ports in ProgramIndex::inheritedPortNames_ start here; `with` ports
/// are below.
constexpr size_t kNormPortBase = size_t{1} << 20;

bool executesKind(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data:
    case GCNodeKind::Port:
    case GCNodeKind::Sync:
    case GCNodeKind::Gate:
    case GCNodeKind::Dref:
    case GCNodeKind::Bind:
        return false;
    default:
        return true;
    }
}

std::string normalisedPath(const std::string &path) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(path), ec);
    if (ec) {
        p = fs::absolute(fs::path(path), ec);
        if (ec) {
            p = fs::path(path);
        }
    }
    std::string text = p.lexically_normal().generic_string();
#if defined(_WIN32)
    for (char &c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
#endif
    return text;
}

} // namespace

/// Stores what the builders report about names; everything else is ignored.
class ProgramIndex::Recorder final : public camel::compile::SemanticRecorder {
  public:
    struct Bound {
        origin_id_t decl;
        std::string name;
        origin_id_t node;
        Type *type;
        bool variable;
    };
    struct Built {
        origin_id_t func;
        std::string name;
    };
    std::vector<Bound> bound;
    std::vector<Built> built;

    void typeExpression(origin_id_t, Type *) override {}
    void valueDeclared(origin_id_t, const std::string &, Type *, bool) override {}
    void expressionTyped(origin_id_t, Type *) override {}
    void valueReferenced(origin_id_t, const std::string &, origin_id_t, Type *) override {}
    void functionReferenced(origin_id_t, const std::string &, std::vector<std::string>) override {}
    void genericReferenced(origin_id_t, const std::string &, origin_id_t) override {}
    void operatorReferenced(origin_id_t, const std::string &, std::vector<std::string>) override {}
    void callResolved(
        origin_id_t, origin_id_t, const std::string &, const std::string &,
        FunctionType *) override {}
    void functionBuilt(
        origin_id_t func, const std::string &name, const std::string &, FunctionType *) override {
        built.push_back({func, name});
    }
    void valueBound(
        origin_id_t decl, const std::string &name, const std::string &, origin_id_t node,
        Type *type, bool variable) override {
        bound.push_back({decl, name, node, type, variable});
    }
};

ProgramIndex::ProgramIndex() : recorder_(std::make_unique<Recorder>()) {}
ProgramIndex::~ProgramIndex() = default;

camel::compile::SemanticRecorder *ProgramIndex::recorder() { return recorder_.get(); }

void ProgramIndex::begin(const camel::core::context::context_ptr_t &ctx) {
    ctx_ = ctx;
    recorder_->bound.clear();
    recorder_->built.clear();
    files_.clear();
    fileBySourceId_.clear();
    fileByPath_.clear();
    locs_.clear();
    functions_.clear();
    bindings_.clear();
    bindingByNode_.clear();
    bindingByFunction_.clear();
    graphs_.clear();
    builtLines_.clear();
    initialLines_.clear();
    runningLines_.clear();
    runningGraphs_.clear();
    compiledKnown_ = false;
    runningKnown_  = false;
}

const SourceLoc &ProgramIndex::loc(origin_t origin) {
    static const SourceLoc none;
    if (origin == camel::source::kInvalidOriginId || !ctx_) {
        return none;
    }
    if (auto it = locs_.find(origin); it != locs_.end()) {
        return it->second;
    }
    SourceLoc out;
    auto sc = ctx_->sourceContext();
    const auto *record = sc ? sc->origin(origin) : nullptr;
    const auto *span   = record ? sc->span(record->primarySpan) : nullptr;
    const auto *file   = span ? sc->sourceFile(span->fileId) : nullptr;
    if (span != nullptr && span->valid() && file != nullptr) {
        auto known = fileBySourceId_.find(static_cast<uint32_t>(span->fileId));
        if (known == fileBySourceId_.end()) {
            const int index = static_cast<int>(files_.size());
            files_.push_back(file->path);
            fileByPath_.emplace(normalisedPath(file->path), index);
            known = fileBySourceId_.emplace(static_cast<uint32_t>(span->fileId), index).first;
        }
        const auto range = sc->resolveSpan(record->primarySpan);
        out.file         = known->second;
        out.line         = static_cast<int>(range.start.line) + 1;
        out.column       = static_cast<int>(range.start.character) + 1;
        out.endLine      = static_cast<int>(range.end.line) + 1;
        out.endColumn    = static_cast<int>(range.end.character) + 1;
        out.startByte    = span->startOffset;
        out.endByte      = span->endOffset;
    }
    return locs_.emplace(origin, out).first->second;
}

const std::string &ProgramIndex::filePath(int file) const {
    static const std::string none;
    return file >= 0 && static_cast<size_t>(file) < files_.size() ? files_[file] : none;
}

int ProgramIndex::fileOf(const std::string &path) {
    auto it = fileByPath_.find(normalisedPath(path));
    return it == fileByPath_.end() ? -1 : it->second;
}

int ProgramIndex::functionOf(const SourceLoc &loc) const {
    if (!loc.valid()) {
        return -1;
    }
    int best = -1;
    for (size_t i = 0; i < functions_.size(); ++i) {
        const auto &f = functions_[i];
        if (f.file != loc.file || f.startByte > loc.startByte || f.endByte < loc.endByte) {
            continue;
        }
        if (f.startByte == loc.startByte && f.endByte == loc.endByte) {
            continue; // the function value itself belongs to the function around it
        }
        if (best < 0 ||
            f.endByte - f.startByte < functions_[best].endByte - functions_[best].startByte) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

void ProgramIndex::finishBindings() {
    for (const auto &b : recorder_->built) {
        const SourceLoc &l = loc(b.func);
        if (!l.valid()) {
            continue;
        }
        const bool known = std::any_of(functions_.begin(), functions_.end(), [&](const auto &f) {
            return f.file == l.file && f.startByte == l.startByte && f.endByte == l.endByte;
        });
        if (!known) {
            functions_.push_back({l.file, l.startByte, l.endByte, b.name});
        }
    }
    for (const auto &b : recorder_->bound) {
        if (b.node == camel::source::kInvalidOriginId) {
            continue; // a parameter: found as a port of the graph
        }
        const SourceLoc &l = loc(b.decl);
        Binding binding;
        binding.name     = b.name;
        binding.decl     = b.decl;
        binding.node     = b.node;
        binding.type     = b.type;
        binding.variable = b.variable;
        binding.function = functionOf(l);
        binding.line     = l.line;
        const auto index = static_cast<uint32_t>(bindings_.size());
        bindings_.push_back(std::move(binding));
        bindingByNode_.emplace(b.node, index);
        bindingByFunction_.emplace(bindings_.back().function, index);
    }
}

void ProgramIndex::collect(
    GCGraph *root, std::map<LineKey, LineNodes> &into, std::vector<GCGraph *> *graphs) {
    if (root == nullptr) {
        return;
    }
    std::unordered_set<GCGraph *> seen{root};
    std::deque<GCGraph *> queue{root};
    while (!queue.empty()) {
        GCGraph *g = queue.front();
        queue.pop_front();
        if (graphs) {
            graphs->push_back(g);
        }
        if (g->hasNodePayload()) {
            for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
                const GCNode *n = *it;
                if (n != nullptr && n->kind == GCNodeKind::Brch && graphs != nullptr) {
                    // An arm that is a call of a subgraph: that graph is the arm.
                    for (const GCBranchArm &arm : g->branchArmsOf(it.ref())) {
                        for (gc_node_ref_t ref : {arm.head, arm.tail}) {
                            const GCNode *armNode =
                                ref != kInvalidNodeRef ? g->node(ref) : nullptr;
                            if (armNode != nullptr && armNode->kind == GCNodeKind::Func) {
                                if (GCGraph *callee = g->directCalleeGraphOf(ref)) {
                                    armGraphs_.insert(callee);
                                }
                            }
                        }
                    }
                }
                const origin_t o = g->nodeOrigin(it.ref());
                if (n == nullptr || o == 0) {
                    continue;
                }
                const SourceLoc &l = loc(o);
                if (!l.valid()) {
                    continue;
                }
                auto &line = into[LineKey{l.file, l.line}];
                line.all.insert(o);
                if (executesKind(n->kind)) {
                    line.executing.insert(o);
                }
            }
        }
        g->traceGraphs([&](GCGraph *next) {
            if (seen.insert(next).second) {
                queue.push_back(next);
            }
        });
    }
}

void ProgramIndex::compiled(GCGraph *root) {
    finishBindings();
    // Every node the graph builder created has an origin of stage GIR, whether or not the
    // node made it into a graph: that is how "built but not executed" is told from "no code".
    if (auto sc = ctx_ ? ctx_->sourceContext() : nullptr) {
        for (origin_t id = 1;; ++id) {
            const auto *record = sc->origin(id);
            if (record == nullptr) {
                break;
            }
            if (record->stage != camel::source::OriginStage::GIR ||
                record->kind != camel::source::OriginKind::GirNode) {
                continue;
            }
            const SourceLoc &l = loc(id);
            if (l.valid()) {
                builtLines_.insert(LineKey{l.file, l.line});
            }
        }
    }
    collect(root, initialLines_, nullptr);
    compiledKnown_ = true;
}

void ProgramIndex::running(GCGraph *root) {
    runningLines_.clear();
    runningGraphs_.clear();
    armGraphs_.clear();
    inheritedPortNames_.clear();
    graphs_.clear(); // graphs of an earlier run may be gone
    collect(root, runningLines_, &runningGraphs_);
    runningKnown_ = true;
}

LineResolution ProgramIndex::resolveLine(const std::string &path, int line) {
    LineResolution out;
    const int file = fileOf(path);
    if (file < 0) {
        out.state = LineState::UnknownFile;
        return out;
    }
    const LineKey key{file, line};
    const auto &current = runningKnown_ ? runningLines_ : initialLines_;
    if (auto it = current.find(key); it != current.end()) {
        if (!it->second.executing.empty()) {
            out.state = LineState::Executable;
            out.origins.assign(it->second.executing.begin(), it->second.executing.end());
            return out;
        }
        out.state = LineState::ConstantOnly;
        return out;
    }
    if (runningKnown_ && initialLines_.count(key) != 0) {
        out.state = LineState::Removed;
    } else if (builtLines_.count(key) != 0) {
        out.state = LineState::NotDepended;
    } else {
        out.state = LineState::NoCode;
    }
    return out;
}

std::vector<const Binding *> ProgramIndex::bindingsOfNode(origin_t node) const {
    std::vector<const Binding *> out;
    auto [first, last] = bindingByNode_.equal_range(node);
    for (auto it = first; it != last; ++it) {
        out.push_back(&bindings_[it->second]);
    }
    return out;
}

std::vector<const Binding *> ProgramIndex::bindingsOfFunction(int function) const {
    std::vector<const Binding *> out;
    auto [first, last] = bindingByFunction_.equal_range(function);
    for (auto it = first; it != last; ++it) {
        out.push_back(&bindings_[it->second]);
    }
    std::sort(out.begin(), out.end(), [](const Binding *a, const Binding *b) {
        return a->line != b->line ? a->line < b->line : a->decl < b->decl;
    });
    return out;
}

const NodeInfo *GraphInfo::nodeOfInstruction(origin_t origin, int resultSlot) const {
    if (origin != 0) {
        auto [first, last]    = byOrigin.equal_range(origin);
        const NodeInfo *match = nullptr;
        for (auto it = first; it != last; ++it) {
            const NodeInfo &n = nodes[it->second];
            if (resultSlot != 0 && n.slot == resultSlot) {
                return &n;
            }
            if (match == nullptr || (n.executes && !match->executes)) {
                match = &n;
            }
        }
        if (match != nullptr) {
            return match;
        }
    }
    if (resultSlot > 0) {
        if (auto it = bySlot.find(resultSlot); it != bySlot.end()) {
            return &nodes[it->second];
        }
    }
    return nullptr;
}

GraphInfo &ProgramIndex::graph(GCGraph *g) {
    if (auto it = graphs_.find(g); it != graphs_.end()) {
        return *it->second;
    }
    auto info      = std::make_unique<GraphInfo>();
    info->graph    = g;
    info->name     = g->name();
    info->stableId = g->stableId();

    // Names of the ports, by position: `with` parameters, then ordinary ones, then captures.
    std::unordered_map<gc_node_ref_t, std::string> portNames;
    if (auto *type = g->funcType()) {
        const auto names   = type->argNames();
        const auto with    = g->withPorts();
        const auto norm    = g->normPorts();
        const auto closure = g->closureNodes();
        if (names.size() == with.size() + norm.size()) {
            for (size_t i = 0; i < with.size(); ++i) {
                portNames[with[i]] = std::string(names[i]);
            }
            for (size_t i = 0; i < norm.size(); ++i) {
                portNames[norm[i]] = std::string(names[with.size() + i]);
            }
        }
        const auto captured = type->closureRefs();
        for (size_t i = 0; i < closure.size() && i < captured.size(); ++i) {
            portNames[closure[i]] = std::string(captured[i]);
        }
    }
    // A graph the compiler made itself (the arm of a branch) takes the values it uses from the
    // function around it as unnamed ports: they are named after what its caller passes there.
    if (auto inherited = inheritedPortNames_.find(g); inherited != inheritedPortNames_.end()) {
        const auto with = g->withPorts();
        const auto norm = g->normPorts();
        for (const auto &[position, name] : inherited->second) {
            const bool isNorm  = position >= kNormPortBase;
            const size_t index = isNorm ? position - kNormPortBase : position;
            const auto ports   = isNorm ? norm : with;
            if (index < ports.size()) {
                portNames.emplace(ports[index], name);
            }
        }
    }

    std::unordered_map<int, size_t> functionVotes;
    if (g->hasNodePayload()) {
        for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
            const GCNode *n = *it;
            if (n == nullptr) {
                continue;
            }
            NodeInfo node;
            node.ref      = it.ref();
            node.kind     = n->kind;
            node.slot     = n->dataIndex;
            node.origin   = g->nodeOrigin(node.ref);
            node.loc      = loc(node.origin);
            node.function = functionOf(node.loc);
            node.executes = executesKind(n->kind);
            switch (n->kind) {
            case GCNodeKind::Oper: {
                const auto *body = g->nodeBodyAs<GCOperBody>(node.ref);
                node.label       = std::string(body->uri());
            } break;
            case GCNodeKind::Func: {
                GCGraph *callee = g->directCalleeGraphOf(node.ref);
                node.label      = callee ? callee->name() : std::string("?");
                if (callee != nullptr && callee->name().rfind("__g_", 0) == 0) {
                    // What is passed to a compiler-made graph names its ports (see above).
                    const auto nameOf = [&](gc_node_ref_t input) -> std::string {
                        if (auto port = portNames.find(input); port != portNames.end()) {
                            return port->second;
                        }
                        const auto bound = bindingsOfNode(g->nodeOrigin(input));
                        return bound.empty() ? std::string() : bound.front()->name;
                    };
                    auto &names = inheritedPortNames_[callee];
                    size_t i    = 0;
                    for (gc_node_ref_t input : g->withInputsOf(node.ref)) {
                        if (std::string name = nameOf(input); !name.empty()) {
                            names.emplace(i, std::move(name));
                        }
                        ++i;
                    }
                    i = 0;
                    for (gc_node_ref_t input : g->normInputsOf(node.ref)) {
                        if (std::string name = nameOf(input); !name.empty()) {
                            names.emplace(kNormPortBase + i, std::move(name));
                        }
                        ++i;
                    }
                }
            } break;
            case GCNodeKind::Accs: {
                const auto *body = g->nodeBodyAs<GCAccsBody>(node.ref);
                node.label       = body->accsKind == GCAccsKind::TupleIndex
                                       ? "." + std::to_string(body->value)
                                       : "." + std::string(body->key());
            } break;
            case GCNodeKind::Port: {
                auto name  = portNames.find(node.ref);
                node.label = name != portNames.end() ? name->second : std::string("port");
            } break;
            case GCNodeKind::Data: {
                const auto *types = g->staticDataType();
                ::Tuple *area     = g->staticArea();
                const size_t idx  = node.slot < 0 ? static_cast<size_t>(-node.slot) : 0;
                if (idx != 0 && types != nullptr && area != nullptr && idx < area->size() &&
                    idx < types->size()) {
                    node.label = render({area->get<slot_t>(idx), types->typeAt(idx)}, 40).value;
                } else {
                    node.label = "const";
                }
            } break;
            default:
                node.label = kindName(n->kind);
                break;
            }
            const auto index = static_cast<uint32_t>(info->nodes.size());
            info->byRef.emplace(node.ref, index);
            if (node.origin != 0) {
                info->byOrigin.emplace(node.origin, index);
            }
            if (node.slot != 0) {
                info->bySlot.emplace(node.slot, index);
            }
            if (node.loc.valid() && node.executes) {
                ++functionVotes[node.function];
            }
            info->nodes.push_back(std::move(node));
        }
    }

    // The function of the source the graph was built from: the one whose span is the span
    // of the graph's origin; for a graph without one, where most of its nodes were written.
    if (auto sc = ctx_ ? ctx_->sourceContext() : nullptr) {
        origin_t graphOrigin = sc->debugMap().graphOrigin(info->stableId);
        if (graphOrigin == 0) {
            graphOrigin = sc->debugMap().runtimeGraphOrigin(reinterpret_cast<uintptr_t>(g));
        }
        const SourceLoc &l = loc(graphOrigin);
        if (l.valid()) {
            for (size_t i = 0; i < functions_.size(); ++i) {
                const auto &f = functions_[i];
                if (f.file == l.file && f.startByte == l.startByte && f.endByte == l.endByte) {
                    info->function = static_cast<int>(i);
                    break;
                }
            }
        }
    }
    if (info->function < 0) {
        size_t best = 0;
        for (const auto &[function, votes] : functionVotes) {
            if (votes > best) {
                best           = votes;
                info->function = function;
            }
        }
    }

    // What the user calls the graph. A function is called by its name. A graph the compiler
    // named itself (`__g_7__`) is a lambda or the arm of a branch: it is shown as that, in the
    // named function it was written in, with the line it starts on.
    // DESIGN-QUESTION(arm-frames): an arm is shown as the frame it is, not folded into the
    // frame of its function.
    info->display = info->name;
    if (info->name.rfind("__g_", 0) == 0) {
        std::string owner;
        for (GCGraph *outer = g->outerGraph(); outer != nullptr; outer = outer->outerGraph()) {
            if (outer->name().rfind("__", 0) != 0) {
                owner = outer->name();
                break;
            }
        }
        int line = 0;
        for (const NodeInfo &n : info->nodes) {
            if (n.loc.valid() && (line == 0 || n.loc.line < line)) {
                line = n.loc.line;
            }
        }
        info->arm     = armGraphs_.count(g) != 0;
        info->display = (owner.empty() ? std::string() : owner + " ") + "[" +
                        (info->arm ? "arm" : "lambda") +
                        (line > 0 ? " at line " + std::to_string(line) : std::string()) + "]";
    }
    return *graphs_.emplace(g, std::move(info)).first->second;
}

} // namespace debugger::core
