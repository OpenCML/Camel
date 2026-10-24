#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// H254: the AI learnability evaluation. Measures whether a model that has never seen Camel can
// write correct Camel from `docs/reference/camel-for-ai.md` alone, and reports numbers rather
// than opinions: first-draft pass rate, compile-fix rounds needed, and every diagnostic read.
//
// What is measured, and what is not
// ---------------------------------
// A task is a prompt (test/tools/learnability/tasks.json) with one acceptance: the compiler's own
// reading — an exit code, the program's stdout, and for one task `camel check`. Nothing is graded
// by reading the answer. The subject never sees the acceptance beyond the behaviour the task
// states, and never sees this repository.
//
// Isolation is enforced by the fixture, not promised by the runner:
//
//   * every task runs in its own sandbox containing exactly one file, `camel-for-ai.md` (the
//     reference under test), copied there by this script;
//   * the `bl` solver gets that text in the request and has no file system at all — it cannot read
//     the repository even if it wanted to;
//   * the `cmd` solver is spawned with the sandbox as its working directory and with the round's
//     prompt in a file there;
//   * the `provided` solver takes code produced elsewhere (a second arm driven by another agent).
//     That arm's isolation rests on its own instructions and is therefore weaker than `bl`; the
//     report says so.
//
// Usage
// -----
//   node test/tools/learnability/run.mjs --solver bl --model qwen3.8-max \
//        --tasks L05,L12,L14 --rounds 3 --archive <dir> [--camel-host 88zjwei]
//   node test/tools/learnability/run.mjs --solver provided --provided <dir> --tasks L01,...
//   node test/tools/learnability/run.mjs --solver replay --archive <dir>   (recompile, no tokens)
//
// The compiler is not built on this machine (see AGENTS.md): the harness ships each round to the
// build host and compiles it there with test/tools/learnability/remote-compile.sh.
//
// Exit status: 0 when the run completed and the report was written, 2 on bad usage or a budget stop.

import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import crypto from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const REPO = path.resolve(HERE, '..', '..', '..')
const REFERENCE_REL = 'docs/reference/camel-for-ai.md'
const TASKS_REL = 'test/tools/learnability/tasks.json'
const REMOTE_SCRIPT = path.join(HERE, 'remote-compile.sh')

function usage(message) {
    process.stderr.write(`learnability: ${message}\n`)
    process.stderr.write('usage: run.mjs --solver bl|cmd|provided|replay --tasks a,b,c [options]\n')
    process.exit(2)
}

function parseArgs(argv) {
    const opts = {
        solver: null,
        tasks: null,
        rounds: 1,
        archive: null,
        model: 'qwen3.8-max',
        camelHost: '88zjwei',
        camelBin: '$HOME/camel-work/h425-ai-reference/out/latest/bin/camel',
        remoteRoot: '/tmp/camel-learnability',
        tokenBudget: 1500000,
        timeoutSeconds: 900,
        cmdTemplate: null,
        provided: null,
        keepSandbox: false,
    }
    for (let i = 0; i < argv.length; i++) {
        const key = argv[i]
        const value = argv[i + 1]
        const take = () => {
            if (value === undefined) usage(`${key} needs a value`)
            i++
            return value
        }
        if (key === '--solver') opts.solver = take()
        else if (key === '--tasks') opts.tasks = take().split(',').map((s) => s.trim()).filter(Boolean)
        else if (key === '--rounds') opts.rounds = Number(take())
        else if (key === '--archive') opts.archive = take()
        else if (key === '--model') opts.model = take()
        else if (key === '--camel-host') opts.camelHost = take()
        else if (key === '--camel-bin') opts.camelBin = take()
        else if (key === '--cmd-template') opts.cmdTemplate = take()
        else if (key === '--provided') opts.provided = take()
        else if (key === '--token-budget') opts.tokenBudget = Number(take())
        else if (key === '--timeout') opts.timeoutSeconds = Number(take())
        else if (key === '--keep-sandbox') opts.keepSandbox = true
        else usage(`unknown option ${key}`)
    }
    if (!opts.solver) usage('--solver is required')
    if (!opts.archive) usage('--archive is required (the run is archived, not printed and lost)')
    return opts
}

function must(condition, message) {
    if (!condition) {
        process.stderr.write(`learnability: ${message}\n`)
        process.exit(2)
    }
}

function run(command, args, options = {}) {
    const result = spawnSync(command, args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024, ...options })
    if (result.error) throw new Error(`${command} failed to start: ${result.error.message}`)
    return result
}

// ---------------------------------------------------------------- the round's material

function loadInputs() {
    const tasksPath = path.join(REPO, TASKS_REL)
    const referencePath = path.join(REPO, REFERENCE_REL)
    const manifest = JSON.parse(fs.readFileSync(tasksPath, 'utf8'))
    const reference = fs.readFileSync(referencePath, 'utf8')
    return {
        manifest,
        reference,
        referenceSha256: crypto.createHash('sha256').update(reference).digest('hex'),
        referenceChars: reference.length,
    }
}

// A fenced block is the only thing accepted from a subject: it is the shape the prompt asks for,
// and reading anything else (prose around the file) would grade something the task did not ask.
function extractCamel(text) {
    if (typeof text !== 'string') return null
    const fenced = [...text.matchAll(/```(?:camel|cml)?[ \t]*\r?\n([\s\S]*?)```/g)]
    if (fenced.length) return fenced[0][1].replace(/\r\n/g, '\n').trimEnd() + '\n'
    return null
}

function taskPrompt(manifest, task) {
    return `${manifest.solver_prompt_header}\n\n---\n\n# Task\n\n${task.prompt}\n`
}

function firstTurn(manifest, task, reference) {
    return `${taskPrompt(manifest, task)}\n---\n\nThe only description of Camel you have is this file:\n\n<reference file="camel-for-ai.md">\n${reference}\n</reference>\n`
}

function fixTurn(round, reading) {
    const lines = []
    lines.push(`Your attempt at round ${round} was this file:\n`)
    lines.push('```camel')
    lines.push(reading.code.trimEnd())
    lines.push('```')
    lines.push('')
    lines.push(`\`camel prog.cml\` answered with exit code ${reading.exit}.`)
    if (reading.diagnostics.length) {
        lines.push('')
        lines.push('The compiler reported:')
        lines.push('')
        for (const d of reading.diagnostics) {
            lines.push(`- ${d.severity} ${d.name}: ${d.message}`)
            if (d.suggestion) lines.push(`  help: ${d.suggestion}`)
        }
    }
    if (reading.stdout && reading.exit === 0) {
        lines.push('')
        lines.push('The program printed:')
        lines.push('```text')
        lines.push(reading.stdout.trimEnd())
        lines.push('```')
    }
    lines.push('')
    lines.push('Write the corrected file. Reply with the whole file in a single ```camel block and nothing else.')
    return lines.join('\n')
}

// ---------------------------------------------------------------- solvers

function sandboxFor(runRoot, taskId) {
    const dir = path.join(runRoot, 'sandbox', taskId)
    fs.mkdirSync(dir, { recursive: true })
    return dir
}

function callBl(messages, opts, usageSink) {
    const messagesPath = path.join(opts.archive, '_messages.json')
    fs.mkdirSync(opts.archive, { recursive: true })
    fs.writeFileSync(messagesPath, JSON.stringify(messages), 'utf8')
    // `bl` is a Node CLI; on Windows it is reached through its shim, so one of the two spellings
    // is used, never both -- a retry would spend the request twice.
    const args = ['text', 'chat', '--messages-file', messagesPath, '--model', opts.model, '--output', 'json', '--max-tokens', '8192', '--timeout', String(opts.timeoutSeconds)]
    const final = process.platform === 'win32'
        ? run('cmd', ['/c', 'bl', ...args], { shell: false })
        : run('bl', args, { shell: false })
    if (final.status !== 0) {
        throw new Error(`bl text chat exited ${final.status}: ${(final.stderr || '').slice(0, 800)}`)
    }
    let payload
    try {
        payload = JSON.parse(final.stdout)
    } catch (error) {
        throw new Error(`bl text chat did not answer with JSON: ${final.stdout.slice(0, 400)}`)
    }
    const usage = payload.usage || {}
    usageSink.prompt += usage.prompt_tokens || 0
    usageSink.cached += usage.prompt_tokens_details?.cached_tokens || 0
    usageSink.completion += usage.completion_tokens || 0
    return payload.choices?.[0]?.message?.content ?? ''
}

function callCmd(messages, opts, taskId, round) {
    must(opts.cmdTemplate, '--solver cmd needs --cmd-template')
    const sandbox = sandboxFor(path.join(opts.archive, 'run'), taskId)
    fs.writeFileSync(path.join(sandbox, 'camel-for-ai.md'), fs.readFileSync(path.join(REPO, REFERENCE_REL), 'utf8'))
    const requestPath = path.join(sandbox, 'request.json')
    fs.writeFileSync(requestPath, JSON.stringify(messages), 'utf8')
    const answerPath = path.join(sandbox, 'answer.txt')
    const line = opts.cmdTemplate
        .replaceAll('{request}', requestPath)
        .replaceAll('{answer}', answerPath)
        .replaceAll('{round}', String(round))
        .replaceAll('{sandbox}', sandbox)
    const result = run(line, { shell: true, cwd: sandbox })
    if (result.status !== 0) throw new Error(`cmd solver exited ${result.status}: ${(result.stderr || '').slice(0, 800)}`)
    return fs.existsSync(answerPath) ? fs.readFileSync(answerPath, 'utf8') : result.stdout
}

// ------------------------------------------------------------------ compiling on the build host

function compileRound(opts, runRoot, round, readings) {
    // The round is archived with the run: the sources, the compiler's raw answer and what was
    // shipped, so a later reader can recompile the same bytes without this script.
    const roundName = `round-${round}`
    const local = path.join(opts.archive, 'rounds', roundName)
    fs.mkdirSync(local, { recursive: true })
    const ids = []
    for (const [id, reading] of Object.entries(readings)) {
        if (reading.code === null) continue
        fs.writeFileSync(path.join(local, `${id}.cml`), reading.code, 'utf8')
        fs.writeFileSync(path.join(local, `${id}.mode`), reading.acceptanceKind === 'check-and-run' ? 'check-and-run' : 'run', 'utf8')
        ids.push(id)
    }
    if (!ids.length) return {}

    const runTag = path.basename(opts.archive)
    const remoteDir = `${opts.remoteRoot}/${runTag}/${roundName}`
    run('ssh', [opts.camelHost, `mkdir -p ${remoteDir}`], { shell: false })
    run('scp', ['-q', '-r', `${local}${path.sep}.`, `${opts.camelHost}:${remoteDir}`], { shell: false })
    run('scp', ['-q', REMOTE_SCRIPT, `${opts.camelHost}:${opts.remoteRoot}/remote-compile.sh`], { shell: false })
    const remote = run('ssh', [
        opts.camelHost,
        `CAMEL_BIN="${opts.camelBin}" bash ${opts.remoteRoot}/remote-compile.sh ${remoteDir}`,
    ], { shell: false })
    fs.writeFileSync(path.join(local, 'remote.txt'), remote.stdout || '', 'utf8')
    fs.writeFileSync(path.join(local, 'remote.stderr.txt'), remote.stderr || '', 'utf8')
    if (remote.status !== 0) {
        throw new Error(`remote compile exited ${remote.status}: ${(remote.stderr || '').slice(0, 800)}`)
    }
    return parseRemote(remote.stdout || '')
}

function parseRemote(text) {
    const out = {}
    const blocks = text.split('@@@TASK ').slice(1)
    for (const block of blocks) {
        const id = block.slice(0, block.indexOf('\n')).trim()
        const field = (name) => {
            const marker = `@@@${name}\n`
            const at = block.indexOf(marker)
            if (at < 0) return null
            const rest = block.slice(at + marker.length)
            const end = rest.search(/^@@@/m)
            return end < 0 ? rest : rest.slice(0, end)
        }
        const number = (name) => {
            const raw = field(name)
            return raw === null ? null : Number(raw.trim())
        }
        out[id] = {
            exit: number('EXIT'),
            stdout: field('STDOUT') ?? '',
            stderr: field('STDERR') ?? '',
            checkExit: number('CHECK_EXIT'),
            checkStdout: field('CHECK_STDOUT') ?? '',
        }
    }
    return out
}

// `--output-format json` prints the diagnostics as a JSON array on stdout when the compile or
// the run fails. A program that ran may itself print an array (`[2, 4, 6]`), so an element only
// counts as a diagnostic when it is an object that carries this compiler's own fields.
function diagnosticsOf(stdout) {
    const text = (stdout || '').trim()
    if (!text.startsWith('[')) return []
    try {
        const parsed = JSON.parse(text)
        if (!Array.isArray(parsed)) return []
        return parsed
            .filter((d) => d && typeof d === 'object' && (d.data?.name || d.code))
            .map((d) => ({
                name: d.data?.name || d.code || 'unknown',
                severity: d.severity || '?',
                message: d.message || '',
                suggestion: d.data?.suggestion || '',
                line: d.range?.start?.line,
            }))
    } catch {
        return []
    }
}

function normalize(text) {
    return (text || '').replace(/\r\n/g, '\n').replace(/\s+$/, '')
}

function judge(task, compiled) {
    if (!compiled) return { pass: false, why: 'not compiled' }
    const expected = task.acceptance.stdout === undefined ? null : normalize(task.acceptance.stdout)
    if (compiled.exit !== task.acceptance.exit) {
        return { pass: false, why: `exit ${compiled.exit}, expected ${task.acceptance.exit}` }
    }
    if (expected !== null && normalize(compiled.stdout) !== expected) {
        return { pass: false, why: `stdout ${JSON.stringify(normalize(compiled.stdout))}, expected ${JSON.stringify(expected)}` }
    }
    if (task.acceptance.kind === 'check-and-run' && compiled.checkExit !== 0) {
        return { pass: false, why: `camel check exited ${compiled.checkExit}` }
    }
    return { pass: true, why: "the compiler's reading matches the task" }
}

// ------------------------------------------------------------------------------ the run itself

function main() {
    const opts = parseArgs(process.argv.slice(2))
    const { manifest, reference, referenceSha256, referenceChars } = loadInputs()
    const wanted = opts.tasks ?? manifest.tasks.map((t) => t.id)
    const tasks = manifest.tasks.filter((t) => wanted.includes(t.id))
    must(tasks.length === wanted.length, `unknown task id in ${wanted.join(',')}`)
    must(opts.solver !== 'provided' || opts.provided, '--solver provided needs --provided <dir>')

    const runRoot = path.join(os.tmpdir(), `camel-learnability-${process.pid}`)
    fs.mkdirSync(path.join(opts.archive, 'rounds'), { recursive: true })

    const usageSink = { prompt: 0, cached: 0, completion: 0 }
    const report = {
        schema: 1,
        solver: opts.solver,
        model: opts.solver === 'bl' ? opts.model : null,
        rounds_planned: opts.rounds,
        token_budget: opts.tokenBudget,
        camel_revision: manifest.camel_revision,
        camel_bin: opts.camelBin,
        reference_sha256: referenceSha256,
        reference_chars: referenceChars,
        prompt_header: manifest.solver_prompt_header,
        tasks: [],
    }

    // Round 0: every task gets one first draft, from the reference and nothing else.
    const code = {}
    const history = {}
    const failures = new Map()
    for (const task of tasks) {
        fs.mkdirSync(path.join(opts.archive, task.id), { recursive: true })
        let answer = null
        if (opts.solver === 'provided') {
            const candidates = [`${task.id}.cml`, `${task.id}.txt`, `prog.cml`]
            const dir = opts.provided
            const found = candidates.map((n) => path.join(dir, n)).find((p) => fs.existsSync(p))
            must(found, `--provided has no ${task.id}.cml under ${dir}`)
            answer = fs.readFileSync(found, 'utf8')
        } else if (opts.solver === 'replay') {
            answer = fs.readFileSync(path.join(opts.archive, task.id, 'round0.response.txt'), 'utf8')
        } else {
            history[task.id] = [
                { role: 'system', content: 'You are a careful programmer who has never seen Camel.' },
                { role: 'user', content: firstTurn(manifest, task, reference) },
            ]
            // A failed call is recorded as that task's own outcome and the run goes on: one task
            // that timed out must not cost the other twelve, and a retry would spend the request
            // twice.
            try {
                answer = opts.solver === 'bl'
                    ? callBl(history[task.id], opts, usageSink)
                    : callCmd(history[task.id], opts, task.id, 0)
            } catch (error) {
                answer = ''
                failures.set(task.id, `round 0: ${error.message}`)
                process.stdout.write(`learnability: ${task.id} got no answer: ${error.message}\n`)
            }
            fs.writeFileSync(path.join(opts.archive, task.id, 'round0.response.txt'), answer, 'utf8')
            must(usageSink.prompt <= opts.tokenBudget, `token budget ${opts.tokenBudget} exceeded (${usageSink.prompt} input tokens): stopping`)
        }
        // A code arm is asked for a fenced block; a handed-in file is already the file.
        const extracted = opts.solver === 'provided'
            ? answer.replace(/\r\n/g, '\n').trimEnd() + '\n'
            : extractCamel(answer)
        fs.writeFileSync(path.join(opts.archive, task.id, 'round0.raw.txt'), answer, 'utf8')
        code[task.id] = extracted
        if (extracted) fs.writeFileSync(path.join(opts.archive, task.id, 'round0.cml'), extracted, 'utf8')
    }

    const records = new Map(tasks.map((t) => [t.id, { id: t.id, area: t.area, acceptance: t.acceptance, rounds: [], passed_at_round: null }]))

    for (let round = 0; round < opts.rounds; round++) {
        const readings = {}
        for (const task of tasks) {
            const record = records.get(task.id)
            if (record.passed_at_round !== null) continue
            if (code[task.id] === null) {
                record.rounds.push({ round, code: null, diagnostics: [], pass: false, why: failures.get(task.id) || 'no fenced block in the answer' })
                continue
            }
            readings[task.id] = {
                code: code[task.id],
                acceptanceKind: task.acceptance.kind,
                exit: null,
                stdout: '',
                diagnostics: [],
            }
        }
        const compiled = compileRound(opts, runRoot, round, readings)
        for (const task of tasks) {
            const record = records.get(task.id)
            if (record.passed_at_round !== null) continue
            const entry = readings[task.id]
            if (!entry) {
                // No fenced block last round: ask again with the same material, nothing to compile.
                if (opts.solver === 'bl') {
                    history[task.id].push({ role: 'user', content: 'Your reply had no ```camel block. Reply with the whole file in a single ```camel block and nothing else.' })
                    code[task.id] = extractCamel(callBl(history[task.id], opts, usageSink))
                }
                continue
            }
            const reading = compiled[task.id] || { exit: null, stdout: '', stderr: '', checkExit: null }
            const diagnostics = diagnosticsOf(reading.stdout)
            entry.exit = reading.exit
            entry.stdout = reading.stdout
            const verdict = judge(task, { ...reading, checkExit: reading.checkExit })
            record.rounds.push({
                round,
                code_sha256: crypto.createHash('sha256').update(entry.code).digest('hex'),
                exit: reading.exit,
                stdout: reading.stdout,
                stderr: reading.stderr,
                check_exit: reading.checkExit,
                diagnostics,
                pass: verdict.pass,
                why: verdict.why,
            })
            if (verdict.pass) {
                record.passed_at_round = round
                continue
            }
            if (round + 1 < opts.rounds && opts.solver === 'bl') {
                history[task.id].push({ role: 'assistant', content: '```camel\n' + entry.code + '```' })
                history[task.id].push({ role: 'user', content: fixTurn(round, { code: entry.code, exit: reading.exit, stdout: reading.stdout, diagnostics }) })
                must(usageSink.prompt <= opts.tokenBudget, `token budget ${opts.tokenBudget} exceeded (${usageSink.prompt} input tokens): stopping`)
                let answer = ''
                try {
                    answer = callBl(history[task.id], opts, usageSink)
                } catch (error) {
                    failures.set(task.id, `round ${round + 1}: ${error.message}`)
                    process.stdout.write(`learnability: ${task.id} got no answer: ${error.message}\n`)
                }
                fs.writeFileSync(path.join(opts.archive, task.id, `round${round + 1}.response.txt`), answer, 'utf8')
                code[task.id] = extractCamel(answer)
                fs.writeFileSync(path.join(opts.archive, task.id, `round${round + 1}.raw.txt`), answer, 'utf8')
                if (code[task.id]) fs.writeFileSync(path.join(opts.archive, task.id, `round${round + 1}.cml`), code[task.id], 'utf8')
            }
        }
    }

    for (const task of tasks) report.tasks.push(records.get(task.id))
    report.usage = { input_tokens: usageSink.prompt, cached_input_tokens: usageSink.cached, output_tokens: usageSink.completion }
    report.failures = Object.fromEntries(failures)

    const attempted = report.tasks.length
    const passed = report.tasks.filter((t) => t.passed_at_round !== null).length
    const firstRound = report.tasks.filter((t) => t.passed_at_round === 0).length
    const roundsUsed = report.tasks.filter((t) => t.passed_at_round !== null).map((t) => t.passed_at_round)
    const distribution = {}
    for (const task of report.tasks) {
        for (const round of task.rounds) {
            for (const d of round.diagnostics) distribution[d.name] = (distribution[d.name] || 0) + 1
        }
    }
    report.summary = {
        tasks: attempted,
        passed,
        first_draft_passed: firstRound,
        first_draft_rate: attempted ? Number((firstRound / attempted).toFixed(3)) : 0,
        final_rate: attempted ? Number((passed / attempted).toFixed(3)) : 0,
        mean_rounds_to_pass: roundsUsed.length ? Number((roundsUsed.reduce((a, b) => a + b, 0) / roundsUsed.length).toFixed(2)) : null,
        diagnostic_distribution: Object.fromEntries(Object.entries(distribution).sort((a, b) => b[1] - a[1])),
    }

    fs.writeFileSync(path.join(opts.archive, 'report.json'), JSON.stringify(report, null, 2), 'utf8')
    fs.writeFileSync(path.join(opts.archive, 'report.md'), renderMarkdown(report), 'utf8')
    process.stdout.write(renderMarkdown(report))
    if (!opts.keepSandbox) fs.rmSync(runRoot, { recursive: true, force: true })
}

function renderMarkdown(report) {
    const s = report.summary
    const lines = []
    lines.push(`# Learnability run: ${report.solver}${report.model ? ` (${report.model})` : ''}`)
    lines.push('')
    lines.push(`- camel revision: \`${report.camel_revision}\``)
    lines.push(`- reference: \`docs/reference/camel-for-ai.md\`, ${report.reference_chars} characters, sha256 \`${report.reference_sha256}\``)
    lines.push(`- tasks: ${s.tasks}, first-draft pass ${s.first_draft_passed}/${s.tasks} (${Math.round(s.first_draft_rate * 100)}%), final pass ${s.passed}/${s.tasks} (${Math.round(s.final_rate * 100)}%)`)
    lines.push(`- mean compile-fix rounds to pass: ${s.mean_rounds_to_pass === null ? 'n/a' : s.mean_rounds_to_pass}`)
    if (report.usage) {
        lines.push(`- tokens: ${report.usage.input_tokens} in (${report.usage.cached_input_tokens} cached), ${report.usage.output_tokens} out`)
    }
    lines.push('')
    lines.push('| task | area | passed at round | rounds | why it did not pass |')
    lines.push('|---|---|---|---|---|')
    for (const task of report.tasks) {
        const why = task.rounds.filter((r) => !r.pass).map((r) => `r${r.round}: ${r.why}`).join('; ') || '—'
        lines.push(`| ${task.id} | ${task.area} | ${task.passed_at_round === null ? 'never' : task.passed_at_round} | ${task.rounds.length} | ${why} |`)
    }
    lines.push('')
    lines.push('## Diagnostic distribution')
    lines.push('')
    for (const [name, count] of Object.entries(s.diagnostic_distribution)) lines.push(`- ${name}: ${count}`)
    if (!Object.keys(s.diagnostic_distribution).length) lines.push('- (none: every reading was produced by a program that ran)')
    lines.push('')
    return lines.join('\n')
}

main()