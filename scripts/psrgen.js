import fs from 'fs'
import path from 'path'
import { logStep, logDone, logFail, runCommand } from './common.js'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const rootDir = path.join(__dirname, '..')
const srcAntlrDir = path.join(rootDir, 'src', 'parse', 'antlr')
const includeAntlrDir = path.join(rootDir, 'include', 'camel', 'parse', 'antlr')

// 需要统一为 parse/antlr/ 前缀的生成头文件
const GENERATED_HEADERS = ['OpenCMLLexer.h', 'OpenCMLParser.h', 'OpenCMLVisitor.h', 'OpenCMLBaseVisitor.h']

function replaceInFileSync(filePath) {
    let data
    try {
        data = fs.readFileSync(filePath, 'utf8')
    } catch (err) {
        logFail(`Error reading file ${filePath}: ${err}`)
        return
    }
    let result = data.replace(
        /#include "antlr4-runtime\.h"/g,
        '#include "antlr4-runtime/antlr4-runtime.h"'
    )
    for (const h of GENERATED_HEADERS) {
        const escaped = h.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
        result = result.replace(
            new RegExp(`#include "${escaped}"`, 'g'),
            `#include "camel/parse/antlr/${h}"`
        )
    }
    try {
        fs.writeFileSync(filePath, result, 'utf8')
    } catch (err) {
        logFail(`Error writing file ${filePath}: ${err}`)
    }
}

function walkDirAndReplaceSync(dir) {
    if (!fs.existsSync(dir)) return
    const files = fs.readdirSync(dir, { withFileTypes: true })
    for (const file of files) {
        const filePath = path.join(dir, file.name)
        if (file.isDirectory()) {
            walkDirAndReplaceSync(filePath)
        } else if (
            file.isFile() &&
            (path.extname(file.name) === '.cpp' || path.extname(file.name) === '.h')
        ) {
            replaceInFileSync(filePath)
        }
    }
}

function moveFilesUp() {
    const nestedDir = path.join(srcAntlrDir, 'antlr')
    if (!fs.existsSync(nestedDir)) {
        logStep('No nested antlr directory found, skipping move.')
        return
    }
    logStep(`Found nested directory, moving files up...`)
    const files = fs.readdirSync(nestedDir)
    for (const file of files) {
        const srcPath = path.join(nestedDir, file)
        const destPath = path.join(srcAntlrDir, file)
        if (fs.existsSync(destPath)) {
            fs.unlinkSync(destPath)
        }
        fs.renameSync(srcPath, destPath)
    }
    const remaining = fs.readdirSync(nestedDir)
    if (remaining.length === 0) {
        fs.rmdirSync(nestedDir)
        logStep('Moved files up and removed empty nested directory.')
    } else {
        logStep('Moved files up, but nested directory is not empty:', remaining)
    }
}

function moveHeadersToInclude() {
    if (!fs.existsSync(srcAntlrDir)) return
    if (!fs.existsSync(includeAntlrDir)) {
        fs.mkdirSync(includeAntlrDir, { recursive: true })
        logStep(`Created ${includeAntlrDir}`)
    }
    const files = fs.readdirSync(srcAntlrDir)
    for (const file of files) {
        if (path.extname(file) !== '.h') continue
        const srcPath = path.join(srcAntlrDir, file)
        const destPath = path.join(includeAntlrDir, file)
        if (fs.existsSync(destPath)) fs.unlinkSync(destPath)
        fs.copyFileSync(srcPath, destPath)
        fs.unlinkSync(srcPath)
        logStep(`Moved ${file} to include/camel/parse/antlr/`)
    }
}

function generateAntlrParser() {
    logStep('Generating ANTLR parser...')
    runCommand(
        'java -jar ./antlr/antlr-4.13.1-complete.jar -Dlanguage=Cpp -DcontextSuperClass=antlr4::RuleContextWithAltNum ./antlr/OpenCML.g4 -no-listener -visitor -o "./src/parse/antlr"'
    )
    moveFilesUp()
    logDone('Generated ANTLR parser')

    logStep('Moving .h files to include/camel/parse/antlr/...')
    moveHeadersToInclude()
    logDone('Headers moved to include')

    logStep('Redirecting includes in ANTLR generated files...')
    walkDirAndReplaceSync(srcAntlrDir)
    walkDirAndReplaceSync(includeAntlrDir)
    logDone('Redirected includes')
}

generateAntlrParser()
