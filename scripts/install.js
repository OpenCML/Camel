// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import path from 'path'
import fs from 'fs'
import { execFileSync } from 'child_process'
import { removeDir, conanInstall, logStep, logWarn, BASEDIR } from './common.js'

// The grammar snapshot tests of `test/plans/feat/tools/grammar.plan.toml` tokenise Camel source with
// the TextMate grammar of `editors/vscode`, which needs that extension's devDependencies
// (`vscode-textmate`, `vscode-oniguruma`; see `editors/vscode/scripts/have-deps.mjs`). Without them
// the tests SKIP instead of running, so the install step puts them there.
//
// `npm install` installs the whole devDependencies of `editors/vscode`, as its own README and
// `scripts/tokenize.mjs` tell a reader to do. When npm is not there or the registry cannot be
// reached the install is reported and the run goes on: the snapshot tests then skip, which is the
// state a checkout without them is already in, and nothing else depends on these packages.
function installEditorDependencies() {
    const editorRoot = path.join(BASEDIR, 'editors', 'vscode')
    if (!fs.existsSync(path.join(editorRoot, 'package.json'))) {
        return
    }
    if (fs.existsSync(path.join(editorRoot, 'node_modules', 'vscode-textmate'))) {
        logStep('editors/vscode: vscode-textmate is installed, the grammar snapshot tests can run')
        return
    }
    logStep('editors/vscode: installing the devDependencies of the TextMate grammar tests')
    try {
        execFileSync('npm', ['install', '--no-audit', '--no-fund'], {
            cwd: editorRoot,
            stdio: 'inherit',
        })
    } catch (error) {
        logWarn(
            `editors/vscode: npm install failed (${error.message}); the grammar snapshot tests ` +
                'will skip until the devDependencies are installed',
        )
    }
}

function main() {
    process.chdir(BASEDIR)
    removeDir(path.join(BASEDIR, 'build'))
    // Which build types, see conanBuildTypes(): a job that builds only Release sets
    // CAMEL_CONAN_BUILD_TYPES=Release and skips the other two.
    conanInstall()
    installEditorDependencies()
}

main()