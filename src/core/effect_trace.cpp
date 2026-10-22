// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Effect trace (see effect_trace.h).
 */

#include "camel/core/effect_trace.h"

#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/source/manager.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace camel::core::effect_trace {

namespace {

// ---------------------------------------------------------------------------------------------
// SHA-1 (FIPS 180-1) of a byte string, as lowercase hex. Used only to fingerprint payloads.
// ---------------------------------------------------------------------------------------------
std::string sha1Hex(std::string_view data) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::string msg(data);
    const uint64_t bitLen = static_cast<uint64_t>(data.size()) * 8;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) {
        msg.push_back('\0');
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<char>((bitLen >> (i * 8)) & 0xFF));
    }
    auto rol = [](uint32_t v, int s) { return (v << s) | (v >> (32 - s)); };
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto *p = reinterpret_cast<const unsigned char *>(msg.data() + off + i * 4);
            w[i]          = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
                            uint32_t(p[3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e                = d;
            d                = c;
            c                = rol(b, 30);
            b                = a;
            a                = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    static const char *digits = "0123456789abcdef";
    std::string out;
    out.reserve(40);
    for (uint32_t v : h) {
        for (int s = 28; s >= 0; s -= 4) {
            out.push_back(digits[(v >> s) & 0xF]);
        }
    }
    return out;
}

std::string jsonEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    static const char *digits = "0123456789abcdef";
    for (const char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += "\\u00";
                out.push_back(digits[(c >> 4) & 0xF]);
                out.push_back(digits[c & 0xF]);
            } else {
                out.push_back(c);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Process state
// ---------------------------------------------------------------------------------------------
// The file the trace is appended to, set by std::profile(effect_trace="..."). Replaced before
// the run starts, so every reader sees one immutable string; the previous one is kept alive.
std::atomic<const std::string *> traceFile{nullptr};

const std::string *tracePath() { return traceFile.load(std::memory_order_acquire); }

thread_local uint64_t tlsSiteOrigin = 0;
thread_local int tlsMacroDepth      = 0;

struct Sink {
    std::mutex mutex;
    std::FILE *file = nullptr;
    bool failed     = false;
    uint64_t seq    = 0;
};

Sink &sink() {
    static Sink s;
    return s;
}

/// One wrapped operator. Slots are handed out once and never reused, so a trampoline pointer
/// stays valid for the life of the process.
struct Slot {
    operator_t op = nullptr;
    std::string uri;
};

constexpr size_t kMaxWrapped = 512;

struct Registry {
    std::mutex mutex;
    std::array<Slot, kMaxWrapped> slots;
    size_t used = 0;
    std::unordered_map<std::string, size_t> byUri;
};

Registry &registry() {
    static Registry r;
    return r;
}

void appendArgs(std::ostringstream &os, ArgsView &args) {
    for (size_t i = 0; i < args.size(); ++i) {
        type::Type *t = args.type(i);
        const slot_t value = args.slot(i);
        if (t == nullptr) {
            os << '?';
        } else if (value == DeadSlot) {
            os << t->toString() << "=<uninitialized>";
        } else {
            // Values are printed by content (the way println shows them), never by address,
            // so equal arguments hash equally across runs and back ends.
            os << t->toString() << '=';
            rtdata::printSlot(os, value, t);
        }
        os << '\x1f';
    }
}

/// Upper bound, in bytes, of the payload text written to a trace line.
constexpr size_t kMaxPayloadBytes = 2048;

/// The payload as written to the trace: the hashed text with the unit separator (0x1f, after
/// each argument) shown as `|` and the record separator (0x1e, between the normal and the
/// `with` arguments) as `||`, cut to kMaxPayloadBytes. The cut backs off to a UTF-8 character
/// boundary so a multi-byte character is never split; `truncated` reports whether anything
/// was dropped.
std::string payloadText(std::string_view raw, bool &truncated) {
    std::string text;
    text.reserve(raw.size() + 1);
    for (const char c : raw) {
        if (c == '\x1f') {
            text += '|';
        } else if (c == '\x1e') {
            text += "||";
        } else {
            text.push_back(c);
        }
    }
    truncated = text.size() > kMaxPayloadBytes;
    if (truncated) {
        size_t cut = kMaxPayloadBytes;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        text.resize(cut);
    }
    return text;
}

slot_t invoke(size_t index, ArgsView &with, ArgsView &norm, context::Context &ctx) {
    const Slot &slot = registry().slots[index];

    // The payload is taken before the call: operators may write through `var` arguments.
    std::ostringstream payload;
    appendArgs(payload, norm);
    payload << '\x1e';
    appendArgs(payload, with);

    const uint64_t origin = std::exchange(tlsSiteOrigin, 0);
    std::string originJson = "null";
    if (origin != 0) {
        if (auto sc = ctx.sourceContext()) {
            const auto *rec = sc->origin(origin);
            if (rec && rec->primarySpan != camel::source::kInvalidSpanId) {
                const auto range = sc->resolveOrigin(origin);
                originJson       = "{\"file\":\"" + jsonEscape(sc->pathForOrigin(origin)) +
                                   "\",\"line\":" + std::to_string(range.start.line + 1) +
                                   ",\"col\":" + std::to_string(range.start.character + 1) + "}";
            }
        }
    }

    {
        Sink &s = sink();
        std::lock_guard lock(s.mutex);
        if (!s.file && !s.failed) {
            const std::string *path = tracePath();
            s.file                  = path != nullptr ? std::fopen(path->c_str(), "wb") : nullptr;
            s.failed                = (s.file == nullptr);
            if (s.failed) {
                std::fprintf(
                    stderr,
                    "[camel] std::profile(effect_trace): cannot open '%s' for writing\n",
                    path != nullptr ? path->c_str() : "(no file)");
            }
        }
        if (s.file) {
            // The hash covers the full payload; the text written next to it may be cut.
            const std::string raw  = payload.str();
            bool truncated         = false;
            const std::string text = payloadText(raw, truncated);
            const std::string line =
                "{\"seq\":" + std::to_string(s.seq++) + ",\"uri\":\"" + jsonEscape(slot.uri) +
                "\",\"stage\":\"" + (tlsMacroDepth > 0 ? "macro" : "run") +
                "\",\"origin\":" + originJson + ",\"payload_sha1\":\"" + sha1Hex(raw) +
                "\",\"payload\":\"" + jsonEscape(text) + "\"" +
                (truncated ? ",\"payload_truncated\":true" : "") + "}\n";
            std::fwrite(line.data(), 1, line.size(), s.file);
            std::fflush(s.file);
        }
    }

    return slot.op(with, norm, ctx);
}

template <size_t I> slot_t trampoline(ArgsView &with, ArgsView &norm, context::Context &ctx) {
    return invoke(I, with, norm, ctx);
}

template <size_t... I>
constexpr std::array<operator_t, sizeof...(I)> makeTrampolines(std::index_sequence<I...>) {
    return {&trampoline<I>...};
}

constexpr std::array<operator_t, kMaxWrapped> kTrampolines =
    makeTrampolines(std::make_index_sequence<kMaxWrapped>{});

} // namespace

void setTraceFile(std::string path) {
    const std::string *fresh = path.empty() ? nullptr : new std::string(std::move(path));
    traceFile.store(fresh, std::memory_order_release);
}

bool enabled() { return tracePath() != nullptr; }

operator_t wrap(std::string_view uri, operator_t op) {
    if (!enabled() || op == nullptr || !OperatorTable::instance().hasExternalEffect(uri)) {
        return op;
    }
    Registry &r = registry();
    std::lock_guard lock(r.mutex);
    const std::string key(uri);
    if (auto it = r.byUri.find(key); it != r.byUri.end()) {
        return kTrampolines[it->second];
    }
    if (r.used == kMaxWrapped) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::fprintf(
                stderr,
                "[camel] std::profile(effect_trace): more than %zu effectful operators; '%s' and later "
                "ones are not traced\n",
                kMaxWrapped,
                key.c_str());
        }
        return op;
    }
    const size_t index = r.used++;
    r.slots[index]     = Slot{op, key};
    r.byUri.emplace(key, index);
    return kTrampolines[index];
}

operator_t unwrap(operator_t op) {
    if (!enabled()) {
        return op;
    }
    Registry &r = registry();
    std::lock_guard lock(r.mutex);
    for (size_t i = 0; i < r.used; ++i) {
        if (kTrampolines[i] == op) {
            return r.slots[i].op;
        }
    }
    return op;
}

void setSite(uint64_t origin) { tlsSiteOrigin = origin; }

MacroStageScope::MacroStageScope() { ++tlsMacroDepth; }
MacroStageScope::~MacroStageScope() { --tlsMacroDepth; }

} // namespace camel::core::effect_trace
