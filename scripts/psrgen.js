import fs from 'fs'
import path from 'path'
import { logStep, logDone, logFail, runCommand } from './common.js'
import { parseGrammarRules, generateCSTDumpVisitor, transformHeaderCode, generateTmpCppCode } from './codegen.js'

const searchDir = './src/antlr'

function replaceInFile(filePath) {
    fs.readFile(filePath, 'utf8', (err, data) => {
        if (err) {
            logFail(`Error reading file ${filePath}: ${err}`)
            return
        }

        const result = data.replace(
            /#include "antlr4-runtime\.h"/g,
            '#include "antlr4-runtime/antlr4-runtime.h"'
        )

        fs.writeFile(filePath, result, 'utf8', (err) => {
            if (err) {
                logFail(`Error writing file ${filePath}: ${err}`)
            }
        })
    })
}

function walkDirAndReplace(dir) {
    fs.readdir(dir, { withFileTypes: true }, (err, files) => {
        if (err) {
            logFail(`Error reading directory ${dir}: ${err}`)
            return
        }

        files.forEach((file) => {
            const filePath = path.join(dir, file.name)
            if (file.isDirectory()) {
                walkDirAndReplace(filePath)
            } else if (
                file.isFile() &&
                (path.extname(file.name) === '.cpp' || path.extname(file.name) === '.h')
            ) {
                replaceInFile(filePath)
            }
        })
    })
}

function generateAntlrParser() {
    // logStep('Generating ANTLR parser...')
    // runCommand(
    //     'java -jar ./antlr/antlr-4.13.1-complete.jar -Dlanguage=Cpp -DcontextSuperClass=antlr4::RuleContextWithAltNum ./antlr/OpenCML.g4 -no-listener -visitor -o "./src/antlr"'
    // )
    // logDone('Generated ANTLR parser')

    // logStep('Redirecting includes in ANTLR generated files...')
    // // Replace #include "antlr4-runtime.h" with #include "antlr4-runtime/antlr4-runtime.h"
    // // in all generated files in ./src/antlr
    // walkDirAndReplace(searchDir)
    // logDone('Redirected includes')

    // // const headerContent = fs.readFileSync('./src/antlr/OpenCMLVisitor.h', 'utf-8')
    // // const decls = extractDecls(headerContent)

    const grammarContent = fs.readFileSync('./antlr/OpenCML.g4', 'utf-8')
    const rules = parseGrammarRules(grammarContent)

    // logStep('Modifying CSTDumpVisitor.h...')
    // const visitorContent = generateCSTDumpVisitor(rules)
    // fs.writeFileSync('./src/compile/parse/cst.h', visitorContent)
    // logDone('Modified CSTDumpVisitor.h')

    logStep('Transforming Formatter header code...')
    const formatterContent = fs.readFileSync('./src/service/formatter/fmt.h', 'utf-8')
    const transformedFormatterContent = transformHeaderCode(formatterContent, rules)
    fs.writeFileSync('./src/service/formatter/fmt.h', transformedFormatterContent)
    logDone('Transformed Formatter header')

    logStep('Transforming GCT Constructor header code...')
    const gctContent = fs.readFileSync('./src/compile/parse/gct.h', 'utf-8')
    const transformedGCTContent = transformHeaderCode(gctContent, rules)
    fs.writeFileSync('./src/compile/parse/gct.h', transformedGCTContent)
    logDone('Transformed GCT Constructor header')

    logStep('Generating Formatter cpp code...')
    let srcCode = fs.readFileSync('./src/service/formatter/fmt.cpp', 'utf-8')
    let geneCode = generateTmpCppCode(srcCode, rules, 'Formatter')
    fs.writeFileSync('./src/service/formatter/fmt.tmp.cpp', geneCode)
    logDone('Generated Formatter code')

    logStep('Generating GCT Constructor cpp code...')
    srcCode = fs.readFileSync('./src/compile/parse/gct-new.cpp', 'utf-8')
    geneCode = generateTmpCppCode(srcCode, rules, 'Constructor')
    fs.writeFileSync('./src/compile/parse/gct.tmp.cpp', geneCode)
    logDone('Generated GCT Constructor code')
}

generateAntlrParser()
