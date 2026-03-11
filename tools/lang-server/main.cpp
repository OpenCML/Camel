/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and
 * conditions of the MIT license. You may obtain a copy of
 * the MIT license at: [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT
 * WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Feb. 22, 2026
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 *
 */

// nlohmann/json 使用标准 C 的 EOF 宏；ANTLR 在 antlr4-common.h 里会 #undef EOF，
// 故须先包含 nlohmann/json.hpp（并保证 EOF 可见），再包含会拉入 ANTLR 的头文件。
#include "nlohmann/json.hpp"
#include <cstdio>

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/range.h"
#include "camel/core/mm.h"
#include "camel/parse/parse.h"
#include "camel/utils/dll_path.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

using namespace std;
using json   = nlohmann::json;
namespace mm = camel::core::mm;
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

// 读取 Content-Length 协议的消息
static bool readLspMessage(istream &in, string &out) {
    string header;
    int contentLength = -1;
    while (getline(in, header) && !header.empty()) {
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

// 解析 Camel 源码并发布诊断
static void parseAndPublishDiagnostics(ostream &out, const string &uri, const string &content) {
    string path      = uriToPath(uri);
    auto diagnostics = make_shared<Diagnostics>("lsp", path);
    diagnostics->setConfig(DiagsConfig{.total_limit = -1});

    istringstream iss(content);
    auto parser = make_shared<CamelParser>(diagnostics);
    try {
        parser->parse(iss);
    } catch (...) {
        // 解析失败时 diagnostics 已包含错误
    }

    // 将 TokenRange 转为 CharRange
    RangeConverter conv(parser->getTokens());
    diagnostics->fetchAll(parser->getTokens());

    json lspDiags = json::array();
    auto addDiag  = [&](const Diagnostic &d) {
        json diag;
        if (holds_alternative<CharRange>(d.range)) {
            CharRange r   = get<CharRange>(d.range);
            diag["range"] = {
                {"start", {{"line", r.start.line}, {"character", r.start.character}}},
                {"end", {{"line", r.end.line}, {"character", r.end.character}}}};
        } else {
            diag["range"] = {
                {"start", {{"line", 0}, {"character", 0}}},
                {"end", {{"line", 0}, {"character", 0}}}};
        }
        diag["severity"] = severityToLsp(d.severity);
        diag["source"]   = "Camel";
        diag["message"]  = d.message;
        if (!d.suggestion.empty())
            diag["code"] = d.suggestion;
        lspDiags.push_back(diag);
    };
    for (const Diagnostic *d : diagnostics->errors())
        addDiag(*d);
    for (const Diagnostic *d : diagnostics->warnings())
        addDiag(*d);
    for (const Diagnostic *d : diagnostics->infos())
        addDiag(*d);
    for (const Diagnostic *d : diagnostics->hints())
        addDiag(*d);

    json params;
    params["uri"]         = uri;
    params["diagnostics"] = lspDiags;
    sendNotification(out, "textDocument/publishDiagnostics", params);
}

int main(int argc, char *argv[]) {
    camel::utils::setupLibrarySearchPath();

    (void)mm::autoSpace();
    (void)mm::metaSpace();
    (void)mm::permSpace();

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
        json id       = msg.value("id", nullptr);
        json params   = msg.value("params", json::object());

        // ---- 生命周期 ----
        if (method == "initialize") {
            json result;
            result["capabilities"] = {
                {"textDocumentSync",
                 {{"openClose", true}, {"change", 1}, {"save", json::object()}}},
                {"diagnosticProvider",
                 {{"interFileDependencies", false}, {"workspaceDiagnostics", false}}}};
            result["serverInfo"] = {{"name", "camel-ls"}, {"version", "0.1.0"}};
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
            parseAndPublishDiagnostics(out, uri, content);
            continue;
        }
        if (method == "textDocument/didChange") {
            string uri    = params["textDocument"]["uri"];
            auto &changes = params["contentChanges"];
            if (!changes.empty() && changes[0].contains("text")) {
                openDocuments[uri] = changes[0]["text"];
                parseAndPublishDiagnostics(out, uri, openDocuments[uri]);
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
