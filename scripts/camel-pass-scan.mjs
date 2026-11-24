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
//   node scripts/camel-pass-scan.mjs [--root DIR] [--list] [--check]
//
//     (no flag)   one summary: how many pipelines end in a translating pass, how many need
//                 `std::nop`, how many already hand the graph on, and a count per pass
//     --list      the same, then one line per pipeline: file:line, the passes, and the verdict
//     --check     exit 0 when no pipeline needs `std::nop`, 1 when one does, 2 when the tree could
//                 not be read; the gate entry, for whoever wires it
//
// What counts as a pipeline: every `args = [...]` array in test/plans/**/*.plan.toml (a plan test's
// command line), and every `[camel, path, 'std::x']` style call under test/tools and benchmarks. A
// pass word is the first whitespace-separated word of a token that looks like a pass (`std::...`,
// `onnx::...`); its parameters follow it, which is why `std::jgir canonical=true` is one pass.
import fs from 'node:fs'
import path from 'node:path'

const args = process.argv.slice(2)
const rootIndex = args.indexOf('--root')
const root = path.resolve(rootIndex >= 0 ? args[rootIndex + 1] : process.cwd())
const list = args.includes('--list')
const check = args.includes('--check')

// A pass whose job is done by printing or exporting: it hands the graph on.
const TRANSLATE = new Set([
  'std::gir', 'std::rgir', 'std::gir_regions', 'std::rgir_regions',
  'std::agir', 'std::argir', 'std::jgir',
  'std::cml', 'std::cpp', 'std::cpp::module_file', 'std::cpp::inspect',
  'std::topo_node_seq', 'std::tfdump',
  'std::fastvm::bytecode', 'std::fastvm::linked_bytecode',
  'std::fastvm::jit_asm', 'std::fastvm::jit_bin',
  'std::fastvm::jit_rmir', 'std::fastvm::jit_mir',
  'onnx::export',
])
// The passes that print and hand the graph on without being turned around by this model: a pipeline
// that ends in one of them never executed the program to begin with.
const ALREADY_HANDS_ON = new Set(['std::stats', 'std::rulesets', 'std::arraytype', 'std::identitytype'])
const ALIASES = {
  'std::lnr': 'std::fvm', 'std::linear': 'std::nvm', 'std::nodevm': 'std::nvm',
  'std::default': 'std::nvm', 'std::jit': 'std::fastvm::jit', 'std::tf': 'std::taskflow',
  'std::prl': 'std::taskflow', 'std::parallel': 'std::taskflow', 'std::tfg': 'std::tfdump',
  'std::tns': 'std::topo_node_seq', 'std::bc': 'std::fastvm::bytecode',
  'std::lbc': 'std::fastvm::linked_bytecode', 'std::asm': 'std::fastvm::jit_asm',
  'std::bin': 'std::fastvm::jit_bin', 'std::rmir': 'std::fastvm::jit_rmir',
  'std::mir': 'std::fastvm::jit_mir', 'std::cppmod': 'std::cpp::module_file',
  'std::cppinspect': 'std::cpp::inspect',
}
const PASSY = /^(?:std|test|onnx|tiles|json|re|nn|tensor)::/
const canon = (word) => { const name = word.trim().split(/\s+/)[0]; return ALIASES[name] ?? name }

function walk(dir, out = []) {
  let entries
  try { entries = fs.readdirSync(dir, { withFileTypes: true }) } catch { return out }
  for (const entry of entries) {
    const full = path.join(dir, entry.name)
    if (entry.isDirectory()) {
      if (entry.name === 'node_modules' || entry.name === 'out' || entry.name === '.git') continue
      walk(full, out)
    } else out.push(full)
  }
  return out
}

// Every command line this tree writes, as { file, line, passes }.
function pipelines() {
  const found = []
  for (const file of walk(path.join(root, 'test/plans'))) {
    if (!file.endsWith('.plan.toml')) continue
    const text = fs.readFileSync(file, 'utf8')
    const re = /args\s*=\s*\[([\s\S]*?)\]/g
    let m
    while ((m = re.exec(text))) {
      const tokens = [...m[1].matchAll(/"([^"]*)"/g)].map((x) => x[1])
      const passes = tokens.filter((t) => PASSY.test(t)).map(canon)
      if (passes.length === 0) continue
      const line = text.slice(0, m.index).split('\n').length
      // A pipeline that ends in a translating pass on purpose says so on its own line, so the check
      // can be a gate without reading intent out of the passes: # camel-pass-scan: keep.
      const lineEnd = text.indexOf('\n', m.index + m[0].length)
      const tail = text.slice(m.index, lineEnd < 0 ? text.length : lineEnd)
        const keep = tail.indexOf('# camel-pass-scan: keep')
        if (keep >= 0) {
            // Listed, never just skipped: an exception nobody prints becomes the rule with age.
            kept.push({ file, line, passes, why: tail.slice(keep).trim() })
            continue
        }
      found.push({ file, line, passes })
    }
  }
  for (const dir of ['test/tools', 'benchmarks']) {
    for (const file of walk(path.join(root, dir))) {
      if (!/\.(mjs|js|py)$/.test(file)) continue
      const lines = fs.readFileSync(file, 'utf8').split('\n')
      lines.forEach((text, index) => {
        // A command line, not a sentence about one: it runs camel and names a pass.
        if (!/\bcamel\b/.test(text)) return
        const words = [...text.matchAll(/'([^']*)'|"([^"]*)"/g)].map((x) => x[1] ?? x[2])
        const passes = words.filter((t) => PASSY.test(t)).map(canon)
        if (passes.length === 0) return
        if (!passes.some((p) => TRANSLATE.has(p))) return
        found.push({ file, line: index + 1, passes })
      })
    }
  }
  return found
}

const kept = []
const all = pipelines()
const ending = all.filter((p) => TRANSLATE.has(p.passes[p.passes.length - 1]))
const needsNop = ending.filter((p) => p.passes[p.passes.length - 1] !== 'std::nop')
const handsOn = ending.filter((p) => ALREADY_HANDS_ON.has(p.passes[p.passes.length - 1]))
// A pipeline that ends in one of these prints and executes nothing: it never needed std::nop, and
// counting it here is how "already hands the graph on" is a number instead of an impression.
const alreadyOnes = all.filter((p) => ALREADY_HANDS_ON.has(p.passes[p.passes.length - 1]))
const byPass = {}
for (const p of ending) {
  const name = p.passes[p.passes.length - 1]
  byPass[name] = (byPass[name] ?? 0) + 1
}
const rel = (file) => path.relative(root, file).replaceAll('\\', '/')
const plans = ending.filter((p) => p.file.endsWith('.plan.toml')).length
const scripts = ending.length - plans

console.log(`pipelines read: ${all.length}`)
console.log(`ending in a translating pass: ${ending.length} (plans ${plans}, scripts ${scripts})`)
console.log(`  already hand the graph on: ${alreadyOnes.length} (${[...ALREADY_HANDS_ON].join(', ')})`)
console.log(`  need std::nop: ${needsNop.length}`)
console.log(`by pass: ${Object.entries(byPass).sort().map(([k, v]) => `${k}=${v}`).join(' ')}`)
console.log(`kept by '# camel-pass-scan: keep': ${kept.length}`)
for (const p of kept) console.log(`  ${rel(p.file)}:${p.line}  ${p.passes.join(' ')}  -- ${p.why}`)
if (list) {
  for (const p of needsNop) console.log(`  ${rel(p.file)}:${p.line}  ${p.passes.join(' ')}`)
}
if (check) process.exit(needsNop.length === 0 ? 0 : 1)
