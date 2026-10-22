// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The transport of camel-ls: JSON-RPC messages framed with `Content-Length` on the standard
 * streams, read by a thread of its own into a queue, and the conversion between file URIs and
 * paths.
 *
 * The reader thread exists so that the server can see what is waiting while it works: a burst
 * of edits is compiled once, and a request that was cancelled while it waited is not computed.
 */

#pragma once

// nlohmann/json uses the C macro EOF, which the ANTLR headers undefine: include it first.
#include "nlohmann/json.hpp"

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <thread>

namespace camel::ls {

using json = nlohmann::json;

/// JSON-RPC and LSP error codes.
enum ErrorCode : int {
    ParseError       = -32700,
    InvalidRequest   = -32600,
    MethodNotFound   = -32601,
    InvalidParams    = -32602,
    InternalError    = -32603,
    RequestFailed    = -32803,
    RequestCancelled = -32800,
};

std::string uriToPath(const std::string &uri);
std::string pathToUri(const std::string &path);

class Transport {
  public:
    Transport(std::istream &in, std::ostream &out);
    ~Transport();

    struct Message {
        json body;
        bool malformed = false; // a frame whose content is not JSON
    };

    /// The next message; waits for one. Empty when the input has ended.
    std::optional<Message> next();
    /// The next message if one arrives within `milliseconds`; `ended` tells an input that
    /// has ended from one that is merely quiet.
    std::optional<Message> next(int milliseconds, bool &ended);
    /// Whether a `$/cancelRequest` for this id has been read (and forgets it).
    bool takeCancelled(const json &id);

    void send(const json &message);
    void respond(const json &id, const json &result);
    void respondError(const json &id, int code, const std::string &message);
    void notify(const std::string &method, const json &params);

  private:
    std::istream &in_;
    std::ostream &out_;
    std::thread reader_;
    std::mutex mutex_;
    std::condition_variable arrived_;
    std::deque<Message> queue_;
    std::set<std::string> cancelled_;
    bool ended_ = false;
    std::mutex writeMutex_;

    void readLoop();
    bool readFrame(std::string &body);
};

} // namespace camel::ls
