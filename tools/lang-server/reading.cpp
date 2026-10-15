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

// Syntax-based reading aids and compiler-validated typo corrections.
#include "server.h"
#include <algorithm>
#include <regex>
#include <set>

namespace camel::ls {
json Server::readingSupport(const std::string &method, const json &params) {
    auto *doc   = document(params);
    json result = json::array();
    if (!doc)
        return result;
    const auto &model = modelOf(*doc);
    const auto &file  = model.main();
    if (method == "textDocument/typeDefinition") {
        const auto offset = offsetAt(*doc, params);
        auto declarations = model.definition(offset);
        for (const auto &target : declarations) {
            const auto &source = model.files()[target.file];
            const auto *decl   = source.syntax.declAtName(target.range.start);
            if (!decl)
                continue;
            if (decl->kind == semantic::DeclKind::Type) {
                result.push_back(
                    json{
                        {"uri", pathToUri(source.path)},
                        {"range", range(source, decl->nameRange)}});
                continue;
            }
            // Explicit named annotations have an unambiguous source identity. Do not
            // guess a typedef from structural equality of inferred types.
            if (decl->annotation.empty())
                continue;
            const auto owner = projectModel(source.path, source.text);
            for (const auto &ref : source.syntax.refs) {
                if (ref.kind != semantic::RefKind::Type || ref.range.start < decl->nameRange.end ||
                    !decl->ownerRange.contains(ref.range))
                    continue;
                for (const auto &loc : owner->definition(ref.nameRange().start)) {
                    const auto &typeFile = owner->files()[loc.file];
                    result.push_back(
                        json{
                            {"uri", pathToUri(typeFile.path)},
                            {"range", range(typeFile, loc.range)}});
                }
            }
        }
    } else if (method == "textDocument/foldingRange") {
        std::set<std::pair<uint32_t, uint32_t>> seen;
        const auto add = [&](semantic::Range r, const std::string &kind) {
            const auto a = file.lines.utf16PositionOf(r.start),
                       b = file.lines.utf16PositionOf(r.end);
            if (b.line > a.line && seen.emplace(a.line, b.line).second)
                result.push_back(json{{"startLine", a.line}, {"endLine", b.line}, {"kind", kind}});
        };
        for (const auto &scope : file.syntax.scopes)
            if (scope.parent >= 0)
                add(scope.range, "region");
        for (const auto &token : file.syntax.tokens)
            if (token.channel >= 3)
                add(token.range, "comment");
    } else if (method == "textDocument/selectionRange") {
        for (const auto &pos : params.at("positions")) {
            const auto offset = file.lines.offsetOfUtf16(pos.at("line"), pos.at("character"));
            std::vector<semantic::Range> ranges{{0, static_cast<uint32_t>(file.text.size())}};
            for (const auto &scope : file.syntax.scopes)
                if (scope.range.touches(offset))
                    ranges.push_back(scope.range);
            const auto token = file.syntax.tokenAt(offset);
            if (token >= 0)
                ranges.push_back(file.syntax.tokens[token].range);
            std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) {
                return a.length() > b.length();
            });
            json parent = nullptr;
            semantic::Range previous{0, static_cast<uint32_t>(file.text.size())};
            for (auto r : ranges) {
                if (!previous.contains(r) || (!parent.is_null() && r == previous))
                    continue;
                json item{{"range", range(file, r)}};
                if (!parent.is_null())
                    item["parent"] = parent;
                parent   = item;
                previous = r;
            }
            result.push_back(parent);
        }
    } else if (method == "textDocument/inlayHint") {
        if (!file.compiled)
            return result;
        const auto &requested = params.at("range");
        const auto start =
            file.lines.offsetOfUtf16(requested["start"]["line"], requested["start"]["character"]);
        const auto end =
            file.lines.offsetOfUtf16(requested["end"]["line"], requested["end"]["character"]);
        for (const auto &decl : file.syntax.decls) {
            if (decl.kind != semantic::DeclKind::Local && decl.kind != semantic::DeclKind::Variable)
                continue;
            const auto &facts = file.declFacts[decl.id];
            if (!decl.annotation.empty() || facts.types.size() != 1 || decl.nameRange.end < start ||
                decl.nameRange.end > end)
                continue;
            result.push_back(
                json{
                    {"position", position(file, decl.nameRange.end)},
                    {"label", ": " + model.type(facts.types.front()).text},
                    {"kind", 1},
                    {"paddingLeft", false}});
        }
        // Name hints for standalone literal arguments. Complex expressions and
        // ambiguous overloads intentionally stay quiet rather than guess a parameter.
        std::vector<const semantic::TokenInfo *> tokens;
        for (const auto &token : file.syntax.tokens)
            if (token.channel == 0)
                tokens.push_back(&token);
        const auto spelling = [&](size_t i) {
            const auto r = tokens[i]->range;
            return file.text.substr(r.start, r.length());
        };
        for (size_t i = 1; i + 1 < tokens.size(); ++i) {
            const auto r = tokens[i]->range;
            if (r.start < start || r.end > end)
                continue;
            const auto before = spelling(i - 1), after = spelling(i + 1), text = spelling(i);
            if ((before != "(" && before != ",") || (after != ")" && after != ",") || text.empty())
                continue;
            if (!(text.front() == '"' || (text.front() >= '0' && text.front() <= '9') ||
                  text == "true" || text == "false"))
                continue;
            const auto help = model.signatureHelp(r.start);
            if (!help || help->signatures.size() != 1 ||
                help->activeParameter >= help->signatures[0].parameters.size())
                continue;
            const auto &signature = help->signatures[0];
            const auto bounds     = signature.parameters[help->activeParameter];
            const auto parameter =
                signature.label.substr(bounds.first, bounds.second - bounds.first);
            std::smatch match;
            if (!std::regex_search(
                    parameter,
                    match,
                    std::regex(R"(^\s*(?:var\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*:)")))
                continue;
            result.push_back(
                json{
                    {"position", position(file, r.start)},
                    {"label", match[1].str() + ":"},
                    {"kind", 2},
                    {"paddingRight", true}});
        }
    } else if (method == "textDocument/codeAction") {
        if (params.contains("context") && params["context"].contains("only")) {
            bool requested = false;
            for (const auto &kind : params["context"]["only"])
                if (kind == "" || kind == "quickfix")
                    requested = true;
            if (!requested)
                return result;
        }
        const auto &requestRange = params.at("range");
        const auto start         = file.lines.offsetOfUtf16(
            requestRange["start"]["line"],
            requestRange["start"]["character"]);
        const auto end =
            file.lines.offsetOfUtf16(requestRange["end"]["line"], requestRange["end"]["character"]);
        for (const auto &diag : model.diagnostics()) {
            if (diag.file || (diag.code != "0x0300000B" && diag.code != "0x0300000b"))
                continue;
            if (diag.range.end < start || diag.range.start > end)
                continue;
            const auto *ref = file.syntax.refAt(diag.range.start);
            if (!ref || ref->path.size() != 1)
                continue;
            const auto &name = ref->name();
            std::set<std::string> candidates;
            // A single substitution, insertion or deletion; never rewrite a diagnostic's prose.
            for (const auto &candidate : model.completions(ref->range.start)) {
                const auto &other = candidate.label;
                if (name == other || name.size() + 1 < other.size() ||
                    other.size() + 1 < name.size())
                    continue;
                size_t i = 0, j = 0, distance = 0;
                while (i < name.size() && j < other.size()) {
                    if (name[i] == other[j]) {
                        ++i;
                        ++j;
                        continue;
                    }
                    if (++distance > 1)
                        break;
                    if (name.size() >= other.size())
                        ++i;
                    if (other.size() >= name.size())
                        ++j;
                }
                distance += name.size() - i + other.size() - j;
                if (distance == 1)
                    candidates.insert(other);
            }
            if (candidates.size() != 1)
                continue;
            auto options           = analysisOptions(doc->path, doc->text);
            const auto replacement = *candidates.begin();
            options.text.replace(ref->nameRange().start, ref->nameRange().length(), replacement);
            checkCancelled();
            if (!semantic::analyze(options)->main().compiled)
                continue;
            const auto edits = json::array(
                {json{{"range", range(file, ref->nameRange())}, {"newText", replacement}}});
            const auto edit =
                documentChanges_
                    ? json{{"documentChanges", json::array({json{{"textDocument", json{{"uri", doc->uri}, {"version", doc->version}}}, {"edits", edits}}})}}
                    : json{{"changes", json{{doc->uri, edits}}}};
            result.push_back(
                json{
                    {"title", "Replace with '" + replacement + "'"},
                    {"kind", "quickfix"},
                    {"isPreferred", true},
                    {"edit", edit}});
        }
    }
    return result;
}
} // namespace camel::ls
