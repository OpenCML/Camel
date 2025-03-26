import { logFail } from './common.js'

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

export function parseGrammarRules(grammarText) {
    const ruleMap = new Map()
    const rulePattern = /([a-zA-Z_]+)(\s*:\s*(?:.(?!\s*;))*.?\s*;)/gs

    const segIndex = grammarText.indexOf('program')
    grammarText = grammarText.substring(segIndex)

    let match
    while ((match = rulePattern.exec(grammarText)) !== null) {
        const ruleName = match[1]
        let ruleBody = ruleName + match[2]
        ruleMap.set(ruleName, ruleBody)
    }

    return ruleMap
}

export function generateCSTDumpVisitor(rules) {
    const visitFunctions = Array.from(rules.keys()).map((ruleName) => {
        ruleName = ruleName.charAt(0).toUpperCase() + ruleName.slice(1)
        return `    std::any visit${ruleName}(OpenCMLParser::${ruleName}Context *context) { return dumpNode(context, "${ruleName}"); };`
    })

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
`
}

export function transformHeaderCode(code, rules) {
    const visitDecls = Array.from(rules.keys()).map((ruleName) => {
        ruleName = ruleName.charAt(0).toUpperCase() + ruleName.slice(1)
        return `    std::any visit${ruleName}(OpenCMLParser::${ruleName}Context *context);`
    })

    const startIndex = code.indexOf('// Auto-generated visitor methods')
    const endIndex = code.indexOf('// End of auto-generated visitor methods', startIndex)
    const header = code.substring(0, startIndex)
    const tail = code.substring(endIndex)

    const body = `// Auto-generated visitor methods\n\n${visitDecls.join('\n\n')}\n\n    `
    return header + body + tail
}

export function generateTmpCppCode(code, rules, className = 'Formatter') {
    const funcMap = new Map()
    const funcRegex = /\/\*[^\*][\s\S]*?\*\/\s*any \w+::visit(\w+)\(/gs
    const funcInfos = []

    let match
    while ((match = funcRegex.exec(code)) !== null) {
        const [funcDecl, funcName] = match
        const startIndex = match.index + funcDecl.length
        const commentEnd = funcDecl.indexOf(`any ${className}::`)
        funcInfos.push({
            name: funcName,
            oldRule: funcDecl.slice(0, commentEnd),
            declStart: match.index,
            bodyStart: code.indexOf('{', startIndex),
            bodyEnd: null
        })
    }

    funcInfos.forEach((func, index) => {
        const nextFuncStart =
            index < funcInfos.length - 1 ? funcInfos[index + 1].declStart : code.length
        const bodyEnd = code.lastIndexOf('}', nextFuncStart)
        func.bodyEnd = Math.min(bodyEnd + 1, code.length)
    })

    funcInfos.forEach((func) => {
        if (func.bodyStart !== null && func.bodyEnd !== null) {
            let bodyContent = code
                .slice(func.bodyStart, func.bodyEnd)
                .replace(/^\s+/, '')
                .replace(/\s+$/, '')

            funcMap.set(func.name, [func.oldRule, bodyContent])
        }
    })

    const TODO = '// TODO: '

    const newDecls = Array.from(rules.entries()).map(([ruleName, ruleText]) => {
        const funcName = ruleName.charAt(0).toUpperCase() + ruleName.slice(1)
        const [oldRule, bodyContent] = funcMap.get(funcName) || ['', '{}']
        const newRule = `/*\r\n${ruleText}\r\n*/\n`
        return (
            (newRule.trim() !== oldRule.trim()
                ? TODO + (oldRule.length ? 'MODIFIED' : 'NEW RULE') + '\n' + oldRule
                : '') +
            newRule +
            `any ${className}::visit${funcName}(OpenCMLParser::${funcName}Context *context) ` +
            bodyContent
        )
    })

    if (newDecls.length === 0) {
        logFail('No new declarations generated')
        return code
    }

    const declStart = funcInfos[0].declStart
    const declEnd = funcInfos[funcInfos.length - 1].bodyEnd
    const head = code.slice(0, declStart)
    const tail = code.slice(declEnd)

    return `${head}${newDecls.join('\n\n')}${tail}`
}
