// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Workspace snapshots, cancellation boundaries and atomic, validated symbol renames.

#include "server.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace camel::ls {
namespace fs = std::filesystem;
using semantic::Location;

void Server::checkCancelled() {
    if (!activeRequest_.is_null() && transport_.takeCancelled(activeRequest_))
        throw RequestError{RequestCancelled, "cancelled"};
}

std::map<std::string, std::string> Server::workspaceSources() {
    std::map<std::string, std::string> sources;
    for (const auto &root : workspaceRoots_) {
        std::error_code ec;
        fs::recursive_directory_iterator it(
            root,
            fs::directory_options::skip_permission_denied,
            ec),
            end;
        for (; !ec && it != end; it.increment(ec)) {
            checkCancelled();
            const auto name = it->path().filename().string();
            if (it->is_directory(ec)) {
                if (name.starts_with(".") || name == "node_modules" || name == "build" ||
                    name == "out" || name == "tmp")
                    it.disable_recursion_pending();
                continue;
            }
            if (it->path().extension() != ".cml" || name.starts_with("."))
                continue;
            if (sources.size() >= 5000)
                throw RequestError{
                    RequestFailed,
                    "Workspace exceeds 5000 source files; open a smaller workspace for complete "
                    "references and rename"};
            std::ifstream in(it->path(), std::ios::binary);
            if (!in)
                throw RequestError{
                    RequestFailed,
                    "Cannot read workspace source: " + it->path().string()};
            std::ostringstream text;
            text << in.rdbuf();
            sources[fs::absolute(it->path()).lexically_normal().generic_string()] = text.str();
        }
        if (ec)
            throw RequestError{
                RequestFailed,
                "Cannot enumerate workspace: " + root + ": " + ec.message()};
    }
    for (const auto &[uri, doc] : documents_)
        sources[fs::absolute(doc.path).lexically_normal().generic_string()] = doc.text;
    return sources;
}

semantic::model_ptr_t Server::projectModel(const std::string &path, const std::string &text) {
    checkCancelled();
    auto &cached = projectModels_[path];
    if (!cached || cached->main().text != text)
        cached = semantic::analyze(analysisOptions(path, text));
    checkCancelled();
    return cached;
}

json Server::workspaceRename(const json &params, bool prepare) {
    auto *doc = document(params);
    if (!doc)
        return nullptr;
    const auto &model         = modelOf(*doc);
    const auto offset         = offsetAt(*doc, params);
    const auto targets        = model.definition(offset);
    const std::string newName = prepare ? "" : params.at("newName").get<std::string>();
    if (targets.empty()) {
        // Keep the precise local refusal for keywords, fields and library-only symbols.
        const auto checked = model.rename(offset, newName);
        if (!checked.refusal.empty())
            throw RequestError{RequestFailed, checked.refusal};
        throw RequestError{RequestFailed, "No source declaration to rename"};
    }
    const auto &target = targets.front();
    const auto &owner  = model.files()[target.file];
    const auto *decl   = owner.syntax.declAtName(target.range.start);
    if (!decl)
        throw RequestError{RequestFailed, "Cannot identify the declaration"};
    // Keep the established precise, conservative checks for local symbols.
    if (target.file == 0 && !decl->exported) {
        const auto result = model.rename(offset, newName);
        if (!result.refusal.empty())
            throw RequestError{RequestFailed, result.refusal};
        if (prepare)
            return json{
                {"range", range(model.main(), result.range)},
                {"placeholder", result.placeholder}};
        json edits = json::array();
        for (const auto &loc : result.edits)
            edits.push_back(json{{"range", range(model.main(), loc.range)}, {"newText", newName}});
        if (!documentChanges_)
            return json{{"changes", json{{doc->uri, edits}}}};
        return json{
            {"documentChanges",
             json::array({json{
                 {"textDocument", json{{"uri", doc->uri}, {"version", doc->version}}},
                 {"edits", edits}}})}};
    }
    auto sources = workspaceSources();
    if (!sources.contains(owner.path))
        throw RequestError{RequestFailed, "The declaration is outside the workspace"};
    if (decl->kind != semantic::DeclKind::Function)
        throw RequestError{RequestFailed, "Workspace rename currently supports exported functions"};
    const auto ownerModel = projectModel(owner.path, sources.at(owner.path));
    const auto checked    = ownerModel->rename(target.range.start, newName, true);
    if (!checked.refusal.empty())
        throw RequestError{RequestFailed, checked.refusal};
    const auto token = model.main().syntax.tokenAt(offset);
    if (prepare)
        return json{
            {"range",
             range(
                 model.main(),
                 token >= 0 ? model.main().syntax.tokens[token].range : target.range)},
            {"placeholder", decl->name}};
    std::map<std::string, std::vector<semantic::Range>> changes;
    for (const auto &[path, text] : sources) {
        checkCancelled();
        if (text.find(decl->name) == std::string::npos)
            continue;
        const auto m = projectModel(path, text);
        if (!m->main().compiled)
            throw RequestError{RequestFailed, "Fix errors before workspace rename: " + path};
        std::vector<Location> mapped;
        if (auto f = m->fileOf(owner.path)) {
            // A rename covers the overload set, not just the overload chosen by this call.
            for (const auto &d : m->files()[*f].syntax.decls)
                if (d.name == decl->name && d.scope == decl->scope && d.kind == decl->kind)
                    mapped.push_back({*f, d.nameRange});
        }
        const auto uses = m->referencesTo(mapped, true);
        if (uses.empty())
            continue;
        if (newName != decl->name) {
            for (const auto &d : m->main().syntax.decls)
                if (d.name == newName)
                    throw RequestError{
                        RequestFailed,
                        "Rename would collide with '" + newName + "' in " + path};
            for (const auto &ref : m->main().syntax.refs)
                for (const auto &part : ref.path)
                    if (part.name == newName)
                        throw RequestError{
                            RequestFailed,
                            "Rename may capture an existing use of '" + newName + "' in " + path};
        }
        for (const auto &loc : uses) {
            const auto &file = m->files()[loc.file];
            if (!sources.contains(file.path))
                throw RequestError{RequestFailed, "A reference is outside the workspace"};
            if (file.text.substr(loc.range.start, loc.range.length()) != decl->name)
                throw RequestError{RequestFailed, "An aliased import requires a separate rename"};
            auto &ranges = changes[file.path];
            if (std::find(ranges.begin(), ranges.end(), loc.range) == ranges.end())
                ranges.push_back(loc.range);
        }
    }
    auto rewritten = sources;
    for (auto &[path, ranges] : changes) {
        std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) { return a.start > b.start; });
        for (auto r : ranges)
            rewritten[path].replace(r.start, r.length(), newName);
    }
    // Validate the complete edited snapshot before returning any edits. Nothing is written.
    for (const auto &[path, ranges] : changes) {
        checkCancelled();
        auto options            = analysisOptions(path, rewritten[path]);
        options.sourceOverrides = rewritten;
        if (!semantic::analyze(options)->main().compiled)
            throw RequestError{RequestFailed, "The renamed program does not compile: " + path};
    }
    json documents = json::array();
    for (const auto &[path, ranges] : changes) {
        const semantic::LineIndex lines(sources.at(path));
        json edits = json::array();
        for (const auto r : ranges) {
            const auto a = lines.utf16PositionOf(r.start), b = lines.utf16PositionOf(r.end);
            edits.push_back(
                json{
                    {"range",
                     json{
                         {"start", json{{"line", a.line}, {"character", a.character}}},
                         {"end", json{{"line", b.line}, {"character", b.character}}}}},
                    {"newText", newName}});
        }
        json version = nullptr;
        for (const auto &[uri, open] : documents_)
            if (fs::absolute(open.path).lexically_normal().generic_string() == path)
                version = open.version;
        documents.push_back(
            json{
                {"textDocument", json{{"uri", pathToUri(path)}, {"version", version}}},
                {"edits", edits}});
    }
    if (!documentChanges_) {
        json edits = json::object();
        for (const auto &document : documents)
            edits[document["textDocument"]["uri"].get<std::string>()] = document["edits"];
        return json{{"changes", edits}};
    }
    return json{{"documentChanges", documents}};
}
} // namespace camel::ls
