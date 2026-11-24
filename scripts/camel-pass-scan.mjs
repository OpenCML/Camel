// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Which pipeline in this tree ends in a translating pass, and does it say what happens after it?
//
// A rewriting pass returns a rewritten graph; a translating pass prints or exports and hands the
// graph on (design principles 9.1, docs/design-principles.cn.md:636); a scheduling pass executes the
// program and answers with the residual graph or an empty one. The pipeline has one rule: the passes
// written run in order, and when the graph is still there at the end it goes to `std::default`
// (9.2). To look without running, the list ends in `std::nop`.
//
// So a plan pipeline whose last pass translates runs the program unless it says `std::nop` -- and a
// case that only compares a dump usually says nothing about the program's output. This script finds
// those. It is the measurement behind the `std::nop` patches; it is not a rule the suite enforces by
// itself.
//
//   node scripts/camel-pass-scan.mjs [--root DIR] [--list] [--check] [--audit]
//
//     (no flag)   one summary: how many pipelines end in a translating pass, how many need
//                 `std::nop`, how many already hand the graph on, and a count per pass
//     --list      the same, then one line per pipeline: file:line, the passes, and the verdict
//     --check     exit 0 when no pipeline is left undecided, 1 when one is -- a pipeline that the
//                 reader's rule names (the last pass hands the graph on) and that is neither
//                 patched nor listed in a keep list; a stale keep entry also refuses. The gate
//                 answers the three conditions below together, not the first one alone
//     --audit     compare the pass words this script parsed with the same words read by a second,
//                 independent reader (a quoted-string regex, which is how the bug this file once had
//                 would be written). Exit 3 when a pass word an element writes is missing from the
//                 parse. The reading is a set difference on words, never a count: two totals that
//                 agree by luck are what missed 35 single-quoted `onnx::export` pipelines.
//
// The reader itself lives in scripts/camel-pass-pipelines.mjs, shared with the patch script so the
// two cannot drift. It reads an element written each of the three ways a TOML array allows --
// double-quoted, single-quoted literal, bare word -- plus the two spellings that carry more than one
// pass: a comma-joined list (`"std::opt safe_fp=false,std::fvm"`) and a repeat suffix
// (`std::nvm/10` is `std::nvm`).
//
// Before appending `std::nop` to a pipeline, all three of these must hold. They are checkboxes rather
// than a sentence because "the last pass translates, so append" is exactly the shorthand that got
// this wrong before.
//
//   [ ] the last written pass hands the graph on -- a translating or a rewriting pass, not a
//       scheduling one -- and is not already `std::nop`; the scan decides this, a person does not
//   [ ] the case says nothing about the program's own output or exit code: after `std::nop` the
//       program does not run, so an expectation that only holds while it runs is now untested
//   [ ] the pass's own artefacts (files, dumps, diagnostics) are produced before the `std::nop`, and
//       no pass after it needs the graph -- `... std::nop std::gir` never prints anything
//
// A pipeline that ends in a translating pass on purpose says so on its own line and is listed, never
// silently skipped: `# camel-pass-scan: keep`.
//
// The case scripts below match the reader's rule and must still not get a `std::nop`, so they are
// listed with the reason instead of looking like an oversight. Each runs `camel <case> <passes>`
// where the last pass prints a dump; nothing then consumes the graph, so the program runs as well,
// and the case's expectation is written against that whole run -- its stdout and its exit code.
// Whether that run is load-bearing is a question about each case, so each line is decided on its
// own: the entries live in `scripts/camel-pass-keep.mjs`, one `{ file, line, why }` per line, and
// this file reads them rather than repeating them.
//
// `--check` is green on those and red on anything not decided, and a line number that no longer
// matches a pipeline is reported as stale: an exception nobody re-reads becomes the rule with age.
import { KEPT_CASE_SCRIPTS } from './camel-pass-keep.mjs'
import fs from 'node:fs'
import path from 'node:path'
import {
  TRANSLATE, ALREADY_HANDS_ON, argsArrays, elementsOf, stringsOf, leadingPasses,
  passesOfElement, scriptPipelines, walk,
} from './camel-pass-pipelines.mjs'

const args = process.argv.slice(2)
const rootIndex = args.indexOf('--root')
const root = path.resolve(rootIndex >= 0 ? args[rootIndex + 1] : process.cwd())
const list = args.includes('--list')
const check = args.includes('--check')
const audit = args.includes('--audit')

// Every command line this tree writes, as { file, line, passes }.
function pipelines() {
  const found = []
  const differences = []
  for (const file of walk(path.join(root, 'test/plans'))) {
    if (!file.endsWith('.plan.toml')) continue
    const text = fs.readFileSync(file, 'utf8')
    for (const array of argsArrays(text)) {
      const elements = elementsOf(array.body)
      const passes = elements.flatMap(passesOfElement)
      if (passes.length === 0) continue
      const line = text.slice(0, array.at).split('\n').length
      const lineEnd = text.indexOf('\n', array.close)
      const tail = text.slice(array.at, lineEnd < 0 ? text.length : lineEnd)
      for (const word of leadingPasses(stringsOf(array.body))) {
        if (!passes.includes(word)) differences.push({ file, line, word, passes })
      }
      // A pipeline that ends in a translating pass on purpose says so on its own line, so the check
      // can be a gate without reading intent out of the passes: # camel-pass-scan: keep.
      const keep = tail.indexOf('# camel-pass-scan: keep')
      if (keep >= 0) {
        // Listed, never just skipped: an exception nobody prints becomes the rule with age.
        kept.push({ file, line, passes, why: tail.slice(keep).trim() })
        continue
      }
      found.push({ file, line, passes })
    }
  }
  // `test/cases` is where a plan's case scripts live, and a pipeline written there runs a program
  // just as much as one in `args`: `tiles/public_pass_graph.py` dumps with `std::jgir` and then read
  // the program's own output as JSON.
  for (const dir of ['test/tools', 'benchmarks', 'test/cases']) {
    for (const file of walk(path.join(root, dir))) {
      if (!/\.(mjs|js|py|ps1)$/.test(file)) continue
      for (const p of scriptPipelines(file, fs.readFileSync(file, 'utf8'))) {
        const strings = [...p.text.matchAll(/'([^']*)'|"([^"]*)"/g)].map((m) => m[1] ?? m[2])
        for (const word of leadingPasses(strings)) {
          if (!p.passes.includes(word)) differences.push({ file, line: p.line, word, passes: p.passes })
        }
        if (!p.passes.some((x) => TRANSLATE.has(x))) continue
        // A line that runs a translating pass and then the program says so on its own line.
        if (p.text.includes('# camel-pass-scan: keep')) {
          kept.push({ file, line: p.line, passes: p.passes, why: p.text.slice(p.text.indexOf('# camel-pass-scan: keep')).trim() })
          continue
        }
        // A case script whose decision is written down in `scripts/camel-pass-keep.mjs`: listed
        // with that entry's own reason, never silently skipped.
        const key = `${relFile(file)}:${p.line}`
        const decision = KEPT_CASE_SCRIPTS.find((k) => `${k.file}:${k.line}` === key)
        if (decision) {
          kept.push({ file, line: p.line, passes: p.passes, why: decision.why })
          continue
        }
        found.push(p)
      }
    }
  }
  return { found, differences }
}

const relFile = (file) => path.relative(root, file).replaceAll('\\', '/')

const kept = []
const { found: all, differences } = pipelines()
const ending = all.filter((p) => TRANSLATE.has(p.passes[p.passes.length - 1]))
const needsNop = ending.filter((p) => p.passes[p.passes.length - 1] !== 'std::nop')
const alreadyOnes = all.filter((p) => ALREADY_HANDS_ON.has(p.passes[p.passes.length - 1]))
const byPass = {}
for (const p of ending) {
  const name = p.passes[p.passes.length - 1]
  byPass[name] = (byPass[name] ?? 0) + 1
}
const rel = (file) => path.relative(root, file).replaceAll('\\', '/')
const plans = ending.filter((p) => p.file.endsWith('.plan.toml')).length
const scripts = ending.length - plans
// A keep entry that no longer names a pipeline is a decision about a line that is gone: reported,
// and the gate refuses, so the list cannot rot into permission.
const keptKeys = new Set(kept.map((k) => `${rel(k.file)}:${k.line}`))
const staleKeep = KEPT_CASE_SCRIPTS.filter((k) => !keptKeys.has(`${k.file}:${k.line}`))

console.log(`pipelines read: ${all.length}`)
console.log(`ending in a translating pass: ${ending.length} (plans ${plans}, scripts ${scripts})`)
console.log(`  already hand the graph on: ${alreadyOnes.length} (${[...ALREADY_HANDS_ON].join(', ')})`)
console.log(`  need std::nop: ${needsNop.length}`)
console.log(`by pass: ${Object.entries(byPass).sort().map(([k, v]) => `${k}=${v}`).join(' ')}`)
console.log(`kept (each with its own reason, in scripts/camel-pass-keep.mjs): ${kept.length}`)
for (const p of kept) console.log(`  ${rel(p.file)}:${p.line}  ${p.passes.join(' ')}  -- ${p.why}`)
if (staleKeep.length > 0) {
  console.log(`stale keep entr${staleKeep.length === 1 ? 'y' : 'ies'}: ${staleKeep.length}`)
  for (const k of staleKeep) console.log(`  ${k.file}:${k.line}  -- names no pipeline any more`)
}
if (list) {
  for (const p of needsNop) console.log(`  ${rel(p.file)}:${p.line}  ${p.passes.join(' ')}`)
}
if (audit) {
  // The set difference between the words the files write and the words this script parsed. Empty is
  // the only passing reading.
  console.log(`audit: ${differences.length} pass word(s) written but not parsed`)
  for (const d of differences) {
    console.log(`  ${rel(d.file)}:${d.line}  '${d.word}' not in [${d.passes.join(' ')}]`)
  }
}
// The gate answers the three conditions, not one: a pipeline is undone only when the last pass hands
// the graph on (the scan's rule), the case says nothing about the run (the keep lists), and nothing
// after it needs the graph. So the red count is `needsNop`, which is already what is left after both
// keep decisions, plus a stale entry -- a keep that no longer names a line is a decision nobody read.
if (check) process.exit(needsNop.length === 0 && staleKeep.length === 0 ? 0 : 1)
if (audit && differences.length > 0) process.exit(3)