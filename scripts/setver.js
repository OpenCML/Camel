import fs from 'fs'
import { logStep, logDone } from './common.js'

const ver = process.argv[2]
if (!ver) {
    logDone(`No version provided, exit.`)
}
logStep('Setting version to ' + ver)

// update version in package.json
const packageJson = JSON.parse(fs.readFileSync('package.json', 'utf8'))
packageJson.version = ver
fs.writeFileSync('package.json', JSON.stringify(packageJson, null, 4))
logDone('Updated package.json')

// update version in src/config.h
let configContent = fs.readFileSync('src/config.h', 'utf8')
configContent = configContent.replace(/#define VERSION ".*"/, `#define VERSION "${ver}"`)
fs.writeFileSync('src/config.h', configContent)
logDone('Updated src/config.h')

// update version in pypi/camel-lang/pyproject.toml
let pyprojectContent = fs.readFileSync('pypi/camel-lang/pyproject.toml', 'utf8')
pyprojectContent = pyprojectContent.replace(/version = ".*"/, `version = "${ver}"`)
fs.writeFileSync('pypi/camel-lang/pyproject.toml', pyprojectContent)
logDone('Updated pypi/camel-lang/pyproject.toml')

// check if changes.log exists and starts with the version like "[ver]"
const changesLog = fs.readFileSync('changes.log', 'utf8')
const changesLogLines = changesLog.split('\n')
const firstLine = changesLogLines[0]
if (!firstLine.startsWith(`[${ver}]`)) {
    logStep('changes.log does not start with the version, adding it')
    fs.writeFileSync('changes.log', `[${ver}]\n\n${changesLog}`)
}
logDone('Updated changes.log')

logDone('All processes completed.')
