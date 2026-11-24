// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// How a command line in this tree is read, in one place, for scripts/camel-pass-scan.mjs (which
// reports) and scripts/camel-pass-nop-patch.mjs (which writes). Two readers that agreed by luck is
// how 35 single-quoted `onnx::export` pipelines were counted as "no translating pass here": the
// scan's rule and the patch's rule are the same rule, so they are one function.
//
// An element of a plan's `args` array is written one of three ways and all three are the same
// command line:
//
//   args = ["${case}", "std::gir"]                            double-quoted
//   args = ["${case}", 'onnx::export out="${test_tmp}/m"']    single-quoted (a literal string)
//   args = ["${case}", std::jgir]                             bare word (not TOML, read anyway)
//
// A pass word is the first whitespace- or comma-separated word of an element that looks like a pass;
// its parameters follow it (`std::jgir canonical=true` is one pass), a repeat suffix is dropped
// (`std::nvm/10` is `std::nvm`), and a comma-joined list carries one pass per comma
// (`"std::opt safe_fp=false,std::fvm"`, the `--passes` spelling).

import fs from 'node:fs'
import path from 'node:path'

export const TRANSLATE = new Set([
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
export const ALREADY_HANDS_ON = new Set(['std::stats', 'std::rulesets', 'std::arraytype', 'std::identitytype'])

export const ALIASES = {
  'std::lnr': 'std::fvm', 'std::linear': 'std::nvm', 'std::nodevm': 'std::nvm',
  'std::default': 'std::nvm', 'std::jit': 'std::fastvm::jit', 'std::tf': 'std::taskflow',
  'std::prl': 'std::taskflow', 'std::parallel': 'std::taskflow', 'std::tfg': 'std::tfdump',
  'std::tns': 'std::topo_node_seq', 'std::bc': 'std::fastvm::bytecode',
  'std::lbc': 'std::fastvm::linked_bytecode', 'std::asm': 'std::fastvm::jit_asm',
  'std::bin': 'std::fastvm::jit_bin', 'std::rmir': 'std::fastvm::jit_rmir',
  'std::mir': 'std::fastvm::jit_mir', 'std::cppmod': 'std::cpp::module_file',
  'std::cppinspect': 'std::cpp::inspect',
}

export const PASSY = /^(?:std|test|onnx|tiles|json|re|nn|tensor)::/

// One pass word as the pipeline reads it.
export function passOf(word) {
  const name = word.trim().split(/[\s,]/)[0].replace(/\/\d+$/, '')
  return ALIASES[name] ?? name
}

// The pass words an element writes: a comma-joined list carries one per comma.
export function passesOfElement(element) {
  const out = []
  for (const part of element.split(',')) {
    const word = part.trim().split(/\s+/)[0]
    if (word && PASSY.test(word)) out.push(passOf(word))
  }
  return out
}

// The pass words read straight out of quoted strings, without tokenising: the audit's second
// reader, which must not share a bug with the parser above.
export function leadingPasses(strings) {
  return strings.flatMap(passesOfElement)
}

export function walk(dir, out = []) {
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

// Every `args = [ ... ]` array in a plan, found with quotes in mind: a `]` inside a literal string
// (`'onnx::export out="x[1].onnx"'`) does not end the array.
export function argsArrays(text) {
  const found = []
  const re = /args\s*=\s*\[/g
  let m
  while ((m = re.exec(text))) {
    const start = m.index + m[0].length
    let i = start
    let depth = 1
    let quote = null
    while (i < text.length) {
      const c = text[i]
      if (quote) {
        if (quote === '"' && c === '\\') { i += 2; continue }
        if (c === quote) quote = null
        i++
        continue
      }
      if (c === '"' || c === "'") { quote = c; i++; continue }
      if (c === '[') depth++
      if (c === ']') { depth--; if (depth === 0) break }
      i++
    }
    found.push({ at: m.index, close: i, body: text.slice(start, i) })
    re.lastIndex = i + 1
  }
  return found
}

// The elements of one array, as a TOML reader hands them to the caller: both string forms unwrapped,
// a bare word kept as written, comments and separators dropped.
export function elementsOf(body) {
  const out = []
  let i = 0
  while (i < body.length) {
    const c = body[i]
    if (c === '#') {
      while (i < body.length && body[i] !== '\n') i++
      continue
    }
    if (c === ',' || /\s/.test(c)) { i++; continue }
    if (c === '"' || c === "'") {
      const quote = c
      let j = i + 1
      let text = ''
      while (j < body.length) {
        if (quote === '"' && body[j] === '\\' && j + 1 < body.length) { text += body[j + 1]; j += 2; continue }
        if (body[j] === quote) break
        text += body[j]
        j++
      }
      out.push(text)
      i = j + 1
      continue
    }
    let j = i
    while (j < body.length && !/[,\s]/.test(body[j])) j++
    out.push(body.slice(i, j))
    i = j
  }
  return out
}

// The quoted strings of one array, by regex rather than by scanner: the audit's second reading.
export function stringsOf(body) {
  return [...body.matchAll(/"((?:[^"\\]|\\.)*)"|'([^']*)'/g)].map((m) => m[1] ?? m[2])
}

// The command line of a script file, line by line: the quoted strings on a line that names the
// binary. `EXE` is how a case names the binary when it resolved one (`test/tools/camel_arg.py`), and
// a pipeline written that way runs a program just the same.
export function scriptPipelines(file, text) {
  const out = []
  text.split('\n').forEach((line, index) => {
    if (!/\bcamel\b|\bEXE\b/.test(line)) return
    const strings = [...line.matchAll(/'([^']*)'|"([^"]*)"/g)].map((m) => m[1] ?? m[2])
    const passes = strings.flatMap(passesOfElement)
    if (passes.length === 0) return
    out.push({ file, line: index + 1, text: line, passes })
  })
  return out
}