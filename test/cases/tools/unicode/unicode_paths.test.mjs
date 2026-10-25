#!/usr/bin/env node
// item 3: the process boundary on Windows. A user name, a PATH entry or a checkout whose path
// holds characters the process code page cannot express used to break `env.basic`: Camel strings
// are UTF-8 and guaranteed to be, while what the process hands the program (the command line, the
// environment, the current directory, the module search paths) arrived as bytes of the code page.
// This script builds such a world under CAMEL_TEST_TMPDIR (the runner's per-test scratch
// directory; tmp/test/unicode-paths on a direct run) and compares the text the program prints
// with the text the test itself wrote.
//
// The names below are chosen against CP936 (the code page of the machine this was written on):
// 中文 and Ω have a GBK code, 🐪 has none, so a program that survives them read its arguments,
// environment and paths through the wide interface.
//
// Run by test/plans/feat/modules/unicode_paths.plan.toml; on other platforms the plan skips it
// through test/tools/on-windows.mjs.

import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..')
const CASES = path.join(ROOT, 'test', 'cases', 'modules', 'std', 'env', 'unicode')
const RUN = path.join(process.env.CAMEL_TEST_TMPDIR || path.join(ROOT, 'tmp', 'test'), 'unicode-paths')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')

const TEXT = '你好，世界 Ω 🐪'
const VALUE = '值 中文 Ω 🐪'
const NAMED = '中文值 🐪'
const BARE = '嵌套内容'

let failed = false

function check(name, ok, detail = '') {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${ok || !detail ? '' : `: ${detail}`}`)
    if (!ok) {
        failed = true
    }
}

function stderr(proc) {
    return (proc.stderr || '').trim().split(/\r?\n/).slice(0, 4).join(' | ')
}

/// The `key=value` lines the test programs print.
function fields(stdout) {
    const out = new Map()
    for (const line of String(stdout).split(/\r?\n/)) {
        const at = line.indexOf('=')
        if (at > 0) {
            out.set(line.slice(0, at), line.slice(at + 1))
        }
    }
    return out
}

/// Compares two names of a directory that exists: the same directory may be spelled with a
/// different case (Windows does not distinguish) or through a junction.
function samePath(text, expected) {
    const norm = (p) => {
        try {
            return fs.realpathSync.native(p).replace(/[\\/]+$/, '').toLowerCase()
        } catch {
            return null
        }
    }
    const a = norm(text)
    return a !== null && a === norm(expected)
}

function run(exe, args, cwd, env) {
    const proc = spawnSync(exe, args, {
        cwd,
        env: { ...process.env, CAMEL_HOME: home, ...env },
        encoding: 'utf8',
        timeout: 120000,
    })
    if (proc.error) {
        throw proc.error
    }
    return proc
}

/// The lines a program that names its cwd, an environment variable and its files prints.
function checkProcessText(label, proc, cwd) {
    const out = fields(proc.stdout)
    check(`${label}: exit`, proc.status === 0, stderr(proc))
    check(`${label}: get_cwd`, samePath(out.get('cwd'), cwd), `got ${JSON.stringify(out.get('cwd'))}`)
    check(`${label}: environment value`, out.get('value') === VALUE, `got ${JSON.stringify(out.get('value'))}`)
    check(`${label}: environment name`, out.get('named') === NAMED, `got ${JSON.stringify(out.get('named'))}`)
    check(`${label}: file written and seen`, out.get('exists') === 'true', `got ${JSON.stringify(out.get('exists'))}`)
    check(
        `${label}: file_size of UTF-8 text`,
        out.get('size') === String(Buffer.byteLength(TEXT, 'utf8')),
        `got ${JSON.stringify(out.get('size'))}, want ${Buffer.byteLength(TEXT, 'utf8')}`,
    )
    check(`${label}: read_text`, out.get('text') === TEXT, `got ${JSON.stringify(out.get('text'))}`)
    check(`${label}: parent directory created`, out.get('nested') === BARE, `got ${JSON.stringify(out.get('nested'))}`)
    check(`${label}: exists on a missing file`, out.get('missing') === 'false', `got ${JSON.stringify(out.get('missing'))}`)
}

function main() {
    fs.rmSync(RUN, { recursive: true, force: true })
    const dir = path.join(RUN, '中文 目录 🐪')
    fs.mkdirSync(dir, { recursive: true })

    const program = path.join(dir, '程序 空格 🐪.cml')
    fs.copyFileSync(path.join(CASES, 'env_paths.cml'), program)

    const env = {
        // A value and a name outside the code page: the name proves that reading the variable
        // goes through the wide environment block, not only that its value is converted.
        CAMEL_H9_VALUE: VALUE,
        H9变量: NAMED,
        // A PATH entry outside the code page, as a user's PATH often has one.
        PATH: `${dir};${path.join(home, 'bin')};${process.env.PATH ?? ''}`,
    }

    // 1. The program by its absolute path: the command line carries the emoji.
    checkProcessText('absolute argv', run(camel, [program], dir, env), dir)

    // 2. The program by a name relative to a current directory outside the code page.
    checkProcessText('relative argv', run(camel, [path.basename(program)], dir, env), dir)

    // 3. A module found through CAMEL_PACKAGES, whose root is outside the code page.
    const pkg = path.join(RUN, '包 目录 🐪')
    fs.mkdirSync(pkg, { recursive: true })
    fs.copyFileSync(path.join(CASES, 'helper.cml'), path.join(pkg, 'helper.cml'))
    const user = path.join(RUN, '用户 程序 🐪.cml')
    fs.copyFileSync(path.join(CASES, 'package_user.cml'), user)

    const packaged = run(camel, [user], ROOT, { CAMEL_PACKAGES: pkg })
    const packagedOut = fields(packaged.stdout)
    check('CAMEL_PACKAGES: exit', packaged.status === 0, stderr(packaged))
    check(
        'CAMEL_PACKAGES: module from a non-ASCII root',
        packagedOut.get('package') === '42',
        `got ${JSON.stringify(packagedOut.get('package'))}`,
    )
    check(
        'CAMEL_PACKAGES: CAMEL_HOME read',
        samePath(packagedOut.get('home'), home),
        `got ${JSON.stringify(packagedOut.get('home'))}`,
    )

    // 4. The installation itself under a non-ASCII path: CAMEL_HOME names a junction to the build
    //    output, and the program is started through it, so the stdlib (env, fs) is found there.
    const link = path.join(RUN, '安装 目录 🐪')
    fs.mkdirSync(link, { recursive: true })
    const sdk = path.join(link, 'sdk')
    fs.symlinkSync(fs.realpathSync.native(home), sdk, 'junction')
    const linked = run(
        path.join(sdk, 'bin', path.basename(camel)),
        [user],
        ROOT,
        { CAMEL_HOME: sdk, CAMEL_PACKAGES: pkg, PATH: `${path.join(sdk, 'bin')};${process.env.PATH ?? ''}` },
    )
    const linkedOut = fields(linked.stdout)
    check('non-ASCII CAMEL_HOME: exit', linked.status === 0, stderr(linked))
    check(
        'non-ASCII CAMEL_HOME: module and stdlib found',
        linkedOut.get('package') === '42',
        `got ${JSON.stringify(linkedOut.get('package'))}`,
    )
    check(
        'non-ASCII CAMEL_HOME: get_env',
        samePath(linkedOut.get('home'), sdk),
        `got ${JSON.stringify(linkedOut.get('home'))}`,
    )

    // 5. The negative side: a diagnostic about a file outside the code page keeps its name, in
    //    UTF-8, instead of the '?' the code page would substitute.
    const failing = path.join(dir, '缺失 程序 🐪.cml')
    fs.copyFileSync(path.join(CASES, 'missing_file.cml'), failing)
    const missing = run(camel, [path.basename(failing)], dir, env)
    // The CLI writes the diagnostic to stdout when the message is rich; accept either stream.
    const diagnostic = String(missing.stdout) + String(missing.stderr)
    check('missing file: exit is 1', missing.status === 1, `got ${missing.status}`)
    check(
        'missing file: diagnostic keeps the name',
        diagnostic.includes('不存在 文件 🐪.txt'),
        stderr(missing),
    )
    check(
        'missing file: no replacement character',
        !diagnostic.includes('\uFFFD'),
        stderr(missing),
    )

    if (failed) {
        console.log(`kept ${RUN} for inspection`)
    } else {
        fs.rmSync(RUN, { recursive: true, force: true })
        console.log('PASS the wide-character process boundary')
    }
    process.exit(failed ? 1 : 0)
}

main()