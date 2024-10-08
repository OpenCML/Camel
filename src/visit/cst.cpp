/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Sep. 18, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "cst.h"

std::any CSTDumpVisitor::dumpCST(antlr4::tree::ParseTree *context, std::string nodeName) {
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

    auto getHead = [=](bool last) -> std::string {
        int i = 0;
        std::string ret = "";
        while (i < depth - 1) {
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

    std::cout << getHead(isLast);
    std::cout << nodeName;
    if (children.size() == 0) {
        std::string raw = context->getText();
        std::string text = std::regex_replace(raw, std::regex(R"(\n)"), "\\n");
        std::cout << " " << text;
    }
    std::cout << std::endl;

    if (depth > 0)
        for (int i = depth; i < visible.size(); i++)
            visible[i] = true;

    depth++;

    std::any result = defaultResult();
    size_t n = children.size();
    for (size_t i = 0; i < n; i++) {
        if (children[i]->getTreeType() == antlr4::tree::ParseTreeType::RULE) {
            std::any childResult = context->children[i]->accept(this);
            result = aggregateResult(std::move(result), std::move(childResult));
        } else {
            dumpCST(context->children[i], "");
        }
    }

    depth--;

    return result;
};