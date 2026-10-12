/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
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
 * The normalized text of an abstract syntax tree, for comparing two parsers.
 *
 * One line per node: its load (kind and payload), its token range, and its origin with the
 * byte span, the parent origin and the label. The semantic bundle registered for the node is
 * written under it, part by part. Two parsers that build the same tree, give every node the
 * same place and create their origins in the same order produce the same text; the diagnostics
 * raised while the tree was built are appended.
 */

#pragma once

#include "camel/core/error/diagnostics.h"
#include "camel/core/source/manager.h"
#include "camel/parse/ast.h"

#include <ostream>
#include <sstream>
#include <string>

namespace parsecheck {

inline void dumpOrigin(
    std::ostream &os, const camel::source::source_context_ptr_t &sc,
    camel::source::origin_id_t id) {
    if (!sc || id == camel::source::kInvalidOriginId) {
        os << "-";
        return;
    }
    const auto *rec = sc->origin(id);
    if (!rec) {
        os << "?";
        return;
    }
    // Not the id, which is only a handle: what an origin says is its span, its label and where
    // it derives from.
    if (const auto *span = sc->span(rec->primarySpan)) {
        os << "[" << span->startOffset << "," << span->endOffset << ")";
    }
    if (!rec->label.empty()) {
        os << ":" << rec->label;
    }
    if (rec->parent != camel::source::kInvalidOriginId) {
        os << "<";
        dumpOrigin(os, sc, rec->parent);
    }
}

/// `positions`: with token ranges, origins and semantic bundles; without, the structure only
/// (what must survive reformatting).
inline void dumpAstNode(
    std::ostream &os, const AST::node_ptr_t &node, const camel::source::source_context_ptr_t &sc,
    int depth, bool positions = true) {
    for (int i = 0; i < depth; ++i) {
        os << ' ';
    }
    if (!node) {
        os << "<null>\n";
        return;
    }
    const auto load = node->load();
    std::string text = load->toString();
    for (char &c : text) {
        if (c == '\n') {
            c = ' ';
        }
    }
    os << text;
    if (positions) {
        os << " @" << load->tokenRange().start << ".." << load->tokenRange().end << " ";
        dumpOrigin(os, sc, load->origin());
    }
    if (positions && sc && load->origin() != camel::source::kInvalidOriginId) {
        if (const auto *bundle = sc->astSemantic(load->origin())) {
            os << " {";
            for (const auto &part : bundle->parts) {
                os << static_cast<int>(part.role) << "/" << part.slot << "/" << part.label << "/";
                dumpOrigin(os, sc, part.origin);
                os << ";";
            }
            os << "}";
        }
    }
    os << "\n";
    for (const auto &child : *node) {
        dumpAstNode(os, child, sc, depth + 1, positions);
    }
}

/// The whole result of a parse as text: "ACCEPT" and the tree, or "REJECT"; then the
/// diagnostics (code, range, message).
inline std::string dumpParse(
    bool accepted, const AST::node_ptr_t &ast, const camel::source::source_context_ptr_t &sc,
    const std::string &diagnostics) {
    std::ostringstream os;
    os << (accepted ? "ACCEPT\n" : "REJECT\n");
    if (accepted && ast) {
        dumpAstNode(os, ast, sc, 0);
    }
    if (!diagnostics.empty()) {
        os << "DIAGNOSTICS " << diagnostics << "\n";
    }
    return os.str();
}

} // namespace parsecheck
