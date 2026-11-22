// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "code.h"

namespace camel::passes::cpp {

void printBlock(std::string &out, const Block &block, int indent) {
    const std::string pad(static_cast<size_t>(indent) * 4, ' ');
    for (const Stmt &stmt : block) {
        switch (stmt.kind) {
        case Stmt::Kind::Line:
            out += pad + stmt.text + "\n";
            break;
        case Stmt::Kind::Scope:
            out += pad + "{\n";
            printBlock(out, stmt.body, indent + 1);
            out += pad + "}\n";
            break;
        case Stmt::Kind::Loop:
            out += pad + "for (;;) {\n";
            printBlock(out, stmt.body, indent + 1);
            out += pad + "}\n";
            break;
        case Stmt::Kind::Branch:
            for (size_t i = 0; i < stmt.arms.size(); ++i) {
                const auto &[condition, body] = stmt.arms[i];
                if (i == 0) {
                    out += pad + "if (" + condition + ") {\n";
                } else if (!condition.empty()) {
                    out += pad + "} else if (" + condition + ") {\n";
                } else {
                    out += pad + "} else {\n";
                }
                printBlock(out, body, indent + 1);
            }
            out += pad + "}\n";
            break;
        }
    }
}

} // namespace camel::passes::cpp
