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
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * camel-ls: the language server.
 *
 * The server keeps the text of every open document and, per document, the semantic model of
 * its latest text (camel/service/semantic/model.h). A model is built by compiling the whole
 * text; there is no incremental compilation. So that typing is not held up by it:
 *
 *  - an edit only marks the document stale; the model is rebuilt when a request needs it, or
 *    when no further message has arrived for a short while (then diagnostics are published);
 *  - a request that was cancelled while it waited is answered without being computed.
 *
 * Every handler runs inside a catch-all: an exception becomes an error response, never the
 * end of the server.
 */

#pragma once

#include "protocol.h"

#include "camel/service/semantic/model.h"

#include <map>
#include <memory>
#include <string>

namespace camel::ls {

namespace semantic = camel::service::semantic;

class Server {
  public:
    Server(Transport &transport, std::string executableDir);
    /// Runs until `exit` or the end of the input. Returns the process exit code.
    int run();

  private:
    struct Document {
        std::string uri;
        std::string path;
        std::string text;
        json version = nullptr;
        bool stale     = true; // the text changed since `model` was built
        bool published = false; // diagnostics of `model` were pushed
        semantic::model_ptr_t model;
        /// The last model whose text compiled: where a text that does not compile takes the
        /// types of its declarations from.
        semantic::model_ptr_t lastCompiled;
    };

    Transport &transport_;
    std::string executableDir_;
    std::map<std::string, Document> documents_;
    std::vector<std::string> workspaceRoots_;
    bool shutdown_      = false;
    bool pushDiagnostics_ = true;
    int debounceMs_     = 150;
    /// Workspace files that are not open, parsed for workspace/symbol: path -> (stamp, model).
    std::map<std::string, std::pair<std::string, semantic::model_ptr_t>> workspaceCache_;

    void dispatch(const json &message);
    json handleRequest(const std::string &method, const json &params);
    void handleNotification(const std::string &method, const json &params);

    Document *document(const json &params);
    const semantic::SemanticModel &modelOf(Document &doc);
    void refreshStale();
    void publish(Document &doc);
    json diagnosticsOf(Document &doc);

    json position(const semantic::FileModel &file, uint32_t offset) const;
    json range(const semantic::FileModel &file, semantic::Range r) const;
    json location(Document &doc, const semantic::Location &loc);
    uint32_t offsetAt(Document &doc, const json &params);

    json initialize(const json &params);
    void didOpen(const json &params);
    void didChange(const json &params);
    void didClose(const json &params);
    json hover(const json &params);
    json definition(const json &params);
    json references(const json &params);
    json documentHighlight(const json &params);
    json documentSymbol(const json &params);
    json workspaceSymbol(const json &params);
    json completion(const json &params);
    json signatureHelp(const json &params);
    json prepareRename(const json &params);
    json rename(const json &params);
    json semanticTokens(const json &params);
    json formatting(const json &params);
    json graph(const json &params);
};

/// Thrown by a handler to answer a request with an error.
struct RequestError {
    int code;
    std::string message;
};

} // namespace camel::ls
