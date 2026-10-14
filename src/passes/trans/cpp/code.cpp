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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
