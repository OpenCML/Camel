import path from 'path'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'
import { splitNulList } from './file-list.js'

const __filename = fileURLToPath(import.meta.url)
const REPO_ROOT = path.dirname(path.dirname(__filename))

function run(command, args, options = {}) {
    const proc = spawnSync(command, args, {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        stdio: options.stdio || 'pipe',
    })
    if (proc.error) {
        throw new Error(proc.error.message)
    }
    if (options.allowExitCodes?.includes(proc.status)) {
        return proc
    }
    if (proc.status !== 0) {
        const detail = `${proc.stdout || ''}${proc.stderr || ''}`.trim()
        throw new Error(`${command} ${args.join(' ')} failed${detail ? `:\n${detail}` : ''}`)
    }
    return proc
}

function git(args, options = {}) {
    return run('git', args, options)
}

function gitPathList(args) {
    const proc = git([...args, '-z'])
    return splitNulList(proc.stdout)
}

function hasUnstagedChanges(file) {
    const proc = git(['diff', '--quiet', '--', file], { allowExitCodes: [0, 1] })
    return proc.status === 1
}

function rejectPartialStaging(files) {
    const partialFiles = files.filter(hasUnstagedChanges)
    if (partialFiles.length === 0) {
        return
    }
    console.error('[precommit] Refusing to auto-format with partially staged files.')
    console.error('[precommit] Stage the full file or split the changes before committing:')
    for (const file of partialFiles) {
        console.error(`  - ${file}`)
    }
    process.exit(1)
}

function runFormatterScripts(stagedFiles) {
    run(process.execPath, ['scripts/format.js', '--files', ...stagedFiles], { stdio: 'inherit' })
    run(process.execPath, ['scripts/update.js', '--files', ...stagedFiles], { stdio: 'inherit' })
}

function main() {
    // Target selection stays inside format.js/update.js; this hook only limits
    // the operation to paths that were already staged for the commit.
    const stagedFiles = gitPathList([
        'diff',
        '--cached',
        '--name-only',
        '--diff-filter=ACMR',
    ])
    if (stagedFiles.length === 0) {
        console.log('[precommit] No staged files.')
        return
    }

    rejectPartialStaging(stagedFiles)
    runFormatterScripts(stagedFiles)
    git(['add', '--', ...stagedFiles], { stdio: 'inherit' })
}

try {
    main()
} catch (error) {
    console.error(`[precommit] ${error.message}`)
    process.exit(1)
}
