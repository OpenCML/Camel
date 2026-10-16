/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and
 * conditions of the MIT license. You may obtain a copy of the MIT license at:
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
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/utils/windows_parser_guard.h"
// protocol.h includes nlohmann/json before anything that pulls in ANTLR.
#include "server.h"

#include "camel/core/error/diagnostics.h"
#include "camel/parse/parse.h"

#include "../format/fmt.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#define CAMEL_LS_POPEN _popen
#define CAMEL_LS_PCLOSE _pclose
#define CAMEL_LS_GETPID _getpid
#else
#include <unistd.h>
#define CAMEL_LS_POPEN popen
#define CAMEL_LS_PCLOSE pclose
#define CAMEL_LS_GETPID getpid
#endif

namespace camel::ls {

namespace fs = std::filesystem;
using semantic::DeclKind;
using semantic::FileModel;
using semantic::Location;

namespace {

// Formats a source text; empty when it does not parse.
std::optional<std::string> formatSource(const std::string &path, const std::string &content) {
    auto diagnostics = std::make_shared<camel::core::error::Diagnostics>("lsp-format", path);
    auto parser      = std::make_shared<camel::parse::CamelParser>(diagnostics);
    std::istringstream input(content);
    try {
        if (!parser->parseCST(input)) {
            return std::nullopt;
        }
        Formatter::Options options;
        Formatter formatter(*parser->tree(), options);
        return formatter.format();
    } catch (...) {
        return std::nullopt;
    }
}

int symbolKind(DeclKind kind) {
    switch (kind) {
    case DeclKind::Function:
        return 12; // Function
    case DeclKind::Type:
        return 23; // Struct
    case DeclKind::Namespace:
    case DeclKind::Module:
        return 3; // Namespace
    case DeclKind::Local:
        return 14; // Constant
    default:
        return 13; // Variable
    }
}

int completionItemKind(semantic::Completion::Kind kind) {
    using Kind = semantic::Completion::Kind;
    switch (kind) {
    case Kind::Function:
        return 3;
    case Kind::Variable:
    case Kind::Parameter:
        return 6;
    case Kind::Type:
        return 22;
    case Kind::TypeParameter:
        return 25;
    case Kind::Module:
        return 9;
    case Kind::Field:
        return 5;
    case Kind::Keyword:
        return 14;
    }
    return 1;
}

std::string lower(std::string text) {
    for (char &c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

} // namespace

Server::Server(Transport &transport, std::string executableDir)
    : transport_(transport), executableDir_(std::move(executableDir)) {}

int Server::run() {
    for (;;) {
        // Wait for a message; when none comes for a while, bring stale documents up to date.
        bool ended   = false;
        auto message = transport_.next(debounceMs_, ended);
        if (!message) {
            if (ended) {
                break;
            }
            try {
                refreshStale();
            } catch (...) {
            }
            message = transport_.next();
            if (!message) {
                break;
            }
        }
        if (message->malformed || !message->body.is_object()) {
            transport_.respondError(nullptr, ParseError, "the message is not a JSON object");
            continue;
        }
        const json &body = message->body;
        if (body.contains("method") && body["method"].is_string() &&
            body["method"].get<std::string>() == "exit") {
            return shutdown_ ? 0 : 1;
        }
        dispatch(message->body);
    }
    return shutdown_ ? 0 : 1;
}

void Server::dispatch(const json &message) {
    const bool isRequest = message.contains("id") && !message["id"].is_null();
    const json id        = isRequest ? message["id"] : json(nullptr);
    std::string method;
    try {
        if (!message.contains("method")) {
            // A response to a request of ours: the server sends none that it waits for.
            return;
        }
        if (!message["method"].is_string()) {
            throw RequestError{InvalidRequest, "the method of a message must be a string"};
        }
        method            = message["method"].get<std::string>();
        const json params = message.contains("params") ? message["params"] : json::object();
        if (!isRequest) {
            handleNotification(method, params);
            return;
        }
        if (transport_.takeCancelled(id)) {
            transport_.respondError(id, RequestCancelled, "cancelled");
            return;
        }
        activeRequest_    = id;
        const auto result = handleRequest(method, params);
        checkCancelled();
        transport_.respond(id, result);
    } catch (const RequestError &e) {
        if (isRequest) {
            transport_.respondError(id, e.code, e.message);
        }
    } catch (const json::exception &e) {
        if (isRequest) {
            transport_.respondError(
                id,
                InvalidParams,
                "invalid parameters for " + method + ": " + e.what());
        }
    } catch (const std::exception &e) {
        std::cerr << "camel-ls: " << method << " failed: " << e.what() << std::endl;
        if (isRequest) {
            transport_.respondError(id, InternalError, method + " failed: " + e.what());
        }
    } catch (...) {
        std::cerr << "camel-ls: " << method << " failed" << std::endl;
        if (isRequest) {
            transport_.respondError(id, InternalError, method + " failed");
        }
    }
}

void Server::handleNotification(const std::string &method, const json &params) {
    if (method == "textDocument/didOpen") {
        didOpen(params);
    } else if (method == "textDocument/didChange") {
        didChange(params);
    } else if (method == "textDocument/didClose") {
        didClose(params);
    }
    if (method == "textDocument/didSave" || method == "workspace/didChangeWatchedFiles" ||
        method == "workspace/didRenameFiles" || method == "workspace/didDeleteFiles" ||
        method == "workspace/didCreateFiles")
        invalidateModels();
    if (method == "workspace/didChangeWorkspaceFolders") {
        const auto &event = params.at("event");
        for (const auto &folder : event.at("removed")) {
            const auto path = uriToPath(folder.at("uri"));
            std::erase(workspaceRoots_, path);
        }
        for (const auto &folder : event.at("added"))
            workspaceRoots_.push_back(uriToPath(folder.at("uri")));
        invalidateModels();
    }
}

json Server::handleRequest(const std::string &method, const json &params) {
    if (method == "initialize") {
        return initialize(params);
    }
    if (method == "shutdown") {
        shutdown_ = true;
        return nullptr;
    }
    if (method == "textDocument/diagnostic") {
        Document *doc = document(params);
        json items    = doc ? diagnosticsOf(*doc) : json::array();
        if (doc) {
            doc->published = true;
        }
        return json{{"kind", "full"}, {"items", items}};
    }
    if (method == "textDocument/prepareCallHierarchy" || method == "callHierarchy/incomingCalls" ||
        method == "callHierarchy/outgoingCalls")
        return callHierarchy(method, params);
    if (method == "textDocument/typeDefinition")
        return readingSupport(method, params);
    if (method == "textDocument/foldingRange" || method == "textDocument/selectionRange" ||
        method == "textDocument/inlayHint" || method == "textDocument/codeAction")
        return readingSupport(method, params);
    if (method == "textDocument/hover") {
        return hover(params);
    }
    if (method == "textDocument/definition") {
        return definition(params);
    }
    if (method == "textDocument/references") {
        return references(params);
    }
    if (method == "textDocument/documentHighlight") {
        return documentHighlight(params);
    }
    if (method == "textDocument/documentSymbol") {
        return documentSymbol(params);
    }
    if (method == "workspace/symbol") {
        return workspaceSymbol(params);
    }
    if (method == "textDocument/completion") {
        return completion(params);
    }
    if (method == "textDocument/signatureHelp") {
        return signatureHelp(params);
    }
    if (method == "textDocument/prepareRename") {
        return prepareRename(params);
    }
    if (method == "textDocument/rename") {
        return rename(params);
    }
    if (method == "textDocument/semanticTokens/full") {
        return semanticTokens(params);
    }
    if (method == "textDocument/formatting") {
        return formatting(params);
    }
    if (method == "camel/graph") {
        return graph(params);
    }
    if (method == "camel/model") {
        // For tests and debugging: the model as text.
        Document *doc = document(params);
        return doc ? json(modelOf(*doc).dump()) : json(nullptr);
    }
    throw RequestError{MethodNotFound, "Method not found: " + method};
}

// ---------------------------------------------------------------------------------------
// Documents and models
// ---------------------------------------------------------------------------------------

Server::Document *Server::document(const json &params) {
    const std::string uri = params.at("textDocument").at("uri").get<std::string>();
    auto it               = documents_.find(uri);
    return it == documents_.end() ? nullptr : &it->second;
}

const semantic::SemanticModel &Server::modelOf(Document &doc) {
    if (doc.stale || !doc.model) {
        auto options     = analysisOptions(doc.path, doc.text);
        options.previous = doc.lastCompiled;
        doc.model        = semantic::analyze(options);
        doc.stale        = false;
        doc.published    = false;
        if (doc.model->main().compiled) {
            doc.lastCompiled = doc.model;
        }
    }
    return *doc.model;
}

semantic::AnalysisOptions
Server::analysisOptions(const std::string &path, const std::string &text) const {
    semantic::AnalysisOptions options;
    options.path         = path;
    options.text         = text;
    options.requireEntry = false;
    for (const auto &[uri, doc] : documents_) {
        options.sourceOverrides[fs::absolute(doc.path).lexically_normal().generic_string()] =
            doc.text;
    }
    return options;
}

void Server::invalidateModels(Document *edited) {
    // Imports can change while a previous model is incomplete. Invalidate conservatively;
    // models remain lazy and are rebuilt only for queries or pushed diagnostics.
    for (auto &[uri, doc] : documents_) {
        doc.stale = true;
        // Keep the edited document's last successful analysis for completion and signature
        // help while its new text is incomplete. Importers must discard their older models.
        if (&doc != edited)
            doc.lastCompiled.reset();
    }
    workspaceCache_.clear();
    projectModels_.clear();
    if (refreshDiagnostics_)
        transport_.send(
            json{
                {"jsonrpc", "2.0"},
                {"id", "diagnostics-" + std::to_string(++refreshId_)},
                {"method", "workspace/diagnostic/refresh"},
                {"params", nullptr}});
}

void Server::refreshStale() {
    for (auto &[uri, doc] : documents_) {
        if (pushDiagnostics_ && (doc.stale || !doc.model)) {
            modelOf(doc);
        }
        if (!doc.published && pushDiagnostics_) {
            publish(doc);
        }
    }
}

void Server::publish(Document &doc) {
    json params{{"uri", doc.uri}, {"diagnostics", diagnosticsOf(doc)}};
    if (!doc.version.is_null()) {
        params["version"] = doc.version;
    }
    doc.published = true;
    transport_.notify("textDocument/publishDiagnostics", params);
}

json Server::position(const FileModel &file, uint32_t offset) const {
    const auto p = file.lines.utf16PositionOf(offset);
    return json{{"line", p.line}, {"character", p.character}};
}

json Server::range(const FileModel &file, semantic::Range r) const {
    return json{{"start", position(file, r.start)}, {"end", position(file, r.end)}};
}

json Server::location(Document &doc, const Location &loc) {
    const auto &model     = modelOf(doc);
    const FileModel &file = model.files()[loc.file];
    return json{
        {"uri", loc.file == 0 ? doc.uri : pathToUri(file.path)},
        {"range", range(file, loc.range)}};
}

uint32_t Server::offsetAt(Document &doc, const json &params) {
    const json &pos = params.at("position");
    // Positions are unsigned in the protocol; a client that sends a negative one means 0.
    const auto line      = std::max<int64_t>(0, pos.at("line").get<int64_t>());
    const auto character = std::max<int64_t>(0, pos.at("character").get<int64_t>());
    return modelOf(doc).main().lines.offsetOfUtf16(
        static_cast<uint32_t>(std::min<int64_t>(line, UINT32_MAX)),
        static_cast<uint32_t>(std::min<int64_t>(character, UINT32_MAX)));
}

json Server::diagnosticsOf(Document &doc) {
    const auto &model = modelOf(doc);
    json items        = json::array();
    for (const semantic::DiagnosticInfo &d : model.diagnostics()) {
        if (d.file != 0) {
            continue; // an imported module's own errors: its import carries one here
        }
        json item{
            {"range", range(model.main(), d.range)},
            {"severity", d.severity},
            {"source", "Camel"},
            {"message", d.message},
            {"code", d.code}};
        json data{{"name", d.name}};
        if (!d.suggestion.empty()) {
            data["suggestion"] = d.suggestion;
        }
        if (!d.notes.empty()) {
            data["notes"] = d.notes;
        }
        item["data"] = data;
        if (!d.related.empty()) {
            json related = json::array();
            for (const auto &label : d.related) {
                related.push_back(
                    json{
                        {"location", location(doc, Location{label.file, label.range})},
                        {"message", label.message}});
            }
            item["relatedInformation"] = related;
        }
        items.push_back(std::move(item));
    }
    return items;
}

// ---------------------------------------------------------------------------------------
// Lifecycle and synchronisation
// ---------------------------------------------------------------------------------------

json Server::initialize(const json &params) {
    const auto workspace =
        params.value("capabilities", json::object()).value("workspace", json::object());
    refreshDiagnostics_ =
        workspace.value("diagnostics", json::object()).value("refreshSupport", false);
    documentChanges_ =
        workspace.value("workspaceEdit", json::object()).value("documentChanges", false);
    // A client that pulls diagnostics gets them only that way: pushing as well would show
    // every problem twice. `initializationOptions.diagnostics` ("push", "pull", "both")
    // overrides the choice.
    bool clientPulls = false;
    if (params.contains("capabilities") && params["capabilities"].contains("textDocument") &&
        params["capabilities"]["textDocument"].contains("diagnostic")) {
        clientPulls = true;
    }
    pushDiagnostics_ = !clientPulls;
    if (params.contains("initializationOptions") && params["initializationOptions"].is_object()) {
        const json &options    = params["initializationOptions"];
        const std::string mode = options.value("diagnostics", "");
        if (mode == "push" || mode == "both") {
            pushDiagnostics_ = true;
        } else if (mode == "pull") {
            pushDiagnostics_ = false;
        }
        if (options.contains("debounceMs") && options["debounceMs"].is_number_integer()) {
            debounceMs_ = std::clamp(options["debounceMs"].get<int>(), 0, 5000);
        }
    }
    if (params.contains("workspaceFolders") && params["workspaceFolders"].is_array()) {
        for (const json &folder : params["workspaceFolders"]) {
            if (folder.contains("uri") && folder["uri"].is_string()) {
                workspaceRoots_.push_back(uriToPath(folder["uri"].get<std::string>()));
            }
        }
    }
    if (workspaceRoots_.empty() && params.contains("rootUri") && params["rootUri"].is_string()) {
        workspaceRoots_.push_back(uriToPath(params["rootUri"].get<std::string>()));
    }

    json capabilities{
        {"positionEncoding", "utf-16"},
        {"textDocumentSync", json{{"openClose", true}, {"change", 2}, {"save", json::object()}}},
        {"diagnosticProvider",
         json{{"interFileDependencies", true}, {"workspaceDiagnostics", false}}},
        {"documentFormattingProvider", true},
        {"hoverProvider", true},
        {"callHierarchyProvider", true},
        {"typeDefinitionProvider", true},
        {"foldingRangeProvider", true},
        {"selectionRangeProvider", true},
        {"inlayHintProvider", true},
        {"codeActionProvider", json{{"codeActionKinds", {"quickfix"}}}},
        {"workspace",
         json{{"workspaceFolders", json{{"supported", true}, {"changeNotifications", true}}}}},
        {"definitionProvider", true},
        {"referencesProvider", true},
        {"documentHighlightProvider", true},
        {"documentSymbolProvider", true},
        {"workspaceSymbolProvider", true},
        {"completionProvider", json{{"triggerCharacters", json::array({".", ":"})}}},
        {"signatureHelpProvider", json{{"triggerCharacters", json::array({"(", ","})}}},
        {"renameProvider", json{{"prepareProvider", true}}},
        {"semanticTokensProvider",
         json{
             {"legend",
              json{
                  {"tokenTypes", semantic::semanticTokenTypes()},
                  {"tokenModifiers", semantic::semanticTokenModifiers()}}},
             {"full", true},
             {"range", false}}},
        {"experimental", json{{"camelGraphProvider", true}}},
    };
    return json{
        {"capabilities", capabilities},
        {"serverInfo", json{{"name", "camel-ls"}, {"version", "0.3.0"}}}};
}

void Server::didOpen(const json &params) {
    const json &item = params.at("textDocument");
    Document doc;
    doc.uri             = item.at("uri").get<std::string>();
    doc.path            = uriToPath(doc.uri);
    doc.text            = item.at("text").get<std::string>();
    doc.version         = item.contains("version") ? item["version"] : json(nullptr);
    documents_[doc.uri] = std::move(doc);
    invalidateModels();
}

void Server::didChange(const json &params) {
    Document *doc = document(params);
    if (doc == nullptr) {
        return;
    }
    for (const json &change : params.at("contentChanges")) {
        if (!change.contains("text")) {
            continue;
        }
        const std::string text = change["text"].get<std::string>();
        if (!change.contains("range")) {
            doc->text = text;
            continue;
        }
        // Each change applies to the text the previous one produced.
        const semantic::LineIndex lines(doc->text);
        const auto at = [&](const json &pos) {
            return lines.offsetOfUtf16(
                static_cast<uint32_t>(std::max<int64_t>(0, pos.at("line").get<int64_t>())),
                static_cast<uint32_t>(std::max<int64_t>(0, pos.at("character").get<int64_t>())));
        };
        uint32_t start = at(change["range"].at("start"));
        uint32_t end   = at(change["range"].at("end"));
        if (end < start) {
            std::swap(start, end);
        }
        doc->text.replace(start, end - start, text);
    }
    doc->version = params.at("textDocument").contains("version") ? params["textDocument"]["version"]
                                                                 : json(nullptr);
    invalidateModels(doc);
}

void Server::didClose(const json &params) {
    Document *doc = document(params);
    if (doc == nullptr) {
        return;
    }
    const std::string uri = doc->uri;
    documents_.erase(uri);
    invalidateModels();
    if (pushDiagnostics_) {
        // The problems of a closed document are no longer the server's to show.
        transport_.notify(
            "textDocument/publishDiagnostics",
            json{{"uri", uri}, {"diagnostics", json::array()}});
    }
}

// ---------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------

json Server::hover(const json &params) {
    Document *doc = document(params);
    if (doc == nullptr) {
        return nullptr;
    }
    const uint32_t offset = offsetAt(*doc, params);
    const auto &model     = modelOf(*doc);
    auto hover            = model.hover(offset);
    // On a problem: what its code means.
    std::string explained;
    for (const semantic::DiagnosticInfo &d : model.diagnostics()) {
        if (d.file == 0 && d.range.touches(offset)) {
            const std::string text = semantic::explainDiagnostic(d.code);
            if (!text.empty() && explained.find(text) == std::string::npos) {
                explained += (explained.empty() ? "" : "\n\n---\n\n") + text;
            }
        }
    }
    if (!hover && explained.empty()) {
        return nullptr;
    }
    std::string markdown = hover ? hover->markdown : std::string();
    if (!explained.empty()) {
        markdown += (markdown.empty() ? "" : "\n---\n\n") + explained;
    }
    json result{{"contents", json{{"kind", "markdown"}, {"value", markdown}}}};
    if (hover) {
        result["range"] = range(model.main(), hover->range);
    }
    return result;
}

json Server::definition(const json &params) {
    Document *doc = document(params);
    json result   = json::array();
    if (doc == nullptr) {
        return result;
    }
    for (const Location &loc : modelOf(*doc).definition(offsetAt(*doc, params))) {
        result.push_back(location(*doc, loc));
    }
    return result;
}

json Server::references(const json &params) {
    Document *doc = document(params);
    json result   = json::array();
    if (!doc)
        return result;
    const auto &model  = modelOf(*doc);
    const auto targets = model.definition(offsetAt(*doc, params));
    const bool include =
        params.contains("context") && params["context"].value("includeDeclaration", false);
    std::set<std::string> seen;
    const auto add = [&](const semantic::SemanticModel &m, const Location &loc) {
        const auto &file = m.files()[loc.file];
        json item{{"uri", pathToUri(file.path)}, {"range", range(file, loc.range)}};
        if (seen.insert(item.dump()).second)
            result.push_back(item);
    };
    for (const auto &loc : model.references(offsetAt(*doc, params), include))
        add(model, loc);
    if (targets.empty())
        return result;
    const auto &targetFile = model.files()[targets.front().file];
    const auto name =
        targetFile.text.substr(targets.front().range.start, targets.front().range.length());
    for (const auto &[path, text] : workspaceSources()) {
        checkCancelled();
        if (text.find(name) == std::string::npos)
            continue;
        const auto other = projectModel(path, text);
        std::vector<Location> mapped;
        for (const auto &target : targets)
            if (auto file = other->fileOf(model.files()[target.file].path))
                mapped.push_back({*file, target.range});
        for (const auto &loc : other->referencesTo(mapped, include))
            add(*other, loc);
    }
    return result;
}

json Server::documentHighlight(const json &params) {
    Document *doc = document(params);
    json result   = json::array();
    if (doc == nullptr) {
        return result;
    }
    const auto &model = modelOf(*doc);
    for (const auto &[r, declaration] : model.highlights(offsetAt(*doc, params))) {
        // 3: a write (the declaration binds the name), 2: a read.
        result.push_back(json{{"range", range(model.main(), r)}, {"kind", declaration ? 3 : 2}});
    }
    return result;
}

json Server::documentSymbol(const json &params) {
    Document *doc = document(params);
    if (doc == nullptr) {
        return json::array();
    }
    const auto &model = modelOf(*doc);
    std::function<json(const std::vector<semantic::OutlineItem> &)> convert =
        [&](const std::vector<semantic::OutlineItem> &items) {
            json out = json::array();
            for (const semantic::OutlineItem &item : items) {
                json symbol{
                    {"name", item.name},
                    {"kind", symbolKind(item.kind)},
                    {"range", range(model.main(), item.range)},
                    {"selectionRange", range(model.main(), item.selection)}};
                if (!item.detail.empty()) {
                    symbol["detail"] = item.detail;
                }
                if (!item.children.empty()) {
                    symbol["children"] = convert(item.children);
                }
                out.push_back(std::move(symbol));
            }
            return out;
        };
    return convert(model.outline());
}

json Server::workspaceSymbol(const json &params) {
    const std::string query = lower(
        params.contains("query") && params["query"].is_string() ? params["query"].get<std::string>()
                                                                : std::string());
    json result        = json::array();
    const auto addFrom = [&](const FileModel &file, const std::string &uri) {
        for (const semantic::Decl &decl : file.syntax.decls) {
            const bool listed =
                decl.kind == DeclKind::Function || decl.kind == DeclKind::Type ||
                ((decl.kind == DeclKind::Local || decl.kind == DeclKind::Variable) &&
                 decl.scope == 0);
            if (!listed || (!query.empty() && lower(decl.name).find(query) == std::string::npos)) {
                continue;
            }
            json symbol{
                {"name", decl.name},
                {"kind", symbolKind(decl.kind)},
                {"location", json{{"uri", uri}, {"range", range(file, decl.nameRange)}}}};
            if (decl.container >= 0) {
                symbol["containerName"] = file.syntax.decls[decl.container].name;
            }
            result.push_back(std::move(symbol));
        }
    };
    std::set<std::string> open;
    for (auto &[uri, doc] : documents_) {
        addFrom(modelOf(doc).main(), uri);
        open.insert(fs::path(doc.path).lexically_normal().generic_string());
    }
    // The source files of the workspace that are not open: parsed, not compiled.
    size_t visited = 0;
    for (const std::string &root : workspaceRoots_) {
        std::error_code ec;
        fs::recursive_directory_iterator it(
            root,
            fs::directory_options::skip_permission_denied,
            ec),
            end;
        for (; !ec && it != end; it.increment(ec)) {
            const fs::path &path   = it->path();
            const std::string name = path.filename().string();
            if (it->is_directory(ec)) {
                if (name.starts_with(".") || name == "node_modules" || name == "build" ||
                    name == "out" || name == "tmp") {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (path.extension() != ".cml" || ++visited > 5000) {
                continue;
            }
            const std::string normal = path.lexically_normal().generic_string();
            if (open.contains(normal)) {
                continue;
            }
            const std::string stamp =
                // The tick count is __int128 with libc++, which std::to_string does not take.
                std::to_string(
                    static_cast<long long>(
                        fs::last_write_time(path, ec).time_since_epoch().count())) +
                ":" + std::to_string(fs::file_size(path, ec));
            auto &cached = workspaceCache_[normal];
            if (!cached.second || cached.first != stamp) {
                std::ifstream in(path, std::ios::binary);
                std::stringstream buffer;
                buffer << in.rdbuf();
                semantic::AnalysisOptions options;
                options.path    = normal;
                options.text    = buffer.str();
                options.compile = false;
                cached          = {stamp, semantic::analyze(options)};
            }
            addFrom(cached.second->main(), pathToUri(normal));
        }
    }
    return result;
}

json Server::completion(const json &params) {
    Document *doc = document(params);
    json items    = json::array();
    if (doc == nullptr) {
        return items;
    }
    for (const semantic::Completion &c : modelOf(*doc).completions(offsetAt(*doc, params))) {
        json item{
            {"label", c.label},
            {"kind", completionItemKind(c.kind)},
            {"sortText", std::to_string(c.rank) + c.label}};
        if (!c.insert.empty()) {
            // Shown by one name, written by another (a namespace member): matched by what
            // is typed, which is the written one.
            item["insertText"] = c.insert;
            item["filterText"] = c.insert;
        }
        if (!c.detail.empty()) {
            item["detail"] = c.detail;
        }
        if (!c.doc.empty()) {
            item["documentation"] = c.doc;
        }
        items.push_back(std::move(item));
    }
    return json{{"isIncomplete", false}, {"items", items}};
}

json Server::signatureHelp(const json &params) {
    Document *doc = document(params);
    if (doc == nullptr) {
        return nullptr;
    }
    auto help = modelOf(*doc).signatureHelp(offsetAt(*doc, params));
    if (!help) {
        return nullptr;
    }
    json signatures = json::array();
    for (const semantic::SignatureInfo &info : help->signatures) {
        json parameters = json::array();
        // Labels as [start, end) in UTF-16 units of the signature label.
        const semantic::LineIndex lines(info.label);
        for (const auto &[start, end] : info.parameters) {
            parameters.push_back(
                json{
                    {"label",
                     json::array(
                         {lines.utf16PositionOf(start).character,
                          lines.utf16PositionOf(end).character})}});
        }
        json signature{{"label", info.label}, {"parameters", parameters}};
        if (!info.doc.empty()) {
            signature["documentation"] = info.doc;
        }
        signatures.push_back(std::move(signature));
    }
    return json{
        {"signatures", signatures},
        {"activeSignature", help->activeSignature},
        {"activeParameter", help->activeParameter}};
}

json Server::prepareRename(const json &params) { return workspaceRename(params, true); }

json Server::rename(const json &params) { return workspaceRename(params, false); }

json Server::semanticTokens(const json &params) {
    Document *doc = document(params);
    json data     = json::array();
    if (doc == nullptr) {
        return json{{"data", data}};
    }
    const auto &model     = modelOf(*doc);
    const FileModel &file = model.main();
    uint32_t prevLine     = 0;
    uint32_t prevStart    = 0;
    for (const semantic::SemanticToken &token : model.semanticTokens()) {
        const auto start = file.lines.utf16PositionOf(token.range.start);
        const auto end   = file.lines.utf16PositionOf(token.range.end);
        if (end.line != start.line || end.character <= start.character) {
            continue;
        }
        data.push_back(start.line - prevLine);
        data.push_back(start.line == prevLine ? start.character - prevStart : start.character);
        data.push_back(end.character - start.character);
        data.push_back(token.type);
        data.push_back(token.modifiers);
        prevLine  = start.line;
        prevStart = start.character;
    }
    return json{{"data", data}};
}

json Server::formatting(const json &params) {
    Document *doc = document(params);
    json edits    = json::array();
    if (doc == nullptr) {
        return edits;
    }
    auto formatted = formatSource(doc->path, doc->text);
    if (formatted.has_value() && *formatted != doc->text) {
        const semantic::LineIndex lines(doc->text);
        const auto end = lines.utf16PositionOf(static_cast<uint32_t>(doc->text.size()));
        edits.push_back(
            json{
                {"range",
                 json{
                     {"start", json{{"line", 0}, {"character", 0}}},
                     {"end", json{{"line", end.line}, {"character", end.character}}}}},
                {"newText", *formatted}});
    }
    return edits;
}

// ---------------------------------------------------------------------------------------
// camel/graph: the graph of the function at the cursor
// ---------------------------------------------------------------------------------------

namespace {

// The part of a DOT text printed by std::gir that belongs to the function `name`: its
// `subgraph cluster` block, wrapped as a graph of its own. The whole text when there is no
// such block.
std::string clusterOf(const std::string &dot, const std::string &name) {
    if (name.empty()) {
        return dot;
    }
    const std::regex header(
        "subgraph\\s+\"?cluster[^\\n{]*\\{[^\\n]*\\n[^\\n]*label\\s*=\\s*\"" + name + "[\"\\s(:<]");
    std::smatch match;
    if (!std::regex_search(dot, match, header)) {
        return dot;
    }
    const size_t start = static_cast<size_t>(match.position(0));
    int depth          = 0;
    bool inString      = false;
    for (size_t i = start; i < dot.size(); ++i) {
        const char c = dot[i];
        if (c == '"' && (i == 0 || dot[i - 1] != '\\')) {
            inString = !inString;
        } else if (!inString && c == '{') {
            ++depth;
        } else if (!inString && c == '}') {
            if (--depth == 0) {
                return "digraph \"" + name + "\" {\n" + dot.substr(start, i + 1 - start) + "\n}\n";
            }
        }
    }
    return dot;
}

} // namespace

json Server::graph(const json &params) {
    Document *doc = document(params);
    if (doc == nullptr) {
        throw RequestError{InvalidParams, "the document is not open"};
    }
    std::vector<std::string> passes;
    if (params.contains("passes") && params["passes"].is_array()) {
        static const std::regex passName("^[A-Za-z_][A-Za-z0-9_:./-]*$");
        for (const json &pass : params["passes"]) {
            const std::string name = pass.get<std::string>();
            // The names go on a command line.
            if (!std::regex_match(name, passName)) {
                throw RequestError{InvalidParams, "not a pass name: " + name};
            }
            passes.push_back(name);
        }
    }
    std::string function;
    if (params.contains("position")) {
        function = modelOf(*doc).functionAround(offsetAt(*doc, params));
    }
    // The compiler runs as a process of its own: a pass prints to standard output, which in
    // this process is the protocol stream, and a fault in a pass must not end the server.
    // It reads the text of the editor, not of the disk, from a file beside the document so
    // that relative imports resolve.
    const std::string exe =
#ifdef _WIN32
        "camel.exe";
#else
        "camel";
#endif
    fs::path camel = fs::path(executableDir_) / exe;
    std::error_code ec;
    if (executableDir_.empty() || !fs::exists(camel, ec)) {
        camel = exe; // from PATH
    }
    const fs::path dir     = fs::path(doc->path).parent_path();
    const fs::path scratch = dir / (".camel-ls-" + std::to_string(CAMEL_LS_GETPID()) + "-" +
                                    fs::path(doc->path).filename().string());
    {
        std::ofstream out(scratch, std::ios::binary);
        if (!out) {
            throw RequestError{
                RequestFailed,
                "cannot write beside the document: " + scratch.string()};
        }
        out << doc->text;
    }
    std::string command = "\"" + camel.string() + "\" --color never \"" + scratch.string() + "\"";
    for (const std::string &pass : passes) {
        command += " " + pass;
    }
    command += " std::gir 2>&1";
#ifdef _WIN32
    command = "\"" + command + "\""; // cmd.exe strips one pair of quotes
#endif
    std::string output;
    int status = -1;
    if (FILE *pipe = CAMEL_LS_POPEN(command.c_str(), "r")) {
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, pipe)) > 0) {
            output.append(buffer, n);
        }
        status = CAMEL_LS_PCLOSE(pipe);
    }
    fs::remove(scratch, ec);
    // Mentions of the scratch file are mentions of the document.
    for (size_t at = output.find(scratch.string()); at != std::string::npos;
         at        = output.find(scratch.string(), at)) {
        output.replace(at, scratch.string().size(), doc->path);
    }
    const size_t dotAt = output.find("digraph");
    if (status != 0 || dotAt == std::string::npos) {
        return json{
            {"function", function.empty() ? json(nullptr) : json(function)},
            {"passes", passes},
            {"text", ""},
            {"diagnostics", output}};
    }
    json result{
        {"function", function.empty() ? json(nullptr) : json(function)},
        {"passes", passes},
        {"text", clusterOf(output.substr(dotAt), function)}};
    if (dotAt > 0) {
        result["diagnostics"] = output.substr(0, dotAt);
    }
    return result;
}

} // namespace camel::ls
