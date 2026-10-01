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
 * Created: Feb. 22, 2026
 * Updated: Oct. 01, 2026
 * Supported by: National Key Research and Development Program of China
 *
 */

#include "camel/utils/windows_parser_guard.h"
// nlohmann/json 使用标准 C 的 EOF 宏；ANTLR 在 antlr4-common.h 里会 #undef EOF，
// 故须先包含 nlohmann/json.hpp（并保证 EOF 可见），再包含会拉入 ANTLR 的头文件。
#include "nlohmann/json.hpp"
#include <cstdio>

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/range.h"
#include "camel/core/mm.h"
#include "camel/core/module/userdef.h"
#include "camel/init.h"
#include "camel/parse/parse.h"
#include "camel/utils/install_layout.h"

#include "../format/fmt.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>

using namespace std;
using json   = nlohmann::json;
namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::error;
using namespace camel::parse;

namespace fs = std::filesystem;

// LSP 诊断严重程度: Error=1, Warning=2, Information=3, Hint=4
static int severityToLsp(Severity s) {
    switch (s) {
    case Severity::Error:
        return 1;
    case Severity::Warn:
        return 2;
    case Severity::Info:
        return 3;
    case Severity::Hint:
        return 4;
    default:
        return 1;
    }
}

static json positionJson(size_t line, size_t character) {
    json pos         = json::object();
    pos["line"]      = line;
    pos["character"] = character;
    return pos;
}

static json rangeJson(const json &start, const json &end) {
    json r     = json::object();
    r["start"] = start;
    r["end"]   = end;
    return r;
}

// 读取 Content-Length 协议的消息
static bool readLspMessage(istream &in, string &out) {
    string header;
    int contentLength = -1;
    while (getline(in, header)) {
        // binary mode keeps the CR of CRLF line endings; strip it
        if (!header.empty() && header.back() == '\r')
            header.pop_back();
        if (header.empty())
            break;
        if (header.compare(0, 16, "Content-Length: ") == 0) {
            contentLength = stoi(header.substr(16));
        }
    }
    if (contentLength <= 0)
        return false;
    out.resize(static_cast<size_t>(contentLength));
    in.read(&out[0], contentLength);
    return in.good();
}

// 发送 LSP 消息
static void sendLspMessage(ostream &out, const json &msg) {
    string body = msg.dump();
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    out.flush();
}

// 发送 JSON-RPC 响应
static void sendResponse(ostream &out, const json &id, const json &result, const json &error) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"]      = id;
    if (!error.is_null())
        resp["error"] = error;
    else
        resp["result"] = result;
    sendLspMessage(out, resp);
}

// 发送 JSON-RPC 通知
static void sendNotification(ostream &out, const string &method, const json &params) {
    json notif;
    notif["jsonrpc"] = "2.0";
    notif["method"]  = method;
    notif["params"]  = params;
    sendLspMessage(out, notif);
}

// URI 转为本地路径 (file:///...)
static string uriToPath(const string &uri) {
    if (uri.compare(0, 7, "file://") != 0)
        return uri;
    string path = uri.substr(7);
#ifdef _WIN32
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':')
        path = path.substr(1); // /C:/... -> C:/...
#endif
    for (char &c : path) {
        if (c == '/')
            c = fs::path::preferred_separator;
    }
    return path;
}

// 解析并编译 Camel 源码（与 `camel check` 相同的管线），返回 parser 与所有
// 用户模块的 LSP 诊断数组（语法 + 语义：未解析引用、类型错误等）。
static json buildDiagnosticsArray(const string &uri, const string &content) {
    string path      = uriToPath(uri);
    auto diagnostics = make_shared<Diagnostics>("lsp", path);
    diagnostics->setConfig(DiagsConfig{.total_limit = -1});

    // 语义诊断所需的编译上下文（与 camel-cli check 分支一致）
    std::string entryDir = fs::absolute(fs::path(path)).parent_path().string();
    auto searchPaths     = camel::utils::buildModuleSearchPaths(
        entryDir,
        camel::utils::ModuleSearchPathOptions{.stdlibOverride = ""});
    auto ctx = Context::create(
        EntryConfig{
            .entryDir    = entryDir,
            .entryFile   = path,
            .searchPaths = std::move(searchPaths),
        },
        DiagsConfig{
            .total_limit         = -1,
            .per_severity_limits = {{Severity::Error, 0}},
        });

    istringstream iss(content);
    auto parser     = make_shared<CamelParser>(diagnostics);
    auto mainModule = make_shared<UserDefinedModule>("main", path, ctx, parser);
    ctx->setMainModule(mainModule);
    try {
        parser->parse(iss);
    } catch (...) {
        // 解析失败时 diagnostics 已包含错误
    }
    try {
        mainModule->compile(CompileStage::Done);
    } catch (...) {
        // 编译失败时模块 diagnostics 已包含错误
    }

    json lspDiags = json::array();
    std::set<std::string> seen;
    auto addDiag = [&](const Diagnostic &d) {
        json diag = json::object();
        if (holds_alternative<CharRange>(d.range)) {
            CharRange r   = get<CharRange>(d.range);
            diag["range"] = rangeJson(
                positionJson(r.start.line, r.start.character),
                positionJson(r.end.line, r.end.character));
        } else {
            diag["range"] = rangeJson(positionJson(0, 0), positionJson(0, 0));
        }
        diag["severity"] = severityToLsp(d.severity);
        diag["source"]   = "Camel";
        diag["message"]  = d.message;
        if (!d.suggestion.empty())
            diag["code"] = d.suggestion;
        // parser 与模块诊断可能重复（共享编译产物），按键去重
        const json &r   = diag["range"];
        std::string key = std::to_string(r["start"]["line"].get<int>()) + ":" +
                          std::to_string(r["start"]["character"].get<int>()) + "-" +
                          std::to_string(r["end"]["line"].get<int>()) + ":" +
                          std::to_string(r["end"]["character"].get<int>()) + "|" +
                          std::to_string(diag["severity"].get<int>()) + "|" + d.message;
        if (seen.insert(key).second)
            lspDiags.push_back(diag);
    };
    auto collectFrom = [&](const Diagnostics &diags) {
        for (const Diagnostic *d : diags.errors())
            addDiag(*d);
        for (const Diagnostic *d : diags.warnings())
            addDiag(*d);
        for (const Diagnostic *d : diags.infos())
            addDiag(*d);
        for (const Diagnostic *d : diags.hints())
            addDiag(*d);
    };

    // parser 诊断：TokenRange -> CharRange
    RangeConverter conv(parser->getTokens());
    diagnostics->fetchAll(parser->getTokens());
    collectFrom(*diagnostics);

    // 各模块（含 main）的语义诊断
    for (const auto &mod : ctx->allUserModules()) {
        auto ud = std::dynamic_pointer_cast<UserDefinedModule>(mod);
        if (!ud || !ud->diagnostics())
            continue;
        ud->diagnostics()->fetchAll(ud->parser()->getTokens());
        collectFrom(*ud->diagnostics());
    }

    return lspDiags;
}

// 用 Formatter 格式化源码；解析失败时返回空 optional。
static std::optional<std::string>
formatSource(const std::string &path, const std::string &content) {
    auto diagnostics = std::make_shared<Diagnostics>("lsp-format", path);
    auto parser      = std::make_shared<CamelParser>(diagnostics);
    istringstream iss(content);
    try {
        if (!parser->parseCST(iss)) {
            return std::nullopt;
        }
    } catch (...) {
        return std::nullopt;
    }

    Formatter::Options options;
    Formatter formatter(parser->getTokens(), options);
    try {
        return std::any_cast<std::string>(formatter.visit(parser->cst()));
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

// 计算文档末位位置（全量替换 edit 的 range 终点）
static json documentEndPosition(const string &content) {
    size_t line   = 0;
    size_t lastNl = string::npos;
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            ++line;
            lastNl = i;
        }
    }
    size_t character = content.size() - (lastNl == string::npos ? 0 : lastNl + 1);
    return positionJson(line, character);
}

int main(int argc, char *argv[]) {
    camel::ScopedRuntime camelRuntime;

#ifdef _WIN32
    // LSP framing requires exact CRLF bytes; Windows text mode would translate
    // them and corrupt the protocol on both directions.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    (void)mm::autoSpace();
    (void)mm::metaSpace();
    (void)mm::permSpace();

    // 目前唯一传输方式是 stdio；显式接受 --stdio 以便调用方稳定 spawn。
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--stdio") {
            continue;
        }
        cerr << "camel-ls: ignoring unknown argument: " << arg << "\n";
    }

    bool shutdownReceived = false;
    map<string, string> openDocuments;

    istream &in  = cin;
    ostream &out = cout;

    while (true) {
        string raw;
        if (!readLspMessage(in, raw))
            break;

        json msg;
        try {
            msg = json::parse(raw);
        } catch (...) {
            continue;
        }

        string method = msg.value("method", "");
        json id       = msg.contains("id") ? msg["id"] : json(nullptr);
        json params   = msg.contains("params") ? msg["params"] : json::object();

        // ---- 生命周期 ----
        if (method == "initialize") {
            json textDocumentSync         = json::object();
            textDocumentSync["openClose"] = true;
            textDocumentSync["change"]    = 1;
            textDocumentSync["save"]      = json::object();

            json diagnosticProvider                     = json::object();
            diagnosticProvider["interFileDependencies"] = false;
            diagnosticProvider["workspaceDiagnostics"]  = false;

            json capabilities                          = json::object();
            capabilities["textDocumentSync"]           = textDocumentSync;
            capabilities["documentFormattingProvider"] = true;
            capabilities["diagnosticProvider"]         = diagnosticProvider;

            json serverInfo       = json::object();
            serverInfo["name"]    = "camel-ls";
            serverInfo["version"] = "0.2.0";

            json result            = json::object();
            result["capabilities"] = capabilities;
            result["serverInfo"]   = serverInfo;
            sendResponse(out, id, result, nullptr);
            continue;
        }
        if (method == "initialized") {
            continue;
        }
        if (method == "shutdown") {
            shutdownReceived = true;
            sendResponse(out, id, nullptr, nullptr);
            continue;
        }
        if (method == "exit") {
            break;
        }

        // ---- 文档同步 ----
        if (method == "textDocument/didOpen") {
            string uri         = params["textDocument"]["uri"];
            string content     = params["textDocument"]["text"];
            openDocuments[uri] = content;
            json diags         = buildDiagnosticsArray(uri, content);
            json outParams;
            outParams["uri"]         = uri;
            outParams["diagnostics"] = diags;
            sendNotification(out, "textDocument/publishDiagnostics", outParams);
            continue;
        }
        if (method == "textDocument/didChange") {
            string uri    = params["textDocument"]["uri"];
            auto &changes = params["contentChanges"];
            if (!changes.empty() && changes[0].contains("text")) {
                openDocuments[uri] = changes[0]["text"];
                json diags         = buildDiagnosticsArray(uri, openDocuments[uri]);
                json outParams;
                outParams["uri"]         = uri;
                outParams["diagnostics"] = diags;
                sendNotification(out, "textDocument/publishDiagnostics", outParams);
            }
            continue;
        }
        if (method == "textDocument/didClose") {
            string uri = params["textDocument"]["uri"];
            openDocuments.erase(uri);
            json paramsOut;
            paramsOut["uri"]         = uri;
            paramsOut["diagnostics"] = json::array();
            sendNotification(out, "textDocument/publishDiagnostics", paramsOut);
            continue;
        }

        // ---- Pull 诊断 ----
        if (method == "textDocument/diagnostic") {
            string uri = params["textDocument"]["uri"];
            json items;
            auto it = openDocuments.find(uri);
            if (it != openDocuments.end()) {
                items = buildDiagnosticsArray(uri, it->second);
            } else {
                items = json::array();
            }
            json report;
            report["kind"]  = "full";
            report["items"] = items;
            sendResponse(out, id, report, nullptr);
            continue;
        }

        // ---- 格式化 ----
        if (method == "textDocument/formatting") {
            string uri = params["textDocument"]["uri"];
            json edits = json::array();
            auto it    = openDocuments.find(uri);
            if (it != openDocuments.end()) {
                auto formatted = formatSource(uriToPath(uri), it->second);
                if (formatted.has_value()) {
                    json edit     = json::object();
                    edit["range"] = rangeJson(positionJson(0, 0), documentEndPosition(it->second));
                    edit["newText"] = *formatted;
                    edits.push_back(edit);
                }
            }
            sendResponse(out, id, edits, nullptr);
            continue;
        }

        // 未知方法：若是请求则回复方法未找到
        if (!id.is_null()) {
            json err;
            err["code"]    = -32601;
            err["message"] = "Method not found: " + method;
            sendResponse(out, id, nullptr, err);
        }
    }

    return shutdownReceived ? 0 : 1;
}
