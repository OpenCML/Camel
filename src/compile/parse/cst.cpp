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
 * Created: Sep. 18, 2024
 * Updated: Mar. 17, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "cst.h"

using namespace std;

any CSTDumpVisitor::dumpNode(antlr4::tree::ParseTree *context, string nodeName) {
    bool isLast = false;
    auto children = context->children;

    if (visible.size() <= depth)
        visible.push_back(true);

    if (depth > 0) {
        auto siblings = context->parent->children;
        if (siblings[siblings.size() - 1] == context) {
            isLast = true;
            visible[depth - 1] = false;
        }
    }

    auto getHead = [this](bool last) -> string {
        size_t i = 0;
        string ret = "";
        while (depth > 0 && i < depth - 1) {
            if (visible[i])
                ret += "|  ";
            else
                ret += "   ";
            i++;
        }
        if (depth > 0) {
            if (last)
                ret += "\\-";
            else
                ret += "|-";
        }
        return ret;
    };

    os << getHead(isLast);
    os << nodeName;
    if (children.size() == 0) {
        string raw = context->getText();
        string text = regex_replace(raw, regex(R"(\n)"), "\\n");
        os << " " << text;
    }
    os << endl;

    if (depth > 0)
        for (size_t i = depth; i < visible.size(); i++)
            visible[i] = true;

    depth++;

    any result = defaultResult();
    size_t n = children.size();
    for (size_t i = 0; i < n; i++) {
        if (children[i]->getTreeType() == antlr4::tree::ParseTreeType::RULE) {
            any childResult = context->children[i]->accept(this);
            result = aggregateResult(std::move(result), std::move(childResult));
        } else {
            dumpNode(context->children[i], "");
        }
    }

    depth--;

    return result;
};
