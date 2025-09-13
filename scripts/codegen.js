import fs from 'fs'
import { logStep, logDone, logFail } from './common.js'

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

function geneDeclByRuleName(ruleName) {
    return `    std::any visit${ruleName}(OpenCMLParser::${ruleName}Context *context);`
}

function geneCSTDumperDeclByRuleName(ruleName) {
    return `    std::any visit${ruleName}(OpenCMLParser::${ruleName}Context *context) { return dumpNode(context, "${ruleName}"); };`
}

function transformHeaderCode(code, rules, generator = geneDeclByRuleName) {
    const visitDecls = Array.from(rules.keys()).map((ruleName) => {
        ruleName = ruleName.charAt(0).toUpperCase() + ruleName.slice(1)
        return generator(ruleName)
    })

    const startIndex = code.indexOf('// Auto-generated visitor methods')
    const endIndex = code.indexOf('// End of auto-generated visitor methods', startIndex)
    const header = code.substring(0, startIndex)
    const tail = code.substring(endIndex)

    const body = `// Auto-generated visitor methods\n\n${visitDecls.join('\n\n')}\n\n    `
    return header + body + tail
}

function generateTmpCppCode(code, rules, className = 'Formatter') {
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

function generateParserCode() {
    logStep('Parsing grammar rules...')
    const grammarContent = fs.readFileSync('./antlr/OpenCML.g4', 'utf-8')
    const rules = parseGrammarRules(grammarContent)
    logDone('Parsed grammar rules')

    let srcCode = ''
    let geneCode = ''

    logStep('Modifying CSTDumpVisitor...')
    srcCode = fs.readFileSync('./src/parse/cst_dumper.h', 'utf-8')
    geneCode = transformHeaderCode(srcCode, rules, geneCSTDumperDeclByRuleName)
    fs.writeFileSync('./src/parse/cst_dumper.h', geneCode)
    logDone('Modified CSTDumpVisitor')

    logStep('Modifying Formatter header code...')
    srcCode = fs.readFileSync('./src/service/formatter/fmt.h', 'utf-8')
    geneCode = transformHeaderCode(srcCode, rules)
    fs.writeFileSync('./src/service/formatter/fmt.h', geneCode)
    logDone('Modified Formatter header')

    logStep('Generating Formatter cpp code...')
    srcCode = fs.readFileSync('./src/service/formatter/fmt.cpp', 'utf-8')
    geneCode = generateTmpCppCode(srcCode, rules, 'Formatter')
    fs.writeFileSync('./src/service/formatter/fmt.tmp.cpp', geneCode)
    logDone('Generated Formatter code')

    logStep('Modifying AST Builder header code...')
    srcCode = fs.readFileSync('./src/parse/ast_builder.h', 'utf-8')
    geneCode = transformHeaderCode(srcCode, rules)
    fs.writeFileSync('./src/parse/ast_builder.h', geneCode)
    logDone('Modified AST Builder header')

    logStep('Generating AST Builder cpp code...')
    srcCode = fs.readFileSync('./src/parse/ast_builder.cpp', 'utf-8')
    geneCode = generateTmpCppCode(srcCode, rules, 'Builder')
    fs.writeFileSync('./src/parse/ast_builder.tmp.cpp', geneCode)
    logDone('Generated AST Builder code')
}

generateParserCode()
