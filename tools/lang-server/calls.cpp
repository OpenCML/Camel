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
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Source-level call hierarchy from compiler-resolved call references.
#include "server.h"
#include <filesystem>
#include <map>

namespace camel::ls {
json Server::callHierarchy(const std::string &method, const json &params) {
    json result     = json::array();
    const auto item = [&](const semantic::FileModel &file, const semantic::Decl &decl) {
        return json{
            {"name", decl.name},
            {"kind", 12},
            {"uri", pathToUri(file.path)},
            {"range", range(file, decl.fullRange)},
            {"selectionRange", range(file, decl.nameRange)},
            {"detail", decl.header}};
    };
    if (method == "textDocument/prepareCallHierarchy") {
        auto *doc = document(params);
        if (!doc)
            return result;
        const auto &model = modelOf(*doc);
        for (const auto &loc : model.definition(offsetAt(*doc, params))) {
            const auto &file = model.files()[loc.file];
            const auto *decl = file.syntax.declAtName(loc.range.start);
            if (decl && decl->kind == semantic::DeclKind::Function)
                result.push_back(item(file, *decl));
        }
        return result;
    }
    const auto &requested    = params.at("item");
    const auto requestedPath = std::filesystem::absolute(uriToPath(requested.at("uri")))
                                   .lexically_normal()
                                   .generic_string();
    const auto sources       = workspaceSources();
    if (!sources.contains(requestedPath))
        return result;
    const auto targetModel = projectModel(requestedPath, sources.at(requestedPath));
    const auto &pos        = requested.at("selectionRange").at("start");
    const auto offset =
        targetModel->main().lines.offsetOfUtf16(pos.at("line"), pos.at("character"));
    const auto *targetDecl = targetModel->main().syntax.declAtName(offset);
    if (!targetDecl || targetDecl->kind != semantic::DeclKind::Function)
        return result;
    const bool incoming = method == "callHierarchy/incomingCalls";
    std::map<std::string, json> groups;
    for (const auto &[path, text] : sources) {
        if ((!incoming && path != requestedPath) ||
            (incoming && text.find(targetDecl->name) == std::string::npos))
            continue;
        checkCancelled();
        const auto model = projectModel(path, text);
        const auto &file = model->main();
        for (const auto &ref : file.syntax.refs) {
            if (!ref.callee)
                continue;
            const semantic::Decl *caller = nullptr;
            for (const auto &decl : file.syntax.decls)
                if (decl.kind == semantic::DeclKind::Function &&
                    decl.fullRange.contains(ref.range) &&
                    (!caller || decl.fullRange.length() < caller->fullRange.length()))
                    caller = &decl;
            if (!caller || (!incoming && caller->nameRange != targetDecl->nameRange))
                continue;
            for (const auto &loc : model->definition(ref.nameRange().start)) {
                const auto &dest   = model->files()[loc.file];
                const auto *callee = dest.syntax.declAtName(loc.range.start);
                if (!callee || callee->kind != semantic::DeclKind::Function)
                    continue;
                if (incoming &&
                    (dest.path != requestedPath || callee->nameRange != targetDecl->nameRange))
                    continue;
                const auto other = incoming ? item(file, *caller) : item(dest, *callee);
                const auto key   = other.dump();
                if (!groups.contains(key))
                    groups[key] =
                        json{{incoming ? "from" : "to", other}, {"fromRanges", json::array()}};
                groups[key]["fromRanges"].push_back(range(file, ref.nameRange()));
            }
        }
    }
    for (const auto &[key, group] : groups)
        result.push_back(group);
    return result;
}
} // namespace camel::ls
