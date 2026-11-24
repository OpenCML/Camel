// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The other half of scripts/camel-pass-scan.mjs: it puts `std::nop` at the end of every plan pipeline
// the scan reports, so a pipeline that only prints does not fall through to the default scheduling
// pass and execute a program its expectation says nothing about (design principles 9.1/9.2).
//
//   node scripts/camel-pass-nop-patch.mjs [--root DIR] [--apply]
//
//     (no flag)   what it would change: the count and one line per plan file
//     --apply     write the files
//
// It touches test/plans only. A script under test/tools or benchmarks that runs `camel <case> std::gir`
// is left for a person: whether the program may run is a question about the caller, not a text edit
// the pass list answers. A pipeline that ends in a translating pass on purpose carries
// `# camel-pass-scan: keep` on its line and is skipped here and by the check.
//
// The rule is the scan's rule, deliberately shared rather than re-derived: pass word = first
// whitespace-separated word of a token that looks like a pass, aliases canonicalised, the last word
// decides.
import fs from 'node:fs'
import path from 'node:path'

const args = process.argv.slice(2)
const rootIndex = args.indexOf('--root')
const root = path.resolve(rootIndex >= 0 ? args[rootIndex + 1] : process.cwd())
const apply = args.includes('--apply')

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
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name)
    if (entry.isDirectory()) {
      if (entry.name === 'node_modules' || entry.name === 'out' || entry.name === '.git') continue
      walk(full, out)
    } else out.push(full)
  }
  return out
}

let patched = 0
const kept = []
const files = walk(path.join(root, 'test/plans')).filter((f) => f.endsWith('.plan.toml'))
for (const file of files) {
  const text = fs.readFileSync(file, 'utf8')
  const edits = []
  const re = /args\s*=\s*\[([\s\S]*?)\]/g
  let m
  while ((m = re.exec(text))) {
    const lineEnd = text.indexOf('\n', m.index + m[0].length)
    const tail = text.slice(m.index, lineEnd < 0 ? text.length : lineEnd)
    const keep = tail.indexOf('# camel-pass-scan: keep')
    if (keep >= 0) {
        // Listed, never just skipped: the exception is what a reader of this output needs to see.
        kept.push({ file, line: text.slice(0, m.index).split('\n').length, why: tail.slice(keep).trim() })
        continue
    }
    const tokens = [...m[1].matchAll(/"([^"]*)"/g)].map((x) => x[1])
    const passes = tokens.filter((t) => PASSY.test(t)).map(canon)
    if (passes.length === 0) continue
    if (!TRANSLATE.has(passes[passes.length - 1])) continue
    // Insert before the `]` that closes this array, keeping the array's own layout: a one-line array
    // grows by `, "std::nop"`, a multi-line one gets the new element on its own line, indented like
    // the element before it.
    const close = m.index + m[0].length - 1
    let back = close - 1
    while (back >= m.index && /\s/.test(text[back])) back--
    const indentMatch = /(?:^|\n)([ \t]*)[^\n]*$/.exec(m[1])
    const indent = indentMatch ? indentMatch[1] : '  '
    const insert = text.slice(back + 1, close).includes('\n') ? `,\n${indent}"std::nop"` : ', "std::nop"'
    edits.push({ at: close, insert })
  }
  if (edits.length === 0) continue
  let next = text
  for (const e of edits.sort((a, b) => b.at - a.at)) next = next.slice(0, e.at) + e.insert + next.slice(e.at)
  patched += edits.length
  const rel = path.relative(root, file).replaceAll('\\', '/')
  console.log(`  ${apply ? 'patched' : 'would patch'} ${edits.length}: ${rel}`)
  if (apply) fs.writeFileSync(file, next)
}
console.log(`${apply ? 'patched' : 'would patch'} ${patched} pipeline(s) in ${files.length} plan file(s)`)
console.log(`kept by '# camel-pass-scan: keep': ${kept.length}`)
for (const k of kept) console.log(`  ${path.relative(root, k.file).replaceAll('\\', '/')}:${k.line}  -- ${k.why}`)
