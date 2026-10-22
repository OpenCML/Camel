// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/parse/syntax/dump.h"

#include <ostream>
#include <vector>

namespace camel::parse::syntax {

namespace {

std::string oneLine(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '\n') {
            out += "\\n";
        } else if (c != '\r') {
            out += c;
        }
    }
    return out;
}

void dumpChild(std::ostream &os, Child child, std::vector<bool> &open, bool last) {
    for (size_t i = 0; i + 1 < open.size(); ++i) {
        os << (open[i] ? "|  " : "   ");
    }
    if (!open.empty()) {
        os << (last ? "\\-" : "|-");
    }
    if (child.isToken()) {
        const TokenRef token = child.token();
        os << " " << (token.kind() == TokenKind::Eof ? "<EOF>" : oneLine(token.view())) << "\n";
        return;
    }
    const Node node = child.node();
    os << nodeKindName(node.kind()) << "\n";
    if (!open.empty()) {
        open.back() = !last;
    }
    open.push_back(true);
    const size_t n = node.childCount();
    for (size_t i = 0; i < n; ++i) {
        dumpChild(os, node.child(i), open, i + 1 == n);
    }
    open.pop_back();
}

} // namespace

void dumpTree(std::ostream &os, const Tree &tree) {
    const Node root = tree.root();
    if (!root) {
        return;
    }
    os << nodeKindName(root.kind()) << "\n";
    std::vector<bool> open{true};
    const size_t n = root.childCount();
    for (size_t i = 0; i < n; ++i) {
        dumpChild(os, root.child(i), open, i + 1 == n);
    }
}

void dumpTokens(std::ostream &os, const TokenBuffer &buffer) {
    for (size_t i = 0; i < buffer.size(); ++i) {
        const Token &token = buffer[i];
        if (token.kind == TokenKind::Eof) {
            break;
        }
        std::string index = std::to_string(i);
        std::string place =
            std::to_string(buffer.lineOf(token.begin) + 1) + ":" +
            std::to_string(buffer.columnOf(token.begin));
        os << std::string(index.size() < 4 ? 4 - index.size() : 0, ' ') << index << " ["
           << std::string(place.size() < 7 ? 7 - place.size() : 0, ' ') << place << "] "
           << (token.newlineBefore() ? 'n' : token.spaceBefore() ? 's' : '-') << " "
           << tokenKindName(token.kind)
           << std::string(
                  tokenKindName(token.kind).size() < 16 ? 16 - tokenKindName(token.kind).size()
                                                        : 1,
                  ' ')
           << ": " << oneLine(buffer.text(token)) << "\n";
    }
}

} // namespace camel::parse::syntax
