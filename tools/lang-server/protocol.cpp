// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "protocol.h"

#include <chrono>
#include <filesystem>

namespace camel::ls {

namespace fs = std::filesystem;

std::string uriToPath(const std::string &uri) {
    if (uri.compare(0, 7, "file://") != 0) {
        return uri;
    }
    // Percent-decoding first: `file:///c%3A/dir/a%20b.cml`.
    std::string path;
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    for (size_t i = 7; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size() && hex(uri[i + 1]) >= 0 && hex(uri[i + 2]) >= 0) {
            path += static_cast<char>(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
            i    += 2;
        } else {
            path += uri[i];
        }
    }
#ifdef _WIN32
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':') {
        path = path.substr(1); // /C:/... -> C:/...
    }
#endif
    return fs::path(path).lexically_normal().generic_string();
}

std::string pathToUri(const std::string &path) {
    if (path.compare(0, 7, "file://") == 0) {
        return path;
    }
    std::error_code ec;
    fs::path absolute = fs::absolute(fs::path(path), ec);
    std::string p     = (ec ? fs::path(path) : absolute).lexically_normal().generic_string();
    std::string uri   = "file://";
    if (p.empty() || p[0] != '/') {
        uri += '/'; // C:/... -> file:///C:/...
    }
    static const char *hex = "0123456789ABCDEF";
    for (unsigned char c : p) {
        if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') {
            uri += static_cast<char>(c);
        } else {
            uri += '%';
            uri += hex[c >> 4];
            uri += hex[c & 0xF];
        }
    }
    return uri;
}

Transport::Transport(std::istream &in, std::ostream &out) : in_(in), out_(out) {
    reader_ = std::thread([this] { readLoop(); });
}

Transport::~Transport() {
    // The reader may be blocked in a read of an input that never ends; the process is about
    // to exit, so it is left to the operating system.
    if (reader_.joinable()) {
        reader_.detach();
    }
}

// One frame: headers up to an empty line, then `Content-Length` bytes. False at the end of
// the input; a frame without a usable length is skipped by reading on to the next header.
bool Transport::readFrame(std::string &body) {
    for (;;) {
        std::string header;
        long long length = -1;
        bool sawHeader   = false;
        while (std::getline(in_, header)) {
            if (!header.empty() && header.back() == '\r') {
                header.pop_back();
            }
            if (header.empty()) {
                if (sawHeader) {
                    break;
                }
                continue; // blank lines between frames
            }
            sawHeader = true;
            if (header.size() > 16 && header.compare(0, 15, "Content-Length:") == 0) {
                try {
                    length = std::stoll(header.substr(15));
                } catch (...) {
                    length = -1;
                }
            }
        }
        if (!in_.good()) {
            return false;
        }
        // A length nobody would send: refuse it instead of allocating it.
        if (length < 0 || length > (1ll << 30)) {
            continue;
        }
        body.resize(static_cast<size_t>(length));
        in_.read(body.data(), length);
        return in_.gcount() == length;
    }
}

void Transport::readLoop() {
    std::string raw;
    while (readFrame(raw)) {
        Message message;
        try {
            message.body = json::parse(raw);
        } catch (...) {
            message.malformed = true;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!message.malformed && message.body.is_object() && message.body.contains("method") &&
            message.body["method"].is_string() &&
            message.body["method"].get<std::string>() == "$/cancelRequest" &&
            message.body.contains("params") && message.body["params"].is_object() &&
            message.body.contains("params") && message.body["params"].contains("id")) {
            cancelled_.insert(message.body["params"]["id"].dump());
            continue;
        }
        queue_.push_back(std::move(message));
        arrived_.notify_one();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ended_ = true;
    arrived_.notify_one();
}

std::optional<Transport::Message> Transport::next() {
    std::unique_lock<std::mutex> lock(mutex_);
    arrived_.wait(lock, [this] { return !queue_.empty() || ended_; });
    if (queue_.empty()) {
        return std::nullopt;
    }
    Message message = std::move(queue_.front());
    queue_.pop_front();
    return message;
}

std::optional<Transport::Message> Transport::next(int milliseconds, bool &ended) {
    std::unique_lock<std::mutex> lock(mutex_);
    arrived_.wait_for(lock, std::chrono::milliseconds(milliseconds), [this] {
        return !queue_.empty() || ended_;
    });
    ended = queue_.empty() && ended_;
    if (queue_.empty()) {
        return std::nullopt;
    }
    Message message = std::move(queue_.front());
    queue_.pop_front();
    return message;
}

bool Transport::takeCancelled(const json &id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return cancelled_.erase(id.dump()) > 0;
}

void Transport::send(const json &message) {
    // Text that is not UTF-8 (a source file in another encoding quoted in a message) is
    // written with replacement characters instead of failing the whole response.
    const std::string body = message.dump(-1, ' ', false, json::error_handler_t::replace);
    std::lock_guard<std::mutex> lock(writeMutex_);
    out_ << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    out_.flush();
}

void Transport::respond(const json &id, const json &result) {
    send(json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

void Transport::respondError(const json &id, int code, const std::string &message) {
    send(
        json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", json{{"code", code}, {"message", message}}}});
}

void Transport::notify(const std::string &method, const json &params) {
    send(json{{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
}

} // namespace camel::ls
