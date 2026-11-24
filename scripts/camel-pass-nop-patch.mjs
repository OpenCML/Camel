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
// It touches test/plans only. A script under test/tools, test/cases or benchmarks that runs
// `camel <case> std::gir` is left for a person: whether the program may run is a question about the
// caller, not a text edit the pass list answers. A pipeline that ends in a translating pass on
// purpose carries `# camel-pass-scan: keep` on its line and is skipped here and by the check.
//
// The rule is the scan's rule, in one place rather than re-derived: scripts/camel-pass-pipelines.mjs
// reads the elements of an `args` array (double-quoted, single-quoted literal, bare word) and the
// pass words in them; the last word decides.
import fs from 'node:fs'
import path from 'node:path'
import {
  TRANSLATE, argsArrays, elementsOf, passesOfElement, walk,
} from './camel-pass-pipelines.mjs'

const args = process.argv.slice(2)
const rootIndex = args.indexOf('--root')
const root = path.resolve(rootIndex >= 0 ? args[rootIndex + 1] : process.cwd())
const apply = args.includes('--apply')

let patched = 0
const kept = []
const files = walk(path.join(root, 'test/plans')).filter((f) => f.endsWith('.plan.toml'))
for (const file of files) {
  const text = fs.readFileSync(file, 'utf8')
  const edits = []
  for (const array of argsArrays(text)) {
    const lineEnd = text.indexOf('\n', array.close)
    const tail = text.slice(array.at, lineEnd < 0 ? text.length : lineEnd)
    const keep = tail.indexOf('# camel-pass-scan: keep')
    if (keep >= 0) {
      // Listed, never just skipped: the exception is what a reader of this output needs to see.
      kept.push({ file, line: text.slice(0, array.at).split('\n').length, why: tail.slice(keep).trim() })
      continue
    }
    const passes = elementsOf(array.body).flatMap(passesOfElement)
    if (passes.length === 0) continue
    if (!TRANSLATE.has(passes[passes.length - 1])) continue
    // Insert before the `]` that closes this array, keeping the array's own layout: a one-line array
    // grows by `, "std::nop"`, a multi-line one gets the new element on its own line, indented like
    // the element before it.
    const close = array.close
    let back = close - 1
    while (back >= array.at && /\s/.test(text[back])) back--
    const indentMatch = /(?:^|\n)([ \t]*)[^\n]*$/.exec(array.body)
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