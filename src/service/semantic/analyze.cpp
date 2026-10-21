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
 * Author: Zhenjie Wei
 * Created: Oct. 12, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * analyze(): one compilation with a recorder attached, and the join of what it reported to
 * the syntactic index of each file (see model.h).
 *
 * The join is by source range. A builder reports a fact at the origin of the tree node it
 * was working on; the syntactic index has the range of every declaration and reference. A
 * fact whose range matches nothing is dropped: the model then says less, never something
 * wrong.
 */

#include "camel/utils/windows_parser_guard.h"

#include "camel/service/semantic/model.h"

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/value_entity.h"
#include "camel/compile/semantic_recorder.h"
#include "camel/core/context/context.h"
#include "camel/core/error/abort.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/range.h"
#include "camel/core/module/userdef.h"
#include "camel/core/type.h"
#include "camel/core/type/enum.h"
#include "camel/parse/parse.h"
#include "camel/utils/install_layout.h"
#include "camel/utils/path.h"
#include "core/module/entity_internal.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>
#include <unordered_map>

namespace camel::service::semantic {

namespace fs = std::filesystem;
using camel::core::context::Context;
using camel::core::context::EntryConfig;
using camel::core::error::CharRange;
using camel::core::error::Diagnostic;
using camel::core::error::Diagnostics;
using camel::core::error::DiagsConfig;
using camel::core::error::Severity;
using camel::core::module::UserDefinedModule;
using camel::core::type::FunctionType;
using camel::core::type::Type;
using camel::core::type::TypeCode;
using camel::source::origin_id_t;

namespace {

struct RawFact {
    enum class Kind : uint8_t {
        TypeExpression,
        TypeDeclared,
        ValueDeclared,
        ExpressionTyped,
        ValueReferenced,
        FunctionReferenced,
        GenericReferenced,
        OperatorReferenced,
        CallResolved,
        FunctionBuilt,
    };
    Kind kind;
    origin_id_t at    = 0;
    origin_id_t other = 0;
    Type *type        = nullptr;
    bool flag         = false;
    std::string name;
    camel::runtime::GraphId graph = camel::runtime::kInvalidGraphId;
    std::string uri;
    std::vector<std::string> list;
    std::vector<camel::runtime::GraphId> graphs;
};

// Stores what the builders report, in the order reported. Nothing is interpreted while the
// compilation runs.
class Recorder : public camel::compile::SemanticRecorder {
  public:
    std::vector<RawFact> facts;

    void typeExpression(origin_id_t expr, Type *type) override {
        facts.push_back({.kind = RawFact::Kind::TypeExpression, .at = expr, .type = type});
    }
    void
    valueDeclared(origin_id_t decl, const std::string &name, Type *type, bool variable) override {
        facts.push_back(
            {.kind = RawFact::Kind::ValueDeclared,
             .at   = decl,
             .type = type,
             .flag = variable,
             .name = name});
    }
    void expressionTyped(origin_id_t expr, Type *type) override {
        facts.push_back({.kind = RawFact::Kind::ExpressionTyped, .at = expr, .type = type});
    }
    void typeDeclared(origin_id_t decl, const std::string &name, Type *type) override {
        facts.push_back(
            {.kind = RawFact::Kind::TypeDeclared, .at = decl, .type = type, .name = name});
    }
    void valueReferenced(
        origin_id_t ref, const std::string &name, origin_id_t decl, Type *type) override {
        facts.push_back(
            {.kind  = RawFact::Kind::ValueReferenced,
             .at    = ref,
             .other = decl,
             .type  = type,
             .name  = name});
    }
    void functionReferenced(
        origin_id_t ref, const std::string &name,
        std::vector<camel::runtime::GraphId> graphs) override {
        facts.push_back(
            {.kind   = RawFact::Kind::FunctionReferenced,
             .at     = ref,
             .name   = name,
             .graphs = std::move(graphs)});
    }
    void genericReferenced(origin_id_t ref, const std::string &name, origin_id_t decl) override {
        facts.push_back(
            {.kind = RawFact::Kind::GenericReferenced, .at = ref, .other = decl, .name = name});
    }
    void operatorReferenced(
        origin_id_t ref, const std::string &name, std::vector<std::string> signatures) override {
        facts.push_back(
            {.kind = RawFact::Kind::OperatorReferenced,
             .at   = ref,
             .name = name,
             .list = std::move(signatures)});
    }
    void callResolved(
        origin_id_t call, origin_id_t callee, camel::runtime::GraphId graph,
        const std::string &operatorUri, FunctionType *type) override {
        facts.push_back(
            {.kind  = RawFact::Kind::CallResolved,
             .at    = callee,
             .other = call,
             .type  = type,
             .graph = graph,
             .uri   = operatorUri});
    }
    void functionBuilt(
        origin_id_t func, const std::string &name, camel::runtime::GraphId graph,
        FunctionType *type) override {
        facts.push_back(
            {.kind  = RawFact::Kind::FunctionBuilt,
             .at    = func,
             .type  = type,
             .name  = name,
             .graph = graph});
    }
};

std::string normalPath(const std::string &path) {
    if (path.empty()) {
        return path;
    }
    std::error_code ec;
    fs::path p = fs::absolute(camel::utils::pathFromUtf8(path), ec);
    if (ec) {
        p = camel::utils::pathFromUtf8(path);
    }
    return camel::utils::genericPathToUtf8(p.lexically_normal());
}

// Types print themselves in the spelling of source text; a signature written by hand as text
// (an operator's) goes through the same mapping (camel::core::type::sourceTypeSpelling).
std::string sourceSpelling(const std::string &text) {
    return camel::core::type::sourceTypeSpelling(text);
}

} // namespace

class ModelBuilder {
  public:
    explicit ModelBuilder(const AnalysisOptions &options) : options_(options) {}

    model_ptr_t build() {
        model_ = std::make_shared<SemanticModel>();
        try {
            compile();
        } catch (const std::exception &e) {
            internalError(e.what());
        } catch (...) {
            internalError("unknown exception");
        }
        if (model_->files_.empty()) {
            // Nothing could be set up: the model still has the text and its tokens.
            addFile(normalPath(options_.path), options_.text, nullptr, false, 0);
        }
        if (options_.previous) {
            inherit(*options_.previous);
        }
        return model_;
    }

  private:
    const AnalysisOptions &options_;
    std::shared_ptr<SemanticModel> model_;
    camel::source::SourceContext *sc_ = nullptr;
    std::unordered_map<uint32_t, uint32_t> fileOfSource_; // SourceFileId -> file index
    std::unordered_map<Type *, uint32_t> typeIds_;
    std::unordered_map<std::string, uint32_t> typeByText_;

    void internalError(const std::string &what) {
        DiagnosticInfo info;
        info.severity = 1;
        info.code     = "internal";
        info.name     = "InternalCompilerError";
        info.message  = "internal compiler error: " + what;
        model_->diagnostics_.push_back(std::move(info));
    }

    uint32_t addFile(
        const std::string &path, const std::string &text, const camel::parse::syntax::Tree *tree,
        bool parsed, uint32_t sourceFileId) {
        const uint32_t index = static_cast<uint32_t>(model_->files_.size());
        model_->files_.emplace_back();
        FileModel &file = model_->files_.back();
        file.path       = path;
        file.text       = text;
        file.parsed     = parsed;
        file.syntax     = buildSyntaxIndex(tree, parsed);
        file.declFacts.resize(file.syntax.decls.size());
        file.refFacts.resize(file.syntax.refs.size());
        for (const Decl &decl : file.syntax.decls) {
            file.declByNameStart.emplace(decl.nameRange.start, decl.id);
        }
        if (sourceFileId != 0) {
            fileOfSource_[sourceFileId] = index;
        }
        return index;
    }

    // The texts of the files are final only once every file is added (the vector moves).
    void indexLines() {
        for (FileModel &file : model_->files_) {
            file.lines = LineIndex(file.text);
        }
    }

    // ---- types ----

    uint32_t typeId(Type *type) {
        if (type == nullptr) {
            return internText("?");
        }
        if (auto it = typeIds_.find(type); it != typeIds_.end()) {
            return it->second;
        }
        TypeInfo info;
        try {
            info.text = sourceSpelling(type->toString());
        } catch (...) {
            info.text = "?";
        }
        const uint32_t id = static_cast<uint32_t>(model_->types_.size());
        model_->types_.push_back(info);
        typeIds_[type] = id;
        // Members after the id is fixed: a type may contain itself through a function type.
        std::vector<std::pair<std::string, uint32_t>> fields;
        try {
            if (type->code() == TypeCode::Struct) {
                auto *st = static_cast<camel::core::type::StructType *>(type);
                for (size_t i = 0; i < st->size(); ++i) {
                    fields.emplace_back(std::string(st->fieldName(i)), typeId(st->typeAt(i)));
                }
            } else if (type->code() == TypeCode::Tuple) {
                auto *tt = static_cast<camel::core::type::TupleType *>(type);
                if (!tt->isCell()) {
                    for (size_t i = 0; i < tt->size(); ++i) {
                        fields.emplace_back(std::to_string(i), typeId(tt->typeAt(i)));
                    }
                }
            }
        } catch (...) {
            fields.clear();
        }
        model_->types_[id].fields = std::move(fields);
        // The variants of an enum: what a `Shape::` completion offers, and no field can are
        // also the fields of a struct. `EnumDecl::variants()` is what was written, so the
        // detail of `Option<T>`'s `Some` reads `Some(T)`.
        std::vector<std::pair<std::string, std::string>> variants;
        try {
            if (type->code() == camel::core::type::EnumType::typeCode()) {
                auto *et = static_cast<camel::core::type::EnumType *>(type);
                for (const auto &variant : et->decl()->variants()) {
                    variants.emplace_back(variant.name, variant.signature());
                }
            }
        } catch (...) {
            variants.clear();
        }
        model_->types_[id].variants = std::move(variants);
        return id;
    }
    uint32_t internText(const std::string &text) {
        if (auto it = typeByText_.find(text); it != typeByText_.end()) {
            return it->second;
        }
        const uint32_t id = static_cast<uint32_t>(model_->types_.size());
        model_->types_.push_back(TypeInfo{text, {}});
        typeByText_[text] = id;
        return id;
    }
    static void addUnique(std::vector<uint32_t> &list, uint32_t id) {
        if (std::find(list.begin(), list.end(), id) == list.end()) {
            list.push_back(id);
        }
    }
    static void addUnique(std::vector<Location> &list, Location loc) {
        for (const Location &have : list) {
            if (have.file == loc.file && have.range == loc.range) {
                return;
            }
        }
        list.push_back(loc);
    }

    // ---- ranges ----

    std::optional<Location> locate(origin_id_t origin) const {
        if (sc_ == nullptr || origin == camel::source::kInvalidOriginId) {
            return std::nullopt;
        }
        const auto *record = sc_->origin(origin);
        if (record == nullptr) {
            return std::nullopt;
        }
        const auto *span = sc_->span(record->primarySpan);
        if (span == nullptr || !span->valid()) {
            return std::nullopt;
        }
        auto it = fileOfSource_.find(span->fileId);
        if (it == fileOfSource_.end()) {
            return std::nullopt;
        }
        return Location{
            it->second,
            Range{
                static_cast<uint32_t>(span->startOffset),
                static_cast<uint32_t>(span->endOffset)}};
    }

    // The declarations of each file by the range of the construct that declares them: the
    // key the builders' reports are joined on.
    std::vector<std::unordered_multimap<uint64_t, uint32_t>> declsByOwner_;
    static uint64_t rangeKey(Range r) { return (static_cast<uint64_t>(r.start) << 32) | r.end; }
    const std::unordered_multimap<uint64_t, uint32_t> &declsByOwner(uint32_t file) {
        if (declsByOwner_.size() < model_->files_.size()) {
            declsByOwner_.resize(model_->files_.size());
            for (size_t f = 0; f < model_->files_.size(); ++f) {
                for (const Decl &decl : model_->files_[f].syntax.decls) {
                    declsByOwner_[f].emplace(rangeKey(decl.ownerRange), decl.id);
                }
            }
        }
        return declsByOwner_[file];
    }

    // The declaration of `name` that the compiler reported at `where`: the one whose
    // construct has that range, else the first one of that name inside the range.
    std::optional<uint32_t> findDecl(const Location &where, const std::string &name) {
        const SyntaxIndex &syntax = model_->files_[where.file].syntax;
        const auto [first, last]  = declsByOwner(where.file).equal_range(rangeKey(where.range));
        std::optional<uint32_t> exact;
        for (auto it = first; it != last; ++it) {
            if (syntax.decls[it->second].name == name && (!exact || it->second < *exact)) {
                exact = it->second;
            }
        }
        if (exact) {
            return exact;
        }
        for (const Decl &decl : syntax.decls) {
            if (decl.name == name && where.range.contains(decl.nameRange)) {
                return decl.id;
            }
        }
        return std::nullopt;
    }
    // The function declared by the construct at `where`.
    std::optional<uint32_t> findFunction(const Location &where) {
        const SyntaxIndex &syntax = model_->files_[where.file].syntax;
        const auto [first, last]  = declsByOwner(where.file).equal_range(rangeKey(where.range));
        for (auto it = first; it != last; ++it) {
            if (syntax.decls[it->second].kind == DeclKind::Function) {
                return it->second;
            }
        }
        return std::nullopt;
    }
    // The reference whose path has the range `where`.
    std::optional<size_t> findRef(const Location &where) const {
        const auto &refs = model_->files_[where.file].syntax.refs;
        auto it          = std::lower_bound(
            refs.begin(),
            refs.end(),
            where.range.start,
            [](const Ref &ref, uint32_t start) { return ref.range.start < start; });
        for (; it != refs.end() && it->range.start == where.range.start; ++it) {
            if (it->range.end == where.range.end) {
                return static_cast<size_t>(it - refs.begin());
            }
        }
        return std::nullopt;
    }
    std::optional<Location> functionOfGraph(camel::runtime::GraphId graph) {
        if (!camel::runtime::isValid(graph) || sc_ == nullptr) {
            return std::nullopt;
        }
        auto where = locate(sc_->debugMap().graphOrigin(graph));
        if (!where) {
            return std::nullopt;
        }
        if (auto decl = findFunction(*where)) {
            return Location{where->file, model_->files_[where->file].syntax.decls[*decl].nameRange};
        }
        return std::nullopt;
    }
    // A declaration as a location: the range of its name.
    Location declLocation(uint32_t file, uint32_t decl) const {
        return Location{file, model_->files_[file].syntax.decls[decl].nameRange};
    }

    // ---- compilation ----

    void compile() {
        const std::string path = normalPath(options_.path);
        if (!options_.compile) {
            parseOnly(path);
            indexLines();
            return;
        }
        auto diagnostics = std::make_shared<Diagnostics>("lsp", path);
        diagnostics->setConfig(DiagsConfig{.total_limit = -1});

        // The context of `camel check`: every module collects all of its errors; nothing runs.
        const std::string entryDir =
            camel::utils::pathToUtf8(camel::utils::pathFromUtf8(path).parent_path());
        auto searchPaths           = camel::utils::buildModuleSearchPaths(
            entryDir,
            camel::utils::ModuleSearchPathOptions{.stdlibOverride = ""});
        auto ctx = Context::create(
            EntryConfig{
                .entryDir       = entryDir,
                .entryFile      = path,
                .searchPaths    = std::move(searchPaths),
                .requireEntry   = options_.requireEntry,
                .sourceOverride = [this](const std::string &file) -> std::optional<std::string> {
                    const auto it = options_.sourceOverrides.find(normalPath(file));
                    return it == options_.sourceOverrides.end()
                               ? std::nullopt
                               : std::optional<std::string>(it->second);
                },
            },
            DiagsConfig{.total_limit = -1});
        ctx->runtimeDiagSink()->setConfig(
            DiagsConfig{
                .total_limit         = -1,
                .per_severity_limits = {{Severity::Error, 0}},
            });
        Recorder recorder;
        if (options_.record) {
            ctx->setSemanticRecorder(&recorder);
        }
        sc_ = ctx->sourceContext().get();

        std::istringstream input(options_.text);
        auto parser     = std::make_shared<camel::parse::CamelParser>(diagnostics);
        auto mainModule = std::make_shared<UserDefinedModule>("main", path, ctx, parser);
        ctx->setMainModule(mainModule);
        bool parsed   = false;
        bool compiled = false;
        std::string failure;
        try {
            parsed = parser->parse(input);
        } catch (const Diagnostic &d) {
            diagnostics->add(Diagnostic(d));
        } catch (const std::exception &e) {
            failure = e.what();
        } catch (...) {
            failure = "unknown exception while parsing";
        }
        if (parsed && failure.empty()) {
            try {
                compiled = mainModule->compile(camel::core::module::CompileStage::Done);
            } catch (const Diagnostic &d) {
                diagnostics->add(Diagnostic(d));
            } catch (const camel::core::error::BuildAbortException &) {
                // Reported where it was raised.
            } catch (const std::exception &e) {
                failure = e.what();
            } catch (...) {
                failure = "unknown exception while compiling";
            }
        }
        ctx->setSemanticRecorder(nullptr);

        // Files: the entry first, then every module compiled from source.
        addFile(path, options_.text, parser->tree(), parser->parsed(), parser->sourceFileId());
        std::vector<std::shared_ptr<UserDefinedModule>> modules;
        for (const auto &mod : ctx->allUserModules()) {
            auto ud = std::dynamic_pointer_cast<UserDefinedModule>(mod);
            if (!ud || ud == mainModule || !ud->parser()) {
                continue;
            }
            const auto fileId  = ud->parser()->sourceFileId();
            const auto *source = sc_->sourceFile(fileId);
            if (source == nullptr) {
                continue;
            }
            const uint32_t index = addFile(
                normalPath(ud->path()),
                source->content,
                ud->parser()->tree(),
                ud->parser()->parsed(),
                fileId);
            model_->files_[index].compiled = ud->diagnostics() && !ud->diagnostics()->hasErrors();
            modules.push_back(ud);
        }
        indexLines();
        model_->files_[0].compiled = compiled && !diagnostics->hasErrors();

        join(recorder.facts);
        try {
            catalog(*mainModule, ctx.get());
        } catch (...) {
            // The catalog is a convenience (completion, hover of built-in names).
        }

        // Diagnostics: the parser's and the entry module's share one container.
        collectDiagnostics(*diagnostics, parser->tokenPositions(), 0);
        for (size_t i = 0; i < modules.size(); ++i) {
            if (modules[i]->diagnostics()) {
                collectDiagnostics(
                    *modules[i]->diagnostics(),
                    modules[i]->parser()->tokenPositions(),
                    static_cast<uint32_t>(i + 1));
            }
        }
        if (!failure.empty()) {
            internalError(failure);
        }
        sc_ = nullptr;
    }

    void parseOnly(const std::string &path) {
        auto diagnostics = std::make_shared<Diagnostics>("lsp", path);
        diagnostics->setConfig(DiagsConfig{.total_limit = -1});
        auto parser = std::make_shared<camel::parse::CamelParser>(diagnostics);
        std::istringstream input(options_.text);
        try {
            parser->parseCST(input);
        } catch (...) {
        }
        addFile(path, options_.text, parser->tree(), parser->parsed(), 0);
    }

    // ---- diagnostics ----

    uint32_t fileOfPath(const std::string &path, uint32_t fallback) const {
        if (path.empty()) {
            return fallback;
        }
        const std::string normal = normalPath(path);
        for (size_t i = 0; i < model_->files_.size(); ++i) {
            if (model_->files_[i].path == normal) {
                return static_cast<uint32_t>(i);
            }
        }
        return fallback;
    }

    Range rangeIn(uint32_t file, const camel::core::error::SourceRange &range) const {
        if (!std::holds_alternative<CharRange>(range)) {
            return {};
        }
        const CharRange r      = std::get<CharRange>(range);
        const LineIndex &lines = model_->files_[file].lines;
        return Range{
            lines.offsetOfCodePoint(
                static_cast<uint32_t>(r.start.line),
                static_cast<uint32_t>(r.start.character)),
            lines.offsetOfCodePoint(
                static_cast<uint32_t>(r.end.line),
                static_cast<uint32_t>(r.end.character))};
    }

    void collectDiagnostics(
        Diagnostics &diags, const camel::core::error::TokenPositions &tokens, uint32_t file) {
        diags.fetchAll(tokens);
        std::set<std::string> seen;
        const auto add = [&](const Diagnostic &d) {
            DiagnosticInfo info;
            info.file  = fileOfPath(d.modulePath, file);
            info.range = rangeIn(info.file, d.range);
            switch (d.severity) {
            case Severity::Warn:
                info.severity = 2;
                break;
            case Severity::Info:
                info.severity = 3;
                break;
            case Severity::Hint:
                info.severity = 4;
                break;
            default:
                info.severity = 1;
            }
            info.code       = "0x" + Diagnostic::hex8(d.diagCode());
            info.name       = d.name;
            info.message    = d.message;
            info.suggestion = d.suggestion;
            info.notes      = d.notes;
            for (const auto &label : d.related) {
                DiagnosticInfo::Related related;
                related.file    = fileOfPath(label.path, info.file);
                related.range   = rangeIn(related.file, label.range);
                related.message = label.message;
                info.related.push_back(std::move(related));
            }
            // The parser's diagnostics and the module's may be the same objects twice.
            const std::string key =
                std::to_string(info.file) + ":" + std::to_string(info.range.start) + "-" +
                std::to_string(info.range.end) + "|" + info.code + "|" + info.message;
            if (seen.insert(key).second) {
                model_->diagnostics_.push_back(std::move(info));
            }
        };
        for (const Diagnostic *d : diags.errors()) {
            add(*d);
        }
        for (const Diagnostic *d : diags.warnings()) {
            add(*d);
        }
        for (const Diagnostic *d : diags.infos()) {
            add(*d);
        }
        for (const Diagnostic *d : diags.hints()) {
            add(*d);
        }
    }

    // ---- the join ----

    void join(const std::vector<RawFact> &facts) {
        // Refs are in source order, but nested paths may share a start: sort a copy of the
        // index by start once so that findRef can search.
        for (const RawFact &fact : facts) {
            auto where = locate(fact.at);
            if (!where) {
                continue;
            }
            FileModel &file = model_->files_[where->file];
            switch (fact.kind) {
            case RawFact::Kind::TypeExpression:
                if (fact.type != nullptr) {
                    file.typeExprs.emplace_back(where->range, typeId(fact.type));
                }
                break;
            case RawFact::Kind::ExpressionTyped:
                if (fact.type != nullptr) {
                    file.exprTypes.emplace_back(where->range, typeId(fact.type));
                }
                break;
            case RawFact::Kind::ValueDeclared:
                if (auto decl = findDecl(*where, fact.name); decl && fact.type != nullptr) {
                    addUnique(file.declFacts[*decl].types, typeId(fact.type));
                }
                break;
            case RawFact::Kind::TypeDeclared:
                if (auto decl = findDecl(*where, fact.name); decl && fact.type != nullptr) {
                    addUnique(file.declFacts[*decl].types, typeId(fact.type));
                }
                break;
            case RawFact::Kind::ValueReferenced:
                if (auto ref = findRef(*where)) {
                    RefFacts &rf = file.refFacts[*ref];
                    rf.resolved  = true;
                    if (fact.type != nullptr) {
                        addUnique(rf.types, typeId(fact.type));
                    }
                    if (auto declAt = locate(fact.other)) {
                        if (auto decl = findDecl(*declAt, fact.name)) {
                            addUnique(rf.targets, declLocation(declAt->file, *decl));
                        }
                    }
                }
                break;
            case RawFact::Kind::FunctionReferenced:
                if (auto ref = findRef(*where)) {
                    RefFacts &rf = file.refFacts[*ref];
                    rf.resolved  = true;
                    for (const camel::runtime::GraphId graph : fact.graphs) {
                        if (auto target = functionOfGraph(graph)) {
                            addUnique(rf.targets, *target);
                        }
                    }
                }
                break;
            case RawFact::Kind::GenericReferenced:
                if (auto ref = findRef(*where)) {
                    RefFacts &rf = file.refFacts[*ref];
                    rf.resolved  = true;
                    if (auto declAt = locate(fact.other)) {
                        if (auto decl = findFunction(*declAt)) {
                            addUnique(rf.targets, declLocation(declAt->file, *decl));
                        }
                    }
                }
                break;
            case RawFact::Kind::OperatorReferenced:
                if (auto ref = findRef(*where)) {
                    RefFacts &rf          = file.refFacts[*ref];
                    rf.resolved           = true;
                    rf.isOperator         = true;
                    rf.operatorSignatures = fact.list;
                    for (std::string &signature : rf.operatorSignatures) {
                        signature = sourceSpelling(signature);
                    }
                }
                break;
            case RawFact::Kind::CallResolved: {
                if (fact.type == nullptr) {
                    break;
                }
                const uint32_t type = typeId(fact.type);
                bool macro          = false;
                try {
                    macro = static_cast<FunctionType *>(fact.type)->modifiers().macro();
                } catch (...) {
                }
                if (auto ref = findRef(*where)) {
                    RefFacts &rf = file.refFacts[*ref];
                    addUnique(rf.callTypes, type);
                    rf.macroCall = rf.macroCall || macro;
                    if (auto target = functionOfGraph(fact.graph)) {
                        addUnique(rf.callTargets, *target);
                    }
                } else if (!fact.uri.empty()) {
                    file.operatorCalls.emplace_back(where->range, type);
                } else if (auto target = functionOfGraph(fact.graph)) {
                    // An operator that took an overload of the program: its call has no
                    // name, the operator stands for the declaration.
                    file.operatorCalls.emplace_back(where->range, type);
                    file.operatorTargets.emplace_back(where->range, *target);
                }
                break;
            }
            case RawFact::Kind::FunctionBuilt:
                if (auto decl = findFunction(*where); decl && fact.type != nullptr) {
                    addUnique(file.declFacts[*decl].signatures, typeId(fact.type));
                } else if (fact.type != nullptr) {
                    // A function literal: the type of the expression.
                    file.exprTypes.emplace_back(where->range, typeId(fact.type));
                }
                break;
            }
        }
        for (FileModel &file : model_->files_) {
            const auto byRange = [](const auto &a, const auto &b) {
                return a.first.start != b.first.start ? a.first.start < b.first.start
                                                      : a.first.end < b.first.end;
            };
            std::stable_sort(file.exprTypes.begin(), file.exprTypes.end(), byRange);
            std::stable_sort(file.typeExprs.begin(), file.typeExprs.end(), byRange);
            std::stable_sort(file.operatorCalls.begin(), file.operatorCalls.end(), byRange);
        }
    }

    // ---- what the entry module can name without declaring it ----

    ExternalName describe(const std::string &name, const camel::core::module::entity &entity) {
        using camel::core::module::detail::EntityAccess;
        ExternalName ext;
        ext.name = name;
        ext.kind = ExternalName::Kind::Function;
        if (entity.isOperGroup()) {
            for (const auto &[uri, resolver] : entity.operGroup()->resolvers()) {
                const std::string signature = sourceSpelling(resolver->signature());
                if (std::find(ext.signatures.begin(), ext.signatures.end(), signature) ==
                    ext.signatures.end()) {
                    ext.signatures.push_back(signature);
                }
                if (ext.module.empty()) {
                    // ":mod/name" names the module the operator lives in.
                    const size_t slash = uri.find('/');
                    if (!uri.empty() && uri.front() == ':' && slash != std::string::npos) {
                        ext.module = uri.substr(1, slash - 1);
                    }
                }
            }
        } else if (EntityAccess::isGraphSet(entity)) {
            const auto &graphs = EntityAccess::graphSet(entity);
            if (graphs) {
                for (const auto &graph : *graphs) {
                    if (!graph) {
                        continue;
                    }
                    if (graph->funcType()) {
                        ext.signatures.push_back(sourceSpelling(graph->funcType()->toString()));
                        ext.macro = ext.macro || graph->funcType()->modifiers().macro();
                    }
                    if (auto where = functionOfGraph(graph->graphId())) {
                        addUnique(ext.definitions, *where);
                    }
                }
            }
        } else if (EntityAccess::isDecoratedGraph(entity)) {
            const auto &graph = EntityAccess::decoratedGraph(entity);
            if (graph && graph->funcType()) {
                ext.signatures.push_back(sourceSpelling(graph->funcType()->toString()));
            }
        } else if (entity.isValue()) {
            // A constant a C++ module exports (`math::pi`; design principles 10.1).
            ext.kind = ExternalName::Kind::Value;
            if (const auto &value = entity.value(); value && value->type() != nullptr) {
                ext.signatures.push_back(sourceSpelling(value->type()->toString()));
            }
        } else if (EntityAccess::isNode(entity)) {
            // An exported value (design principles 10.1): a node of the program's root graph,
            // there once a module has used the value.
            ext.kind = ExternalName::Kind::Value;
            auto *value =
                static_cast<camel::compile::gir::ExportedValue *>(EntityAccess::node(entity));
            const auto node = value != nullptr ? value->node : camel::runtime::DraftNodeRef{};
            const auto *header = node ? node.draft->header(node.id) : nullptr;
            if (header != nullptr && header->dataType != nullptr) {
                ext.signatures.push_back(sourceSpelling(header->dataType->toString()));
            }
        }
        // One function may be bound to a name along several import paths.
        std::vector<std::string> unique;
        for (std::string &signature : ext.signatures) {
            if (std::find(unique.begin(), unique.end(), signature) == unique.end()) {
                unique.push_back(std::move(signature));
            }
        }
        ext.signatures = std::move(unique);
        return ext;
    }

    void catalog(UserDefinedModule &module, Context *context) {
        FileModel &main = model_->files_[0];
        // Names imported into the module's own name space; those written in an import
        // declaration of the file are attached to that declaration.
        for (const Reference &ref : module.importedRefs()) {
            const std::string name = ref.toString();
            std::optional<ExternalName> ext;
            if (context != nullptr) {
                // What the name means is the program's: its exported entities are the graphs
                // this compilation made (one table per program, task H42).
                if (auto entity = context->programExports().importedEntity(module, ref)) {
                    ext = describe(name, *entity);
                }
            }
            bool isType = false;
            if (auto type = module.getImportedType(ref); type.has_value() && *type != nullptr) {
                isType = true;
                if (!ext) {
                    ext       = ExternalName{};
                    ext->name = name;
                    ext->kind = ExternalName::Kind::Type;
                    ext->signatures.push_back(sourceSpelling((*type)->toString()));
                }
            }
            if (!ext) {
                continue;
            }
            bool declared = false;
            for (const Decl &decl : main.syntax.decls) {
                if (decl.kind == DeclKind::Import && decl.name == name) {
                    DeclFacts &facts         = main.declFacts[decl.id];
                    facts.importedFunction   = ext->kind == ExternalName::Kind::Function;
                    facts.importedType       = isType;
                    facts.importedMacro      = ext->macro;
                    facts.importedSignatures = ext->signatures;
                    facts.imported           = ext->definitions;
                    if (isType) {
                        if (auto type = module.getImportedType(ref);
                            type.has_value() && *type != nullptr) {
                            if (auto *enumType = camel::core::type::asEnumTypeOrPrototype(*type)) {
                                for (const auto &variant : enumType->decl()->variants()) {
                                    facts.importedVariants.emplace_back(
                                        variant.name,
                                        variant.signature());
                                }
                            }
                        }
                    }
                    declared = true;
                }
            }
            if (!declared) {
                model_->externals_.push_back(std::move(*ext));
            }
        }
        // The exports of the modules imported as namespaces.
        std::vector<std::pair<std::string, std::shared_ptr<camel::core::module::Module>>> spaces =
            module.importedNamespaces();
        if (!main.parsed && context != nullptr) {
            // The imports of a file that does not parse were not processed. Those that can
            // still be read from it are looked up here, so that a text being typed keeps
            // the members of the modules it names.
            for (const Decl &decl : main.syntax.decls) {
                if (decl.kind != DeclKind::Module || decl.importedFrom.empty()) {
                    continue;
                }
                try {
                    if (auto mod = context->importModule(decl.importedFrom, module.name())) {
                        spaces.emplace_back(decl.name, mod);
                    }
                } catch (...) {
                    // An import that fails is the compiler's to report once the file parses.
                }
            }
        }
        for (const auto &[prefix, mod] : spaces) {
            if (!mod || !mod->loaded()) {
                continue;
            }
            model_->namespaces_[prefix];
            const auto visitEntity = [&](const Reference &ref,
                                         const camel::core::module::entity &entity) {
                std::string space = prefix;
                for (const std::string &part : ref.paths()) {
                    // A nested namespace is a member of the one around it.
                    auto &outer = model_->namespaces_[space];
                    if (std::none_of(outer.begin(), outer.end(), [&](const ExternalName &e) {
                            return e.name == part && e.kind == ExternalName::Kind::Namespace;
                        })) {
                        ExternalName inner;
                        inner.name = part;
                        inner.kind = ExternalName::Kind::Namespace;
                        outer.push_back(std::move(inner));
                    }
                    space += "::" + part;
                }
                ExternalName ext = describe(ref.ident(), entity);
                ext.module       = mod->name();
                model_->namespaces_[space].push_back(std::move(ext));
            };
            if (context != nullptr) {
                // The program's entities for the module, then the module's own (native) names:
                // what a name means is the program's (task H42).
                context->programExports().forEachExportedEntity(*mod, visitEntity);
            } else {
                mod->nativeEntityNS()->forEach(visitEntity);
            }
            if (auto ns = mod->exportedTypeNS()) {
                std::as_const(*ns).forEach([&](const Reference &ref, Type *const &type) {
                    if (type == nullptr) {
                        return;
                    }
                    std::string space = prefix;
                    for (const std::string &part : ref.paths()) {
                        space += "::" + part;
                    }
                    ExternalName ext;
                    ext.name   = ref.ident();
                    ext.kind   = ExternalName::Kind::Type;
                    ext.module = mod->name();
                    ext.signatures.push_back(sourceSpelling(type->toString()));
                    model_->namespaces_[space].push_back(std::move(ext));
                });
            }
        }
        const auto byName = [](const ExternalName &a, const ExternalName &b) {
            return a.name < b.name;
        };
        std::sort(model_->externals_.begin(), model_->externals_.end(), byName);
        for (auto &[prefix, members] : model_->namespaces_) {
            std::sort(members.begin(), members.end(), byName);
        }
    }

    // ---- facts of an earlier model for declarations the compiler did not reach ----

    // A declaration's place: the names of the functions around it, its name and its rank
    // among the declarations of that name there.
    static std::vector<std::string> placeKeys(const SyntaxIndex &syntax) {
        std::vector<std::string> keys(syntax.decls.size());
        std::unordered_map<std::string, int> counts;
        for (const Decl &decl : syntax.decls) {
            std::string key;
            for (int32_t c = decl.container; c >= 0; c = syntax.decls[c].container) {
                key = syntax.decls[c].name + "/" + key;
            }
            key += decl.name;
            key += "#" + std::to_string(counts[key]++);
            keys[decl.id] = std::move(key);
        }
        return keys;
    }

    void inherit(const SemanticModel &previous) {
        if (previous.files_.empty() || model_->files_.empty()) {
            return;
        }
        FileModel &file       = model_->files_[0];
        const FileModel &old  = previous.files_[0];
        const bool lacksFacts = !file.compiled;
        if (lacksFacts) {
            const auto keys    = placeKeys(file.syntax);
            const auto oldKeys = placeKeys(old.syntax);
            std::unordered_map<std::string, uint32_t> oldByKey;
            for (size_t i = 0; i < oldKeys.size(); ++i) {
                oldByKey.emplace(oldKeys[i], static_cast<uint32_t>(i));
            }
            for (const Decl &decl : file.syntax.decls) {
                DeclFacts &facts = file.declFacts[decl.id];
                auto it          = oldByKey.find(keys[decl.id]);
                if (it == oldByKey.end() || old.syntax.decls[it->second].kind != decl.kind) {
                    continue;
                }
                const DeclFacts &oldFacts = old.declFacts[it->second];
                const auto copyTypes      = [&](const std::vector<uint32_t> &from,
                                                std::vector<uint32_t> &to) {
                    if (!to.empty()) {
                        return;
                    }
                    for (uint32_t id : from) {
                        to.push_back(copyType(previous, id));
                    }
                };
                copyTypes(oldFacts.types, facts.types);
                copyTypes(oldFacts.signatures, facts.signatures);
                if (facts.importedSignatures.empty()) {
                    facts.importedSignatures = oldFacts.importedSignatures;
                    facts.importedFunction   = oldFacts.importedFunction;
                    facts.importedType       = oldFacts.importedType;
                    facts.importedMacro      = oldFacts.importedMacro;
                }
                if (facts.importedVariants.empty()) {
                    facts.importedVariants = oldFacts.importedVariants;
                }
            }
        }
        // A file that does not parse has no imports processed: keep what was importable.
        if (model_->namespaces_.empty() && !file.parsed) {
            model_->namespaces_ = previous.namespaces_;
        }
        if (model_->externals_.empty() || (!file.parsed && !previous.externals_.empty())) {
            model_->externals_ = previous.externals_;
            for (ExternalName &ext : model_->externals_) {
                ext.definitions.clear(); // locations of another model
            }
        }
    }

    uint32_t copyType(const SemanticModel &from, uint32_t id) {
        const TypeInfo &info  = from.types_[id];
        const std::string key = "@" + info.text;
        if (auto it = typeByText_.find(key); it != typeByText_.end()) {
            return it->second;
        }
        const uint32_t mine = static_cast<uint32_t>(model_->types_.size());
        model_->types_.push_back(TypeInfo{info.text, {}});
        typeByText_[key] = mine;
        std::vector<std::pair<std::string, uint32_t>> fields;
        for (const auto &[name, field] : info.fields) {
            fields.emplace_back(name, copyType(from, field));
        }
        model_->types_[mine].fields   = std::move(fields);
        model_->types_[mine].variants = info.variants;
        return mine;
    }
};

model_ptr_t analyze(const AnalysisOptions &options) { return ModelBuilder(options).build(); }

std::string explainDiagnostic(const std::string &code) {
    using namespace camel::core::error;
    if (code.size() < 3 || code.compare(0, 2, "0x") != 0) {
        return {};
    }
    uint32_t value = 0;
    try {
        value = static_cast<uint32_t>(std::stoul(code.substr(2), nullptr, 16));
    } catch (...) {
        return {};
    }
    for (const DiagCatalogEntry &entry : diagnosticCatalog()) {
        if (entry.code != value) {
            continue;
        }
        // The templates of every type but RuntimeDiag are std::format strings, in which a
        // literal brace is written twice.
        const auto literal = [&](std::string text) {
            if (entry.type == DiagType::RuntimeDiag) {
                return text;
            }
            for (const char *doubled : {"{{", "}}"}) {
                for (size_t at = text.find(doubled); at != std::string::npos;
                     at        = text.find(doubled, at + 1)) {
                    text.erase(at, 1);
                }
            }
            return text;
        };
        std::string out = "**" + to_string(entry.type) + "::" + entry.name + "** (`" + code +
                          "`, " + to_string(entry.severity) + ")\n\n" + literal(entry.message);
        if (!entry.suggestion.empty()) {
            out += "\n\nhelp: " + literal(entry.suggestion);
        }
        out += "\n\n`camel --explain " + entry.name + "` prints this entry.";
        return out;
    }
    return {};
}

} // namespace camel::service::semantic
