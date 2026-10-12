#!/usr/bin/env node
// Precondition of the grammar snapshot tests (test/plans/feat/tools/grammar.plan.toml):
// exits 0 when the tokeniser's packages are installed, 1 with a one-line reason otherwise, so
// that a checkout that has not run `npm install` in editors/vscode skips those tests.

import { createRequire } from 'node:module'

const require = createRequire(import.meta.url)
const missing = []
for (const name of ['vscode-textmate', 'vscode-oniguruma']) {
    try {
        require.resolve(`${name}/package.json`)
    } catch {
        missing.push(name)
    }
}
if (missing.length > 0) {
    console.log(`${missing.join(' and ')} not installed: run \`npm install\` in editors/vscode to enable the grammar snapshot tests`)
    process.exit(1)
}
