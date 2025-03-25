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

export function generateCSTDumpVisitor(headerContent) {
    // 正则表达式匹配所有virtual visit函数声明
    const funcPattern = /virtual std::any visit(\w+?)\(OpenCMLParser::(\w+Context) \*context\) = 0;/g;
    
    // 提取所有匹配的函数并生成代码
    const visitFunctions = [];
    let match;
    
    while ((match = funcPattern.exec(headerContent)) !== null) {
        const funcName = match[1];      // 例如：Program
        const contextType = match[2];   // 例如：ProgramContext
        const code = `    std::any visit${funcName}(OpenCMLParser::${contextType} *context) { return dumpNode(context, "${funcName}"); };`;
        visitFunctions.push(code);
    }

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

// 示例用法：
// const fs = require('fs');
// const headerContent = fs.readFileSync('OpenCMLVisitor.h', 'utf-8');
// console.log(generateCSTDumpVisitor(headerContent));