import fs from 'fs'
import path from 'path'
import { logStep, logDone, logFail, runCommand } from './common.js'
import { fileURLToPath } from 'url'

const searchDir = './src/parse/antlr'

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

function moveFilesUp() {
    const __filename = fileURLToPath(import.meta.url)
    const __dirname = path.dirname(__filename)
    const nestedDir = path.join(__dirname, 'src', 'parse', 'antlr', 'antlr')
    const targetDir = path.join(__dirname, 'src', 'parse', 'antlr')
    if (!fs.existsSync(nestedDir)) {
        return
    }

    const files = fs.readdirSync(nestedDir)

    files.forEach((file) => {
        const ext = path.extname(file)
        if (ext === '.h' || ext === '.cpp') {
            const srcPath = path.join(nestedDir, file)
            const destPath = path.join(targetDir, file)

            if (fs.existsSync(destPath)) {
                fs.unlinkSync(destPath)
            }

            fs.renameSync(srcPath, destPath)
        }
    })

    const remaining = fs.readdirSync(nestedDir)
    if (remaining.length === 0) {
        fs.rmdirSync(nestedDir)
    }
}

function generateAntlrParser() {
    logStep('Generating ANTLR parser...')
    runCommand(
        'java -jar ./antlr/antlr-4.13.1-complete.jar -Dlanguage=Cpp -DcontextSuperClass=antlr4::RuleContextWithAltNum ./antlr/OpenCML.g4 -no-listener -visitor -o "./src/parse/antlr"'
    )
    moveFilesUp()
    logDone('Generated ANTLR parser')

    logStep('Redirecting includes in ANTLR generated files...')
    // Replace #include "antlr4-runtime.h" with #include "antlr4-runtime/antlr4-runtime.h"
    // in all generated files in ./src/antlr
    walkDirAndReplace(searchDir)
    logDone('Redirected includes')
}

generateAntlrParser()
