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
//     --red PLAN  the plans a run just showed red, for the reason this file is about: the dump is
//                 printed and the program runs behind it. Exit 3 when one of their case scripts
//                 writes a command whose last pass hands the graph on and the reader above cannot
//                 see that line -- the command is assembled inside a helper that does not name the
//                 binary on the line that writes the passes. Same class of answer as `--audit`:
//                 not "the tree is wrong" but "this file did not read it".
//     --blind     print the pipelines the reader's own rule cannot name: a line that writes the
//                 binary as `CAMEL` carries a pass word and is a command, and the rule above looks
//                 for `camel` or `EXE`, so none of these is in any count this file prints. Exit 3
//                 when one exists -- again the class of answer that is about the reading, not the
//                 tree. They are not reported as `need std::nop`: whether the program is meant to run
//                 after that pass is a question about each line (the cpp tool compiles and then runs
//                 what it compiled), so this mode names them and stops.
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
const redIndex = args.indexOf('--red')
const red = redIndex >= 0 ? args.slice(redIndex + 1).filter((a) => !a.startsWith('--')) : []
const blindMode = args.includes('--blind')

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
// `--red`: the reading above answers "is any pipeline I read undecided". It cannot answer "did I
// read the pipeline that is there", and the two answers look identical on screen when both are zero.
// A case script that assembles its command inside a helper --
// `run([str(paths[name]), "std::jgir", "canonical=true"])` -- names neither `camel` nor `EXE` on the
// line that writes the passes, so nothing above looks at it: measured on this tree, three case
// scripts and one tool of the `std::nop` patch were invisible that way, and the scan said
// `need std::nop: 0` while their plans were red. So the plans a run has just shown red are handed
// in, and the answer for each is in words: the scripts those plans run, how many pipelines were read
// in them, and every line whose last pass hands the graph on that the reader did not see.
//
// Exit 3 is `--audit`'s class of answer, not `--check`'s: not "the tree is wrong" but "this file did
// not read it, so its zero is not a reading". The rule is deliberately narrow -- the last pass on the
// line must hand the graph on -- because the point is to name what is invisible, not to guess at
// every line that mentions a pass word. Widening it until the number is non-zero would trade the one
// thing this mode is for (saying "I cannot see") for a number that looks like counting.
if (red.length > 0) {
  // The outermost `[...]` on the line, with its own `]` found by depth, so that
  // `run([str(paths["a"]), "std::jgir"])` is read as one argument list rather than as the `["a"]`
  // inside it -- the inner one is all strings and would hide the line's actual shape.
  const firstArgumentList = (line) => {
    const open = line.indexOf('[')
    if (open < 0) return null
    let depth = 0
    for (let i = open; i < line.length; i++) {
      if (line[i] === '[') depth++
      else if (line[i] === ']') {
        depth--
        if (depth === 0) return line.slice(open + 1, i)
      }
    }
    return null
  }
  const planFiles = []
  for (const target of red) {
    const resolved = path.resolve(root, target)
    if (!fs.existsSync(resolved)) {
      console.log(`red-plan blindness: ${target} -- no such plan`)
      process.exit(3)
    }
    if (fs.statSync(resolved).isDirectory()) {
      planFiles.push(...walk(resolved).filter((f) => f.endsWith('.plan.toml')))
    } else {
      planFiles.push(resolved)
    }
  }
  const blind = []
  // One entry per line, not one per plan: a tool such as `test/tools/cppexec/run.py` is the `case` of
  // a dozen plans, and a list that repeats the same line once per user of it reads as a bigger
  // blindness than there is. The count is of lines the reader cannot see, so it is counted once.
  const blindLines = new Set()
  console.log(`red-plan blindness: ${planFiles.length} plan(s) checked`)
  for (const planFile of planFiles) {
    const text = fs.readFileSync(planFile, 'utf8')
    const scripts = [...text.matchAll(/^case\s*=\s*"([^"]+)"/gm)]
      .map((m) => path.resolve(path.dirname(planFile), m[1]))
    let seen = 0
    for (const script of scripts) {
      if (!fs.existsSync(script)) continue
      const scriptText = fs.readFileSync(script, 'utf8')
      const read = new Set(scriptPipelines(script, scriptText).map((p) => p.line))
      seen += read.size
      scriptText.split('\n').forEach((line, index) => {
        const at = index + 1
        if (read.has(at)) return
        if (line.trim().startsWith('#')) return
        // A line that says `keep` is decided, and a decided line is not this mode's question.
        if (line.includes('# camel-pass-scan: keep')) return
        const strings = [...line.matchAll(/'([^']*)'|"([^"]*)"/g)].map((m) => m[1] ?? m[2])
        const passes = strings.flatMap(passesOfElement)
        if (passes.length === 0) return
        const last = passes[passes.length - 1]
        if (!TRANSLATE.has(last)) return
        // An argument list, not prose: `[...]` holding a pass word and at least one element that is
        // not a plain string -- `run([source, 'std::cml'])`. A diagnostic that mentions `std::cml`
        // (`assert x, "std::cml printed nothing"`), a parameter default (`passes=("std::cml",)`) and
        // a tuple handed to a helper that builds its own command are not command lines, and the
        // narrowness is the price of a mode whose whole job is naming what is really invisible.
        const listText = firstArgumentList(line)
        if (listText === null) return
        const elements = listText.split(',').map((s) => s.trim()).filter(Boolean)
        if (!elements.some((e) => !/^['"].*['"]$/.test(e))) return
        const key = `${rel(script)}:${at}`
        if (blindLines.has(key)) return
        blindLines.add(key)
        blind.push({ file: rel(script), line: at, last, text: line.trim() })
      })
    }
    console.log(`  ${rel(planFile)}  case scripts ${scripts.length}, pipelines read in them ${seen}`)
  }
  if (blind.length > 0) {
    console.log(`cannot see: ${blind.length} line(s) whose last pass hands the graph on`)
    for (const b of blind) console.log(`  ${b.file}:${b.line}  ${b.last}  ${b.text.slice(0, 120)}`)
    console.log('  these build the command inside a helper that does not name the binary on the line:')
    console.log('  `need std::nop: 0` is not a reading of them, and a red plan is what says so')
    process.exit(3)
  }
}
// `--blind`: the second way this file can report zero without having read anything is its own rule,
// which names the binary as `camel` or `EXE` on the line. The tree also writes `CAMEL`, and a line
// such as `run([CAMEL, file, 'std::nvm'])` is a command that nothing above counted -- measured on
// this tree, 18 of them in 7 files (`test/tools/cppexec/run.py`, `test/tools/import-audit.mjs`, and
// four case scripts). They are not added to `need std::nop`, because whether the program is meant to
// run after that pass is a question about each line: the cpp tool compiles and then runs what it
// compiled, so its last pass handing the graph on is the point of the tool. Naming them is the
// honest reading, and it keeps `need std::nop: 0` from meaning two different things.
if (blindMode) {
  const unread = []
  for (const dir of ['test/cases', 'test/tools', 'benchmarks', 'test/plans']) {
    for (const file of walk(path.join(root, dir))) {
      if (!/\.(mjs|js|py|ps1|toml)$/.test(file)) continue
      fs.readFileSync(file, 'utf8').split('\n').forEach((line, index) => {
        // A line the rule already reads is not unread, whatever else it spells.
        if (!/\bCAMEL\b/.test(line) || /\bcamel\b|\bEXE\b/.test(line)) return
        const strings = [...line.matchAll(/'([^']*)'|"([^"]*)"/g)].map((m) => m[1] ?? m[2])
        const passes = strings.flatMap(passesOfElement)
        if (passes.length === 0) return
        const last = passes[passes.length - 1]
        unread.push({
          file: rel(file), line: index + 1, last, handsOn: TRANSLATE.has(last), text: line.trim(),
        })
      })
    }
  }
  // Ordered by what the line does with the graph, and counted that way too. On 2026-11-24 one of
  // these was a real defect -- `test/cases/opt/pass_written/check.mjs` counted a JSON document whose
  // programs then ran, and the plan was red -- and it was one `std::jgir` among seventeen `std::nvm`
  // and friends. A list that does not separate them is read as either all-scary or all-noise, and
  // both readings lose the one line that mattered.
  const handsOn = unread.filter((u) => u.handsOn)
  const rest = unread.filter((u) => !u.handsOn)
  console.log(`unread: ${unread.length} line(s) name the binary as \`CAMEL\` and carry a pass word` +
    `, of which ${handsOn.length} end in a pass that hands the graph on`)
  for (const u of [...handsOn, ...rest]) {
    console.log(`  ${u.handsOn ? 'HANDS-ON' : 'runs    '}  ${u.file}:${u.line}  ${u.last}  ${u.text.slice(0, 100)}`)
  }
  if (unread.length > 0) {
    console.log('  the rule names `camel` and `EXE`, so none of these is inside the total above')
    console.log('  HANDS-ON is where a `std::nop` question could live; `runs` is a program the line meant to run')
    process.exit(3)
  }
}
// The gate answers the three conditions, not one: a pipeline is undone only when the last pass hands
// the graph on (the scan's rule), the case says nothing about the run (the keep lists), and nothing
// after it needs the graph. So the red count is `needsNop`, which is already what is left after both
// keep decisions, plus a stale entry -- a keep that no longer names a line is a decision nobody read.
if (check) process.exit(needsNop.length === 0 && staleKeep.length === 0 ? 0 : 1)
if (audit && differences.length > 0) process.exit(3)