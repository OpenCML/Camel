// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * FNV-1a over explicitly little-endian words: the one mixing primitive of the kernel runtime.
 *
 * Two hashes of the kernel are built out of it -- the block's canonical form (block_form.cpp) and
 * the portable graph hash the fact store keeps (fact_persist.cpp) -- and they were two copies of
 * the same struct, which is one answer too many to "what are these bytes worth": a copy that gains
 * a method, or fixes a width, leaves the other behind and the two hashes stop agreeing about the
 * same bytes without either of them being wrong on its own.
 *
 * Private to the kernel runtime, and for that reason not under `camel/runtime/`: it is a way to
 * mix bytes, not a fact about a graph, so no other layer includes it and the two files above are
 * the whole list of readers.
 */

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "camel/runtime/graph.h"

namespace camel::runtime {

/// FNV-1a, 64 bits: the same value in every process and on every platform for the same bytes.
struct Fnv1a {
    uint64_t h = 0xcbf29ce484222325ull;
    void bytes(const void *data, size_t size) {
        const auto *p = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size; ++i) {
            h = (h ^ p[i]) * 0x100000001b3ull;
        }
    }
    /// A word, little-endian explicitly, so the value does not depend on the layout of the machine
    /// that reads it.
    void u64(uint64_t value) {
        unsigned char b[8];
        for (int i = 0; i < 8; ++i) {
            b[i] = static_cast<unsigned char>(value >> (8 * i));
        }
        bytes(b, 8);
    }
    /// Text with its size, so that "ab" and "b" beside an "a" are two inputs.
    void str(std::string_view text) {
        u64(text.size());
        bytes(text.data(), text.size());
    }
    /// A list of node refs with its size, in the order given: a port list is a list, not a set.
    void refs(std::span<const gc_node_ref_t> nodes) {
        u64(nodes.size());
        for (gc_node_ref_t node : nodes) {
            u64(node);
        }
    }
};

} // namespace camel::runtime