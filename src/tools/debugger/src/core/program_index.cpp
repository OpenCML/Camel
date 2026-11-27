// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "program_index.h"
#include "camel/runtime/control_regions.h"
#include "camel/runtime/oper_shape.h"
#include "camel/runtime/program.h"
#include "values.h"

#include "camel/core/module/module.h"
#include "camel/core/source/manager.h"
#include "camel/core/type/composite/func.h"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <string_view>

namespace debugger::core {

using namespace camel::runtime;
namespace fs = std::filesystem;

namespace {

/// The implementation a view belongs to. The program holds both faces of every graph it knows,
/// and a graph makes its view once and keeps it, so the view's address names the graph. A reader
/// that needs what only the representation answers -- a node's payload, the graph's id, the
/// table of graphs it refers to -- asks for the graph this way rather than the view growing a
/// second face for it.
const camel::runtime::GCGraph *
implOf(const camel::core::context::context_ptr_t &ctx, const camel::graph::GraphView *view) {
    if (ctx == nullptr || view == nullptr) {
        return nullptr;
    }
    const camel::runtime::Program *program = ctx->runtimeProgram();
    if (program == nullptr) {
        return nullptr;
    }
    for (camel::runtime::GCGraph *graph : program->graphs()) {
        if (graph != nullptr && &graph->view() == view) {
            return graph;
        }
    }
    return nullptr;
}

/// The view of an implementation, which is what this index files a graph under.
const camel::graph::GraphView *viewOf(const camel::runtime::GCGraph *graph) {
    return graph == nullptr ? nullptr : &graph->view();
}

} // namespace

const char *kindName(GCNodeKind kind) {
    // The kinds a frozen graph holds are named once (`camel/runtime/oper_shape.h#nodeKindName`), which
    // is also where a kind standing for one of the four operators is named by the operator. The
    // three reserved kinds are this index's own answer, "NODE": never part of a frozen graph
    // (see GCNodeKind), so the shared table's names for them are not printed here.
    switch (kind) {
    case GCNodeKind::Bind:
    case GCNodeKind::Dref:
    case GCNodeKind::Dead:
        return "NODE";
    default:
        break;
    }
    const std::string_view name = camel::runtime::nodeKindName(kind);
    return name.empty() ? "NODE" : name.data();
}

std::string whyNotExecuted(LineState state, const std::vector<std::string> &names) {
    const auto quoted = [](const std::string &name) { return "`" + name + "`"; };
    if (state == LineState::NotCalled) {
        return names.empty() ? std::string("nothing calls its function")
                             : "nothing calls " + quoted(names.front());
    }
    if (names.empty()) {
        return "nothing uses its result"; // a statement such as a call outside a sync block
    }
    if (names.size() == 1) {
        return "nothing uses the value of " + quoted(names.front());
    }
    std::string list;
    for (size_t i = 0; i < names.size(); ++i) {
        list += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + quoted(names[i]);
    }
    return "nothing uses the values of " + list;
}

namespace {

/// Positions of ordinary ports in ProgramIndex::inheritedPortNames_ start here; `with` ports
/// are below.
constexpr size_t kNormPortBase = size_t{1} << 20;

bool executesKind(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data:
    case GCNodeKind::Port:
    case GCNodeKind::Gate:
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
        std::string graph; // id of the graph the node was made in
        origin_id_t node;
        Type *type;
        bool variable;
    };
    struct Built {
        origin_id_t func;
        std::string name;
        std::string graph; // graph id
    };
    std::vector<Bound> bound;
    std::vector<Built> built;

    void typeExpression(origin_id_t, Type *) override {}
    void valueDeclared(origin_id_t, const std::string &, Type *, bool) override {}
    void expressionTyped(origin_id_t, Type *) override {}
    void valueReferenced(origin_id_t, const std::string &, origin_id_t, Type *) override {}
    void functionReferenced(
        origin_id_t, const std::string &, std::vector<camel::runtime::GraphId>) override {}
    void genericReferenced(origin_id_t, const std::string &, origin_id_t) override {}
    void operatorReferenced(origin_id_t, const std::string &, std::vector<std::string>) override {}
    void callResolved(
        origin_id_t, origin_id_t, camel::runtime::GraphId, const std::string &,
        FunctionType *, bool) override {}
    void functionBuilt(
        origin_id_t func, const std::string &name, camel::runtime::GraphId graph,
        FunctionType *) override {
        built.push_back({func, name, camel::runtime::toText(graph)});
    }
    void valueBound(
        origin_id_t decl, const std::string &name, camel::runtime::GraphId graph, origin_id_t node,
        Type *type, bool variable) override {
        bound.push_back({decl, name, camel::runtime::toText(graph), node, type, variable});
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
    initialOrigins_.clear();
    liveGraphIds_.clear();
    liveOrigins_.clear();
    initialGraphs_.clear();
    runningGraphs_.clear();
    runningGraphIds_.clear();
    armGraphIds_.clear();
    armOwners_.clear();
    inheritedPortNames_.clear();
    moduleByFile_.clear();
    modulePaths_.clear();
    entryPath_.clear();
    moduleNames_.clear();
    topLevelNames_.clear();
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
            SourceFunction f;
            f.file      = l.file;
            f.startByte = l.startByte;
            f.endByte   = l.endByte;
            f.line      = l.line;
            f.endLine   = l.endLine;
            f.name      = b.name; // made full by nameFunctions
            functions_.push_back(std::move(f));
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
        binding.file     = l.file;
        binding.line     = l.line;
        binding.graph    = b.graph;
        if (binding.function < 0) {
            // A value of a module's top level, by its full name.
            const std::string &module = moduleOfFile(binding.file);
            topLevelNames_.insert(module.empty() ? binding.name : module + "::" + binding.name);
        }
        const auto index = static_cast<uint32_t>(bindings_.size());
        bindings_.push_back(std::move(binding));
        bindingByNode_.emplace(b.node, index);
        bindingByFunction_.emplace(bindings_.back().function, index);
    }
}

void ProgramIndex::collect(
    const camel::graph::GraphView *root, std::map<LineKey, LineNodes> &lines, std::vector<const camel::graph::GraphView *> &graphs,
    std::unordered_set<origin_t> *origins, ArmOwners *arms) {
    const camel::runtime::GCGraph *rootImpl = implOf(ctx_, root);
    const camel::runtime::Program *program =
        rootImpl != nullptr ? ctx_->programOfRuntimeGraph(*rootImpl) : nullptr;
    if (program == nullptr) {
        return;
    }
    std::unordered_set<const camel::runtime::GCGraph *> seen{rootImpl};
    std::deque<const camel::runtime::GCGraph *> queue{rootImpl};
    while (!queue.empty()) {
        auto *g = queue.front();
        queue.pop_front();
        graphs.push_back(viewOf(g));
        if (g->hasNodePayload()) {
            for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
                const GCNode *n  = *it;
                const origin_t o = g->nodeOrigin(it.ref());
                if (n == nullptr || o == 0) {
                    continue;
                }
                if (origins != nullptr) {
                    origins->insert(o);
                }
                const SourceLoc &l = loc(o);
                if (!l.valid()) {
                    continue;
                }
                auto &line = lines[LineKey{l.file, l.line}];
                line.all.insert(o);
                if (executesKind(n->kind)) {
                    line.executing.insert(o);
                }
            }
        }
        program->traceGraphs(*g, [&](camel::runtime::GCGraph *next) {
            if (seen.insert(next).second) {
                queue.push_back(next);
            }
        });
    }
}

void ProgramIndex::compiled(const camel::graph::GraphView *root) {
    findModules();
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
                builtLines_[LineKey{l.file, l.line}].push_back(id);
            }
        }
    }
    collect(root, initialLines_, initialGraphs_, &initialOrigins_, nullptr);
    findLiveGraphs(root);
    nameFunctions();
    compiledKnown_ = true;
}

void ProgramIndex::running(const camel::graph::GraphView *root) {
    runningLines_.clear();
    runningGraphs_.clear();
    armOwners_.clear();
    inheritedPortNames_.clear();
    graphs_.clear(); // graphs of an earlier run may be gone
    collect(root, runningLines_, runningGraphs_, nullptr, &armOwners_);
    runningGraphIds_.clear();
    for (auto *g : runningGraphs_) {
        if (const auto *impl = implOf(ctx_, g); impl != nullptr) {
            runningGraphIds_.insert(camel::runtime::toText(impl->id()));
        }
    }
    armGraphIds_.clear();
    for (const auto &[arm, owners] : armOwners_) {
        if (const auto *impl = implOf(ctx_, arm); impl != nullptr) {
            armGraphIds_.insert(camel::runtime::toText(impl->id()));
        }
    }
    findLiveGraphs(root);
    runningKnown_ = true;
}

void ProgramIndex::findLiveGraphs(const camel::graph::GraphView *root) {
    liveGraphIds_.clear();
    liveOrigins_.clear();
    const camel::runtime::GCGraph *rootImpl = implOf(ctx_, root);
    const camel::runtime::Program *program =
        rootImpl != nullptr ? ctx_->programOfRuntimeGraph(*rootImpl) : nullptr;
    if (program == nullptr) {
        return;
    }
    // GCGraph::dependencies() are the graphs of the FUNC nodes (calls, the arms of branches)
    // and staticGraphRefs() the graphs of the function values in the static area; subGraphs()
    // (every function declared in a graph) are left out: being declared is not being called.
    std::unordered_set<const camel::runtime::GCGraph *> seen{rootImpl};
    std::deque<const camel::runtime::GCGraph *> queue{rootImpl};
    while (!queue.empty()) {
        auto *g = queue.front();
        queue.pop_front();
        liveGraphIds_.insert(camel::runtime::toText(g->id()));
        if (g->hasNodePayload()) {
            for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
                if (const origin_t o = g->nodeOrigin(it.ref()); o != 0) {
                    liveOrigins_.insert(o);
                }
            }
        }
        for (const auto &next : {program->dependencies(*g), program->staticGraphRefs(*g)}) {
            for (auto *graph : next) {
                if (graph != nullptr && seen.insert(graph).second) {
                    queue.push_back(graph);
                }
            }
        }
    }
}

// ---- names --------------------------------------------------------------------------------

namespace {

/// `lib::first<int>` -> `lib::first`: the name of the function a generic instance is of.
std::string withoutTypeArguments(const std::string &name) {
    if (name.empty() || name.back() != '>') {
        return name;
    }
    const size_t open = name.find('<');
    return open == std::string::npos || open == 0 ? name : name.substr(0, open);
}

/// A graph the compiler named itself: the entry graph, a lambda, the arm of a branch.
bool isCompilerNamed(const std::string &name) { return name.rfind("__", 0) == 0; }

} // namespace

void ProgramIndex::findModules() {
    if (!ctx_) {
        return;
    }
    const auto entry = ctx_->mainModule();
    if (entry) {
        entryPath_ = normalisedPath(entry->path());
    }
    for (const auto &module : ctx_->allUserModules()) {
        if (!module || module == entry) {
            continue;
        }
        modulePaths_.emplace_back(module->name(), normalisedPath(module->path()));
        moduleNames_.insert(module->name());
    }
}

const std::string &ProgramIndex::moduleOfFile(int file) {
    static const std::string none;
    if (file < 0) {
        return none;
    }
    if (auto it = moduleByFile_.find(file); it != moduleByFile_.end()) {
        return it->second;
    }
    std::string module;
    const std::string path = normalisedPath(filePath(file));
    if (path != entryPath_) {
        for (const auto &[name, modulePath] : modulePaths_) {
            if (modulePath == path) {
                module = name;
                break;
            }
        }
    }
    return moduleByFile_.emplace(file, std::move(module)).first->second;
}

void ProgramIndex::nameFunctions() {
    // A graph, as a value: the table names one past the call and reads its name through the face,
    // which is the same builder-written name (`name`) a frozen graph stores.
    std::unordered_map<std::string, const camel::graph::GraphView *> byGraphId;
    for (auto *g : initialGraphs_) {
        if (const auto *impl = implOf(ctx_, g); impl != nullptr) {
            byGraphId.emplace(camel::runtime::toText(impl->id()), g);
        }
    }
    for (const auto &b : recorder_->built) {
        const SourceLoc &l = loc(b.func);
        if (!l.valid()) {
            continue;
        }
        for (auto &f : functions_) {
            if (f.file != l.file || f.startByte != l.startByte || f.endByte != l.endByte) {
                continue;
            }
            const auto graph = byGraphId.find(b.graph);
            if (graph == byGraphId.end()) {
                break; // no graph of it is in the program (yet: another instance may be)
            }
            if (liveGraphIds_.count(b.graph) != 0) {
                f.reachable = true;
            }
            // The builder names the graph with the full name (the module of another module's
            // function, the namespaces, the type arguments of an instance).
            const std::string_view name = graph->second->name();
            if (!isCompilerNamed(std::string(name))) {
                f.name = withoutTypeArguments(std::string(name));
            }
            break;
        }
    }
    for (auto &f : functions_) {
        if (!f.reachable && !f.name.empty()) {
            // No graph to take the name from: qualified with its module at least.
            if (const std::string &module = moduleOfFile(f.file);
                !module.empty() && f.name.rfind(module + "::", 0) != 0) {
                f.name = module + "::" + f.name;
            }
        }
    }
}

std::string ProgramIndex::capturedName(const camel::graph::GraphView *graph, std::string_view shown) {
    // The capturing module writes a value of another module with that module's name, and one
    // of its own as it is written there (GIR builder, captureNameOf).
    const auto colon = shown.find("::");
    if (colon != std::string_view::npos &&
        moduleNames_.count(std::string(shown.substr(0, colon))) != 0) {
        return std::string(shown);
    }
    // A value of the capturing module itself: only a value of the module's top level is
    // qualified (an arm or a lambda captures the locals of its function, which have no full
    // name), with the module unless it is the entry module.
    int file = -1;
    if (auto sc = ctx_ ? ctx_->sourceContext() : nullptr) {
        // The origin of a graph is the representation's (debugMap holds the implementation's
        // graph id), so the pairing is asked for here.
        if (const auto *impl = implOf(ctx_, graph); impl != nullptr) {
            file = loc(sc->debugMap().graphOrigin(impl->id())).file;
        }
    }
    const std::string &module = moduleOfFile(file);
    if (module.empty() || topLevelNames_.count(module + "::" + std::string(shown)) == 0) {
        return std::string(shown);
    }
    return module + "::" + std::string(shown);
}

FunctionResolution ProgramIndex::resolveFunction(const std::string &written) const {
    FunctionResolution out;
    std::string name = written;
    // The entry module may qualify its own names with its own name (design principles 10.2).
    if (const auto entry = ctx_ ? ctx_->mainModule() : nullptr; entry) {
        const std::string self = entry->name() + "::";
        if (name.rfind(self, 0) == 0 && moduleNames_.count(entry->name()) == 0) {
            name = name.substr(self.size());
        }
    }
    const auto &graphs = runningKnown_ ? runningGraphs_ : initialGraphs_;
    // Exact: the full name, or every instance of a generic function. Without the module: the
    // full names `m::<name>` of modules m; one module only, or it is ambiguous.
    std::vector<const camel::graph::GraphView *> exact;
    std::map<std::string, std::vector<const camel::graph::GraphView *>> byModule;
    for (auto *g : graphs) {
        const std::string full(g->name());
        if (isCompilerNamed(full)) {
            continue;
        }
        const std::string base = withoutTypeArguments(full);
        if (full == name || base == name) {
            exact.push_back(g);
            continue;
        }
        for (const std::string *candidate : {&full, &base}) {
            const size_t colon = candidate->find("::");
            if (colon != std::string::npos && moduleNames_.count(candidate->substr(0, colon)) != 0 &&
                candidate->substr(colon + 2) == name) {
                byModule[candidate->substr(0, colon)].push_back(g);
                break;
            }
        }
    }
    std::vector<const camel::graph::GraphView *> chosen = exact;
    if (chosen.empty() && byModule.size() == 1) {
        chosen = byModule.begin()->second;
    }
    if (!chosen.empty()) {
        out.found    = true;
        out.fullName = withoutTypeArguments(std::string(chosen.front()->name()));
        if (chosen.front()->name() == name) {
            out.fullName = name; // one instance was asked for
        }
        bool called = false;
        for (auto *g : chosen) {
            const auto *impl = implOf(ctx_, g);
            if (impl == nullptr) {
                continue;
            }
            const std::string id = camel::runtime::toText(impl->id());
            out.graphs.push_back(id);
            called = called || liveGraphIds_.count(id) != 0;
        }
        if (!called) {
            out.message = "Nothing in the program calls `" + out.fullName + "`.";
        }
        return out;
    }
    if (byModule.size() > 1) {
        std::string names;
        for (const auto &[module, found] : byModule) {
            names += (names.empty() ? "" : ", ") +
                     withoutTypeArguments(std::string(found.front()->name()));
        }
        out.message = "`" + written + "` is ambiguous: " + names + ". Write the full name.";
        return out;
    }
    const bool declared = std::any_of(functions_.begin(), functions_.end(), [&](const auto &f) {
        // The full name, or the full name without its module.
        return f.name == name ||
               (f.name.size() > name.size() + 2 &&
                f.name.compare(f.name.size() - name.size(), name.size(), name) == 0 &&
                f.name.compare(f.name.size() - name.size() - 2, 2, "::") == 0);
    });
    if (runningKnown_ && declared) {
        out.message = "`" + written + "` is not in the graph that runs: a pass inlined or removed it.";
    } else if (declared) {
        out.message = "`" + written + "` is not called by the program.";
    } else {
        out.message = "No function named `" + written + "` in the program.";
    }
    return out;
}

// ---- lines --------------------------------------------------------------------------------

std::vector<std::string> ProgramIndex::unusedNamesOn(int file, int line, int function) const {
    std::vector<std::string> out;
    for (const Binding &b : bindings_) {
        if (b.file != file || b.line != line || b.function != function ||
            initialOrigins_.count(b.node) != 0) {
            continue;
        }
        if (std::find(out.begin(), out.end(), b.name) == out.end()) {
            out.push_back(b.name); // once: a generic function binds it once per instance
        }
    }
    return out;
}

LineResolution ProgramIndex::resolveLine(const std::string &path, int line) {
    LineResolution out;
    out.line       = line;
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
            out.called = std::any_of(out.origins.begin(), out.origins.end(), [&](origin_t o) {
                return liveOrigins_.count(o) != 0;
            });
            if (!out.called) {
                const SourceFunction *f = this->function(functionOf(loc(out.origins.front())));
                out.names               = {f != nullptr ? f->name : std::string()};
            }
            return out;
        }
        out.state = LineState::ConstantOnly;
        return out;
    }
    if (runningKnown_ && initialLines_.count(key) != 0) {
        out.state = LineState::Removed;
        return out;
    }
    const auto built = builtLines_.find(key);
    if (built == builtLines_.end()) {
        out.state = LineState::NoCode;
        return out;
    }
    // Built, and in no graph of the program: either the function it is written in is not
    // called at all, or nothing uses what the line computes.
    const int function = functionOf(loc(built->second.front()));
    if (const SourceFunction *f = this->function(function); f != nullptr && !f->reachable) {
        out.state = LineState::NotCalled;
        out.names = {f->name};
        return out;
    }
    out.state = LineState::NotDepended;
    out.names = unusedNamesOn(file, line, function);
    return out;
}

std::vector<DroppedLine>
ProgramIndex::droppedLines(int function, int firstLine, int lastLine) const {
    std::vector<DroppedLine> out;
    const SourceFunction *f = this->function(function);
    if (f == nullptr || !f->reachable || firstLine > lastLine) {
        return out;
    }
    for (auto it = builtLines_.lower_bound(LineKey{f->file, firstLine});
         it != builtLines_.end() && it->first.file == f->file && it->first.line <= lastLine;
         ++it) {
        if (initialLines_.count(it->first) != 0 || runningLines_.count(it->first) != 0) {
            continue;
        }
        // Code of this function, not of a lambda written in it.
        const bool ours = std::any_of(it->second.begin(), it->second.end(), [&](origin_t o) {
            const auto known = locs_.find(o);
            return known != locs_.end() && functionOf(known->second) == function;
        });
        if (!ours) {
            continue;
        }
        DroppedLine dropped;
        dropped.file     = f->file;
        dropped.line     = it->first.line;
        dropped.function = function;
        dropped.names    = unusedNamesOn(f->file, it->first.line, function);
        out.push_back(std::move(dropped));
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

GraphInfo &ProgramIndex::graph(const camel::graph::GraphView *g) {
    if (auto it = graphs_.find(g); it != graphs_.end()) {
        return *it->second;
    }
    const camel::runtime::GCGraph *gImpl = implOf(ctx_, g);
    ASSERT(gImpl != nullptr, "A graph of the debugged program is in no program.");
    const camel::runtime::Program *programOfGraph = ctx_->programOfRuntimeGraph(*gImpl);
    ASSERT(programOfGraph != nullptr, "A graph of the debugged program is in no program.");
    auto info      = std::make_unique<GraphInfo>();
    info->graph    = g;
    info->name     = std::string(g->name());
    info->graphId  = camel::runtime::toText(gImpl->id());

    // Names of the ports. A function's ports are its declared `with` parameters, its other
    // parameters, and one port per value it captures: a closure node, or, for a function that
    // is called directly, a `with` port after the declared ones (as the graph dumps name them,
    // passes/trans/dot/graphviz.cpp). A captured value is shown by its full name.
    std::unordered_map<gc_node_ref_t, std::string> portNames;
    if (auto *type = g->funcType()) {
        const auto names    = type->argNames();
        const auto with     = gImpl->withPorts();
        const auto norm     = gImpl->normPorts();
        const auto closure  = gImpl->closureNodes();
        const auto captured = gImpl->captureNames();
        const size_t declaredWith = closure.empty() && captured.size() <= with.size()
                                        ? with.size() - captured.size()
                                        : with.size();
        if (names.size() == declaredWith + norm.size()) {
            for (size_t i = 0; i < declaredWith; ++i) {
                portNames[with[i]] = std::string(names[i]);
            }
            for (size_t i = 0; i < norm.size(); ++i) {
                portNames[norm[i]] = std::string(names[declaredWith + i]);
            }
        }
        for (size_t i = declaredWith; i < with.size() && closure.empty(); ++i) {
            portNames[with[i]] =
                capturedName(g, camel::core::symbolText(captured[i - declaredWith]));
            info->capturePorts.insert(with[i]);
        }
        for (size_t i = 0; i < closure.size() && i < captured.size(); ++i) {
            portNames[closure[i]] = capturedName(g, camel::core::symbolText(captured[i]));
            info->capturePorts.insert(closure[i]);
        }
    }
    // A graph the compiler made itself (the arm of a branch) takes the values it uses from the
    // function around it as unnamed ports: they are named after what its caller passes there.
    if (auto inherited = inheritedPortNames_.find(g); inherited != inheritedPortNames_.end()) {
        const auto with = gImpl->withPorts();
        const auto norm = gImpl->normPorts();
        for (const auto &[position, name] : inherited->second) {
            const bool isNorm  = position >= kNormPortBase;
            const size_t index = isNorm ? position - kNormPortBase : position;
            const auto ports   = isNorm ? norm : with;
            if (index < ports.size()) {
                portNames.emplace(ports[index], name);
            }
        }
    }

    // The arm or loop region each region is in (the regions that only annotate are looked
    // through), asked of the graph's own tree: `core.controlRegions` is a fact of the graph, so
    // the answer here is the one the validator and the back ends read, not a second walk that
    // has to be kept in step with it.
    info->regions = std::make_shared<const camel::runtime::ControlRegions>(*gImpl);
    // The ports of the condition and of the body of a loop are named as the loop declares them;
    // a name of a port is there while the code runs in its region.
    std::unordered_map<gc_node_ref_t, uint32_t> portRegion;
    for (uint32_t r = 1; r < gImpl->regionCount(); ++r) {
        const GCRegion *region = gImpl->region(r);
        if (region->kind != GCRegionKind::LoopCond && region->kind != GCRegionKind::LoopBody) {
            continue;
        }
        const auto ports = gImpl->regionPorts(r);
        const auto names =
            region->params != nullptr ? region->params->argNames() : std::vector<std::string_view>{};
        for (size_t i = 0; i < ports.size(); ++i) {
            portRegion.emplace(ports[i], r);
            if (names.size() == ports.size()) {
                portNames[ports[i]] = std::string(names[i]);
            }
        }
    }

    std::unordered_map<int, size_t> functionVotes;
    if (gImpl->hasNodePayload()) {
        for (auto it = gImpl->nodes().begin(); it != gImpl->nodes().end(); ++it) {
            const GCNode *n = *it;
            if (n == nullptr) {
                continue;
            }
            NodeInfo node;
            node.ref      = it.ref();
            node.kind     = n->kind;
            node.slot     = n->dataIndex;
            node.origin   = gImpl->nodeOrigin(node.ref);
            node.loc      = loc(node.origin);
            node.function = functionOf(node.loc);
            node.executes = executesKind(n->kind);
            node.controlRegion = info->regions->controlOf(node.ref);
            if (auto port = portRegion.find(node.ref); port != portRegion.end()) {
                node.controlRegion = port->second;
            }
            switch (n->kind) {
            case GCNodeKind::Oper: {
                const auto *body = gImpl->nodeBodyAs<GCOperBody>(node.ref);
                node.label       = std::string(body->uri());
            } break;
            case GCNodeKind::Func: {
                auto *callee = programOfGraph->calleeOf(*gImpl, node.ref);
                node.label      = callee ? callee->name() : std::string("?");
                if (callee != nullptr && callee->name().rfind("__g_", 0) == 0) {
                    // What is passed to a compiler-made graph names its ports (see above).
                    const auto nameOf = [&](gc_node_ref_t input) -> std::string {
                        if (auto port = portNames.find(input); port != portNames.end()) {
                            return port->second;
                        }
                        const auto bound = bindingsOfNode(gImpl->nodeOrigin(input));
                        return bound.empty() ? std::string() : bound.front()->name;
                    };
                    auto &names = inheritedPortNames_[viewOf(callee)];
                    size_t i    = 0;
                    for (gc_node_ref_t input : gImpl->withInputsOf(node.ref)) {
                        if (std::string name = nameOf(input); !name.empty()) {
                            names.emplace(i, std::move(name));
                        }
                        ++i;
                    }
                    i = 0;
                    for (gc_node_ref_t input : gImpl->normInputsOf(node.ref)) {
                        if (std::string name = nameOf(input); !name.empty()) {
                            names.emplace(kNormPortBase + i, std::move(name));
                        }
                        ++i;
                    }
                }
            } break;
            case GCNodeKind::Accs: {
                const auto *body = gImpl->nodeBodyAs<GCAccsBody>(node.ref);
                node.label       = body->accsKind == GCAccsKind::TupleIndex
                                       ? "." + std::to_string(body->value)
                                       : "." + std::string(body->key());
            } break;
            case GCNodeKind::Port: {
                auto name  = portNames.find(node.ref);
                node.label = name != portNames.end() ? name->second : std::string("port");
            } break;
            case GCNodeKind::Data: {
                const auto *types = gImpl->staticDataType();
                ::Tuple *area     = gImpl->staticArea();
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
        const origin_t graphOrigin = sc->debugMap().graphOrigin(gImpl->id());
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

    // What the user calls the graph. A function is called by its full name. A graph the
    // compiler named itself (`__g_7__`) is a lambda or the arm of a branch: it is shown as
    // that, in the named function it was written in, with the line it starts on. Whether the
    // frame of an arm is shown at all is the stack's business (DebugCore, 5.4): it folds into
    // the frame of the function unless the user asks for the subgraphs.
    info->display = info->name;
    if (info->name.rfind("__g_", 0) == 0) {
        std::string owner;
        for (auto *outer = programOfGraph->outerOf(*gImpl); outer != nullptr;
             outer          = programOfGraph->outerOf(*outer)) {
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
        if (auto arm = armOwners_.find(g); arm != armOwners_.end()) {
            info->role   = GraphRole::Arm;
            info->owners = arm->second;
        } else {
            info->role = GraphRole::Lambda;
        }
        info->display = (owner.empty() ? std::string() : owner + " ") + "[" +
                        (info->folds() ? "arm" : "lambda") +
                        (line > 0 ? " at line " + std::to_string(line) : std::string()) + "]";
    }
    return *graphs_.emplace(g, std::move(info)).first->second;
}

} // namespace debugger::core

