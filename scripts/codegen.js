function formatDate(date) {
    const months = [
        'Jan.',
        'Feb.',
        'Mar.',
        'Apr.',
        'May.',
        'Jun.',
        'Jul.',
        'Aug.',
        'Sep.',
        'Oct.',
        'Nov.',
        'Dec.'
    ]
    return `${months[date.getMonth()]} ${date
        .getDate()
        .toString()
        .padStart(2, '0')}, ${date.getFullYear()}`
}

export function extractDecls(headerContent) {
    const funcPattern = /virtual std::any visit(\w+?)\(OpenCMLParser::(\w+Context) \*context\) = 0;/g;
    
    const decls = [];
    let match;
    
    while ((match = funcPattern.exec(headerContent)) !== null) {
        const funcName = match[1];      // 例如：Program
        const contextType = match[2];   // 例如：ProgramContext
        decls.push({ funcName, contextType });
    }

    return decls;
}

export function generateCSTDumpVisitor(headerContent, decls) {
    const visitFunctions = decls.map(({ funcName, contextType }) => {
        return `    std::any visit${funcName}(OpenCMLParser::${contextType} *context) { return dumpNode(context, "${funcName}"); };`;
    });

    // 组装完整的类定义
    return `/**
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
 * Created: Mar. 17, 2024
 * Updated: ${formatDate(new Date())}
 * Supported by: National Key Research and Development Program of China
 */

#include <iostream>
#include <regex>
#include <string>

#include "antlr/OpenCMLVisitor.h"
#include "antlr4-runtime/antlr4-runtime.h"

class CSTDumpVisitor : public OpenCMLVisitor {
    size_t depth = 0;
    std::ostream &os = std::cout;
    std::vector<bool> visible;

  public:
    CSTDumpVisitor(std::ostream &os) : os(os) {}

    std::any dumpNode(antlr4::tree::ParseTree *context, std::string nodeName);

${visitFunctions.join('\n\n')}
};
`;
}

export function transformFormatterCode(originalCode, decls) {
    const visitDecls = decls.map(({ funcName, contextType }) => {
        return `    std::any visit${funcName}(OpenCMLParser::${contextType} *context);`;
    });

    const segIndex = originalCode.indexOf('public:');
    const header = originalCode.substring(0, segIndex);

    const tail = `public:
    Formatter(const std::vector<antlr4::Token *> tokens) : tokens(tokens) {}

${visitDecls.join('\n\n')}
};
`;

    return header + tail;
}