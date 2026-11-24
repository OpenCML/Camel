// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import fs from 'fs'
import { logStep, logDone } from './common.js'

const ver = process.argv[2]
if (!ver) {
    logDone(`No version provided, exit.`)
}
if (!/^\d+\.\d+\.\d+$/.test(ver)) {
    logDone(`Invalid version format, exit.`)
}
logStep('Setting version to ' + ver)

// The tree was re-laid-out twice under this script and neither move touched it: `changes.log`
// became `CHANGELOG.md` and `pypi/` became `packaging/pypi/` in `2de5b196f8`. The bump then wrote
// package.json and config.h and threw, leaving a tree that said 0.2.47 everywhere except the files
// a reader opens first. Every path is looked up among the names it has had, so a branch cut before
// the move still bumps, and a missing file is an error rather than half a bump.
function find(...candidates) {
    const found = candidates.find((name) => fs.existsSync(name))
    if (!found) {
        throw new Error(`none of these exists: ${candidates.join(', ')}`)
    }
    return found
}

// update version in package.json
const packageJson = JSON.parse(fs.readFileSync('package.json', 'utf8'))
packageJson.version = ver
fs.writeFileSync('package.json', JSON.stringify(packageJson, null, 4))
logDone('Updated package.json')

// update version in src/tools/camel-cli/config.h
const configFile = find('src/tools/camel-cli/config.h')
let configContent = fs.readFileSync(configFile, 'utf8')
configContent = configContent.replace(/#define VERSION ".*"/, `#define VERSION "${ver}"`)
fs.writeFileSync(configFile, configContent)
logDone(`Updated ${configFile}`)

// update version in the wheel's pyproject.toml
const pyprojectFile = find(
    'packaging/pypi/camel-lang/pyproject.toml',
    'pypi/camel-lang/pyproject.toml',
)
let pyprojectContent = fs.readFileSync(pyprojectFile, 'utf8')
pyprojectContent = pyprojectContent.replace(/^version = ".*"/m, `version = "${ver}"`)
fs.writeFileSync(pyprojectFile, pyprojectContent)
logDone(`Updated ${pyprojectFile}`)

// update version in the wheel package's __init__.py, when it still carries a literal version
const initFile = find(
    'packaging/pypi/camel-lang/src/camel/__init__.py',
    'pypi/camel-lang/src/camel/__init__.py',
)
let initContent = fs.readFileSync(initFile, 'utf8')
if (/__version__ = '/.test(initContent)) {
    initContent = initContent.replace(/__version__ = '.*'/, `__version__ = '${ver}'`)
    fs.writeFileSync(initFile, initContent)
    logDone(`Updated ${initFile}`)
} else {
    logStep(`${initFile} reads its version from the installed distribution; nothing to write`)
}

// check if the release notes start with the version like "[ver]"
const notesFile = find('CHANGELOG.md', 'changes.log')
const notes = fs.readFileSync(notesFile, 'utf8')
if (!notes.split('\n')[0].startsWith(`[${ver}]`)) {
    logStep(`${notesFile} does not start with the version, adding it`)
    fs.writeFileSync(notesFile, `[${ver}]\n\n${notes}`)
}
logDone(`Updated ${notesFile}`)

logDone('All processes completed.')