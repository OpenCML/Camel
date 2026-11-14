#!/usr/bin/env node
/**
 * Prints are effects. `std::opt` may make a program faster and smaller, but it may not make it
 * print fewer lines: design principles 9.7 (a pass never changes which effects run), 3.1 and 3.4.
 * This sweep is the lock on that, over the whole corpus rather than over the cases someone thought
 * of: every `.cml` file that prints is run twice -- with `std::nvm` alone, and with `std::opt
 * std::nvm` -- and the two stdouts are compared.
 *
 * Three answers, in the order they matter:
 *
 *   fewer    the optimized run prints fewer lines than the unoptimized one. An effect was dropped.
 *            This is a defect, always: the sweep fails when there is one.
 *   differs  the same number of lines, different text. Also a defect in principle (a value changed
 *            under a semantics-preserving pass), but a different one from a lost effect, so it is
 *            counted apart.
 *   same     identical stdout.
 *
 * A case that cannot be compared -- the program needs an argument this sweep does not know, it
 * exits non-zero (a case written to fail on purpose), it needs input, it times out -- is reported
 * as `skipped` with the reason, never as `same`.
 *
 * A program that is *expected* to print less may be registered in
 * `print-diff-sweep.exceptions.txt` next to this file: one `path` and a reason per line, separated
 * by a tab. The reason is read by a human; a line without one is refused, so an entry cannot be
 * added just to make the count zero.
 *
 * Usage:
 *   node test/tools/print-diff-sweep.mjs [--camel <path>] [--root <dir>] [--jobs N] [--verbose]
 *                                       [--report <file.json>] [--filter <substring>]
 *
 * Exit code: 0 when no case prints fewer lines, 1 otherwise (or 2 when the setup is wrong).
 */
import { spawn } from 'node:child_process'
import { existsSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import process from 'node:process'

const here = dirname(fileURLToPath(import.meta.url))
const repoRoot = resolve(here, '..', '..')

const argv = process.argv.slice(2)
const option = (name, fallback) => {
  const at = argv.indexOf(`--${name}`)
  return at < 0 ? fallback : argv[at + 1]
}
const flag = (name) => argv.includes(`--${name}`)

const root = resolve(option('root', repoRoot))
const jobs = Math.max(1, Number(option('jobs', '8')))
const verbose = flag('verbose')
const reportPath = option('report', null)
const filter = option('filter', null)
const camel =
  option('camel', null) ??
  join(
    process.env.CAMEL_HOME ?? join(repoRoot, 'out', 'latest'),
    'bin',
    process.platform === 'win32' ? 'camel.exe' : 'camel'
  )

if (!existsSync(camel)) {
  console.error(`print-diff-sweep: no camel binary at ${camel} (pass --camel or set CAMEL_HOME)`)
  process.exit(2)
}

/// Directories the sweep reads. Everything else in the tree (build output, docs images) is noise.
const scanRoots = ['test/cases', 'exemplars', 'stdlib']
/// Files that print. Read as text: a file that mentions `println` only in a comment prints
/// nothing, which costs one extra run and no answer.
const prints = (path) => /println/.test(readFileSync(path, 'utf8'))

const walk = (dir, out) => {
  let entries
  try {
    entries = readdirSync(dir)
  } catch {
    return out
  }
  for (const entry of entries.sort()) {
    const path = join(dir, entry)
    let info
    try {
      info = statSync(path)
    } catch {
      continue
    }
    if (info.isDirectory()) {
      walk(path, out)
    } else if (entry.endsWith('.cml') && prints(path)) {
      out.push(path)
    }
  }
  return out
}

const registryPath = join(here, 'print-diff-sweep.exceptions.txt')
const registry = new Map()
if (existsSync(registryPath)) {
  const lines = readFileSync(registryPath, 'utf8').split('\n')
  for (const [index, line] of lines.entries()) {
    if (line.trim() === '' || line.trimStart().startsWith('#')) {
      continue
    }
    const [path, ...rest] = line.split('\t')
    const reason = rest.join('\t').trim()
    if (!reason) {
      console.error(
        `print-diff-sweep: ${registryPath}:${index + 1} registers ${path.trim()} without a reason`
      )
      process.exit(2)
    }
    registry.set(path.trim(), reason)
  }
}

/// Runs the program and returns its stdout, exit code and how it ended.
const runOnce = (program, passes, timeoutMs) =>
  new Promise((done) => {
    const child = spawn(camel, [program, ...passes], { cwd: repoRoot })
    let out = ''
    let err = ''
    let settled = false
    const finish = (value) => {
      if (!settled) {
        settled = true
        done(value)
      }
    }
    const timer = setTimeout(() => {
      child.kill('SIGKILL')
      finish({ timedOut: true, code: null, stdout: out, stderr: err })
    }, timeoutMs)
    child.stdout.on('data', (chunk) => (out += chunk))
    child.stderr.on('data', (chunk) => (err += chunk))
    child.on('error', (error) => {
      clearTimeout(timer)
      finish({ timedOut: false, code: null, stdout: out, stderr: String(error) })
    })
    child.on('close', (code) => {
      clearTimeout(timer)
      finish({ timedOut: false, code, stdout: out, stderr: err })
    })
  })

const normalize = (text) =>
  text
    .replace(/\r\n?/g, '\n')
    .replace(/\u001b\[[0-9;]*m/g, '')
    .split('\n')
    .map((line) => line.replace(/[ \t]+$/, ''))
    .join('\n')

const lineCount = (text) => normalize(text).split('\n').filter((line) => line !== '').length

/// One file, both runs, one verdict.
const compareOne = async (program) => {
  const plain = await runOnce(program, ['std::nvm'], 20000)
  const optimized = await runOnce(program, ['std::opt', 'std::nvm'], 20000)
  const name = relative(root, program).split('\\').join('/')

  if (plain.timedOut || optimized.timedOut) {
    return { name, verdict: 'skipped', reason: 'timeout' }
  }
  if (plain.code === null || optimized.code === null) {
    return { name, verdict: 'skipped', reason: 'could not start' }
  }
  if (plain.code !== 0) {
    return { name, verdict: 'skipped', reason: `the unoptimized run exits ${plain.code}` }
  }
  if (optimized.code !== 0) {
    return { name, verdict: 'skipped', reason: `the optimized run exits ${optimized.code}` }
  }

  const before = normalize(plain.stdout)
  const after = normalize(optimized.stdout)
  if (before === after) {
    return { name, verdict: 'same', lines: lineCount(before) }
  }
  const beforeLines = lineCount(before)
  const afterLines = lineCount(after)
  if (afterLines < beforeLines) {
    return { name, verdict: 'fewer', lines: beforeLines, afterLines, before, after }
  }
  return { name, verdict: 'differs', lines: beforeLines, afterLines, before, after }
}

const files = []
for (const dir of scanRoots) {
  walk(join(root, dir), files)
}
const targets = filter ? files.filter((path) => relative(root, path).includes(filter)) : files
if (targets.length === 0) {
  console.error(`print-diff-sweep: no .cml file that prints under ${scanRoots.join(', ')}`)
  process.exit(2)
}

const results = []
let cursor = 0
const worker = async () => {
  for (;;) {
    const index = cursor++
    if (index >= targets.length) {
      return
    }
    results[index] = await compareOne(targets[index])
  }
}
await Promise.all(Array.from({ length: Math.min(jobs, targets.length) }, worker))

const buckets = { fewer: [], differs: [], same: [], skipped: [] }
for (const result of results) {
  buckets[result.verdict].push(result)
}
const registered = (result) => registry.has(result.name)

const printBucket = (title, entries, withReason) => {
  if (entries.length === 0) {
    return
  }
  console.log(`\n${title} (${entries.length})`)
  for (const entry of entries) {
    const counts =
      entry.verdict === 'skipped' ? `-- ${entry.reason}` : `${entry.lines} -> ${entry.afterLines ?? entry.lines} line(s)`
    console.log(`  ${entry.name}\t${counts}${withReason ? `\t${registry.get(entry.name)}` : ''}`)
    if (verbose && entry.before !== undefined) {
      const before = entry.before.split('\n').filter((line) => line !== '')
      const after = entry.after.split('\n').filter((line) => line !== '')
      const width = Math.max(before.length, after.length)
      for (let i = 0; i < width; i += 1) {
        if (before[i] !== after[i]) {
          console.log(`      line ${i + 1}: unoptimized ${JSON.stringify(before[i] ?? null)} | optimized ${JSON.stringify(after[i] ?? null)}`)
        }
      }
    }
  }
}

const unregisteredFewer = buckets.fewer.filter((entry) => !registered(entry))
const registeredFewer = buckets.fewer.filter(registered)
const unregisteredDiffers = buckets.differs.filter((entry) => !registered(entry))
const registeredDiffers = buckets.differs.filter(registered)

printBucket('prints fewer lines without a registered reason -- a dropped effect', unregisteredFewer, false)
printBucket('prints fewer lines, registered', registeredFewer, true)
printBucket('prints different text, same number of lines', unregisteredDiffers, false)
printBucket('prints different text, registered', registeredDiffers, true)
printBucket('could not be compared', buckets.skipped, false)

console.log(
  `\nprint-diff-sweep: ${targets.length} program(s) that print, ${jobs} at a time\n` +
    `  same ${buckets.same.length}, differs ${buckets.differs.length} (${unregisteredDiffers.length} unregistered), ` +
    `fewer ${buckets.fewer.length} (${unregisteredFewer.length} unregistered), skipped ${buckets.skipped.length}`
)

if (reportPath) {
  writeFileSync(
    resolve(reportPath),
    JSON.stringify(
      {
        camel,
        root,
        programs: targets.length,
        same: buckets.same.map((entry) => entry.name),
        differs: buckets.differs,
        fewer: buckets.fewer,
        skipped: buckets.skipped
      },
      null,
      2
    ) + '\n'
  )
  console.log(`  report: ${resolve(reportPath)}`)
}

process.exit(unregisteredFewer.length === 0 && unregisteredDiffers.length === 0 ? 0 : 1)