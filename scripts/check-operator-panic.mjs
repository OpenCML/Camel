#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * `panic` states whether a call of an operator can fail, and a declaration that leaves it out
 * promises that no call fails (design principles 3.9, and line 165 for the rule this check is:
 * within the repository's standard library a check compares that promise with the implementation).
 * This is that check.
 *
 * --- what the two sides are -------------------------------------------------
 * The **declaration** side is where an operator says `panic`:
 *
 *   1. a Camel declaration of an operator, `#op('<uri>')` and the `export func` line under it, the
 *      keyword written beside `pure` and `uses` (`lib/**\/*.cml`);
 *   2. the C++ entrance, `CppRegistration`'s second argument, which is required and has no default
 *      (`kernel/ops/include/camel/ops/operator_record.h`).
 *
 * The declarations state it now: `d39cd2464` wrote it on 42 operators, and the two scatters
 * `lib/rulesets/transpose.cml` declares privately take the count to 44 of the 378 this run reads. So
 * what the red side measures is the size of the remaining debt, not a regression. The promise a
 * declaration without `panic` makes is the reason the debt matters: "no `panic`" means "this call
 * cannot fail", and an optimizer may move a division out of a guard on the strength of it.
 *
 * The **implementation** side is the operator's kernel, found through the implementation map that
 * registers it (`{"op/div_i", __builtin__div__}`, `{"split", __str_split__}`, ...), together with
 * the helpers defined beside that kernel in the same translation unit -- a lambda the kernel hands a
 * helper is part of the kernel, and so is a helper the kernel calls, followed transitively. A call
 * that passes a helper an enumerator constant for the parameter that helper switches on is followed
 * along the branch the constant selects: `complexBinary(norm, TypeCode::Complex64, ComplexOp::Mul)`
 * reads the `Mul` arm of `complexCompute` and not the `Div` arm that raises. Without that step a
 * shared dispatcher would make every operator that shares it look faulting, and `mul` -- an operator
 * that cannot fail -- would be reported as one that can.
 *
 * A raise is read by what it raises, not by the word `throw`:
 *
 *   - `throwRuntimeFault(RuntimeDiag::X)`, `RuntimeFault::make(X)` and `throw RuntimeFault(X)` are
 *     faults of the program, classified by `X` -- division by zero, index out of range, `fail`
 *     (`ProgramFault`), and everything else the library reports as a runtime fault;
 *   - `ProcessExit` (and `ExitRequest`) is the program leaving, not failing: the scheduler catches it
 *     and sets the exit code (`kernel/execute/pass.cpp`), so `exit` returns no value without faulting;
 *   - any other `throw` is counted under `unexpected-throw`: the operator leaves without a value,
 *     which is a path `panic` has to cover, and today the run does not report it as a fault at all.
 *
 * `OperandTypeUnsupported` and `NullSlotUnwrapped` are **not** counted: both say the compiler emitted
 * a call no type rule would have accepted, which is a defect of the compiler and not a fault the
 * program can make (`null` unwrapped is documented as the one fault `attempt` never isolates). They
 * are counted apart, so that a check does not read as if it had seen nothing there.
 *
 * --- what this check cannot see, and what it does about that ----------------
 * The analysis is syntactic, and it follows calls by name **within one file**: the kernel, the
 * helpers written beside it, and the lambdas it passes them. A raise reached through a helper in
 * another file is not followed -- the name alone does not say which function is meant (`refuse`,
 * `to_string`, `bytes` are each defined in several places), and following them all made every
 * operator that shares a utility read as faulting. What that costs is a path this check does not see,
 * so the answer to "does this file define the kernel at all" is refused (exit 2) rather than read as
 * "raises nothing": a check of nothing is never a pass. The direction is stated here because it is
 * the one that would hide a fault, and the counters in the report are the size of it.
 *
 * --- the second direction, which this check does not read -------------------
 * The comparison has two directions and this run takes one of them, plus half of the other. It reads
 * the red side -- an implementation that raises and a declaration that does not say `panic` -- and it
 * reads a declaration that states `panic` while its implementation raises **nothing at all**
 * (`unkept`). What it cannot read is a `panic` that a *type rule* makes unnecessary.
 *
 * `:str/join` is the example. Its kernel raises `InvalidArgument` when the separator is null
 * (`lib/std/builtin/operators/str.cpp#__join__`), the raise is in the text, and the check counts it;
 * writing `panic` on `:str/join` was measured to change the reading by exactly one `agree` and to say
 * nothing else. That arm is not a fault a call can make: `with <sep: string>`
 * (`lib/std/cpp/operator_rules.cpp#withArgument`) makes the separator a type the compiler writes, so
 * no call hands the kernel a null one. The check cannot see the type rules, so it cannot say "you
 * declared a fault your implementation cannot raise".
 *
 * That is the boundary of the criterion, and it is not a place to loosen it: saying that declaration
 * was wrong would take a reading of the type rules this check does not have, and calling it right by
 * ignoring the raise would stop it reading the raise in the cases where the arm is real.
 *
 * --- what the scan root is, and why it is `git ls-files` --------------------
 * The scan reads **the repository**, not whatever a directory happens to hold. The two are not the
 * same tree: a build directory holds a copy of the sources, and a bench run leaves one under
 * `.cache/parser-bench-dual/` that is stale by however long ago it was made. A directory walk
 * cannot tell a copy from the original, so it read that copy as the source and reported eight
 * kernels as missing from a file nobody had edited -- the file it was reading was not in the
 * repository at all.
 *
 * So the file list comes from `git ls-files`, and from nowhere else: a path that is not in that
 * list can never enter the run, and the check says so instead of skipping it. `.cache/`, `out/`,
 * `build*`, `third_party/` and any `node_modules/` are build residue, never repository content;
 * a tracked path under one of them is left out of the scan and **reported**, so that leaving it out
 * is a reading and not a silence.
 *
 * --- usage -----------------------------------------------------------------
 *   node scripts/check-operator-panic.mjs            report, exit 1 on any mismatch
 *   node scripts/check-operator-panic.mjs --list     report every operator's reading, not only reds
 *   node scripts/check-operator-panic.mjs --root DIR check another git worktree instead of this one
 *
 * Exit status: 0 when every operator's implementation and declaration agree, 1 when they do not,
 * 2 when the scan found no operator, could not find a kernel's implementation, could not read what
 * `git ls-files` lists, or was handed a path that `git ls-files` does not list.
 */

import fs from 'node:fs'
import path from 'node:path'
import { execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const NAME = 'operator-panic'
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

/** The sources of the standard library and of every module: the check has no second scope. */
const SOURCE = /\.(?:h|hpp|cpp|cc|cxx)$/
const CAMEL = /\.cml$/

/**
 * A directory of build residue: a copy of the tree, or something no source lives in. The word
 * `build*` names the build output directories (`build`, `build2`, `build-debug`, `build.cmake`,
 * `cmake-build-debug`); a directory whose name merely starts with the same letters is not one, so
 * `test/cases/tools/build_stamp/` -- a test case -- stays in scope.
 */
const BUILD_RESIDUE = /^(?:\.cache|out|third_party|node_modules|build\d*|build[-.].*|cmake-build.*)$/

/** A raise of one of these is a defect of the compiler, not a fault of the program (3.9). */
const INTERNAL_RAISES = new Set(['OperandTypeUnsupported', 'NullSlotUnwrapped'])

const args = process.argv.slice(2)
const listAll = args.includes('--list')
const rootAt = args.indexOf('--root')
const scanRoot = rootAt >= 0 ? path.resolve(args[rootAt + 1] ?? '.') : ROOT

// ---- reading the tree ------------------------------------------------------

/**
 * The paths git tracks in `dir`, relative and with `/` separators. This is the run's only entrance:
 * a file the repository does not hold is not a file this check may read, and the answer to "what
 * are the sources" is `git ls-files` rather than a directory walk, which cannot tell the tree from
 * a copy of it.
 */
function listTracked(dir) {
  let listing
  let toplevel
  try {
    toplevel = execFileSync('git', ['-C', dir, 'rev-parse', '--show-toplevel'], { encoding: 'utf8' }).trim()
  } catch (error) {
    console.error(`[${NAME}] ${dir} is not in a git worktree: ${error.message}`)
    console.error(`[${NAME}] the scan reads a repository, and refuses a directory it cannot name as one`)
    process.exit(2)
  }
  if (path.resolve(toplevel).toLowerCase() !== path.resolve(dir).toLowerCase()) {
    console.error(`[${NAME}] ${dir} is inside the worktree rooted at ${toplevel}, not its root`)
    console.error(`[${NAME}] \`git ls-files\` under a subdirectory lists that subdirectory: the scan would read a fragment as the whole repository and is refused`)
    process.exit(2)
  }
  try {
    listing = execFileSync('git', ['-C', dir, 'ls-files', '-z'], {
      encoding: 'utf8',
      maxBuffer: 256 << 20,
    })
  } catch (error) {
    console.error(`[${NAME}] cannot list what ${dir} holds with \`git ls-files\`: ${error.message}`)
    console.error(`[${NAME}] a scan that cannot say what is in the repository is refused, never guessed`)
    process.exit(2)
  }
  return listing.split('\0').filter(Boolean)
}

const trackedPaths = listTracked(scanRoot)
const tracked = new Set(trackedPaths)

/**
 * The files this run reads. Every read goes through `readTracked`, which names a path it cannot
 * account for and stops: a path outside the repository read as if it were the repository is the
 * defect this check was written to catch, and skipping it quietly would turn "read the wrong tree"
 * into "the check passed".
 */
const readPaths = new Set()

function readTracked(file) {
  const relative = path.relative(scanRoot, file).split(path.sep).join('/')
  if (!tracked.has(relative)) {
    console.error(`[${NAME}] ${file} is not a file of the repository: \`git ls-files\` does not list ${relative}`)
    console.error(`[${NAME}] every path the scan reads is one the repository holds; this one is named, not skipped`)
    process.exit(2)
  }
  readPaths.add(relative)
  try {
    return fs.readFileSync(file, 'utf8')
  } catch (error) {
    console.error(`[${NAME}] cannot read ${relative}, which \`git ls-files\` lists: ${error.message}`)
    console.error(`[${NAME}] a check of an implementation it cannot read is refused, never passed`)
    process.exit(2)
  }
}

/**
 * The text with every comment and every string/character literal blanked out, offset for offset, so
 * that a brace or a parenthesis inside a literal is not read as code and a comment's `throw` is not
 * read as a raise. Lengths are preserved, so an offset found here is an offset in the raw text.
 */
function blank(t) {
  const out = t.split('')
  const n = t.length
  let i = 0
  const wipe = (from, to) => {
    for (let k = from; k < to && k < n; k++) if (out[k] !== '\n') out[k] = ' '
  }
  while (i < n) {
    const c = t[i]
    if (c === '/' && t[i + 1] === '/') {
      const start = i
      while (i < n && t[i] !== '\n') i++
      wipe(start, i)
    } else if (c === '/' && t[i + 1] === '*') {
      const start = i
      i += 2
      while (i < n && !(t[i] === '*' && t[i + 1] === '/')) i++
      i += 2
      wipe(start, i)
    } else if (c === '"' || c === "'") {
      const quote = c
      const start = i
      i++
      while (i < n && t[i] !== quote) {
        if (t[i] === '\\') i++
        if (i < n && t[i] !== '\n') i++
        else if (t[i] === '\n') break
      }
      i++
      wipe(start, i)
    } else i++
  }
  return out.join('')
}

/** The index just past the `}` that closes the `{` at `open`, or -1. */
function closeBrace(t, open) {
  let depth = 0
  for (let i = open; i < t.length; i++) {
    if (t[i] === '{') depth++
    else if (t[i] === '}') {
      depth--
      if (depth === 0) return i
    }
  }
  return -1
}

/** The index of the `)` that closes the `(` at `open`, or -1. */
function closeParen(t, open) {
  let depth = 0
  for (let i = open; i < t.length; i++) {
    if (t[i] === '(') depth++
    else if (t[i] === ')') {
      depth--
      if (depth === 0) return i
    }
  }
  return -1
}

/** `a, b, c` split at the commas that are not inside a bracket, a brace or an angle. */
function splitArguments(text) {
  const out = []
  let depth = 0
  let start = 0
  for (let i = 0; i < text.length; i++) {
    const c = text[i]
    if ('([{<'.includes(c)) depth++
    else if (')]}>'.includes(c)) depth--
    else if (c === ',' && depth === 0) {
      out.push(text.slice(start, i))
      start = i + 1
    }
  }
  out.push(text.slice(start))
  return out.map(s => s.trim()).filter((s, i, all) => i < all.length - 1 || s.length > 0)
}

// ---- the functions of one file --------------------------------------------

/**
 * One file's function definitions: a name, the parameters it names, where its own text begins (the
 * end of its parameter list, so that an initializer list is part of it) and the offsets of its body.
 *
 * A definition is found by its shape -- an identifier, a parameter list, then the brace that opens
 * the body -- and not by its return type: a helper is written `[[noreturn]] void refuse(...)`,
 * `template <typename T> static slot_t assignMod(...)`, `size_t normalizedAxis(...)`, and an index
 * that knew only a list of return types read none of them. The statements that also have this shape
 * (`if`, `while`, `for`, `switch`, `catch`) are the ones excluded by name.
 */
const NOT_A_DEFINITION = new Set(['if', 'while', 'for', 'switch', 'catch', 'return', 'sizeof', 'do', 'else', 'case'])

/**
 * The `{` that opens a definition's body, found after its parameter list: the brace written
 * there, or -- for a constructor or a member written with an initializer list -- the first brace
 * outside that list's own parentheses. An initializer list is part of what the definition is: it
 * is where `GraphExecutor` hands `kernels()` to its base, and reading only the body would leave
 * the module's map unreachable from the module it belongs to. `-1` when the shape is not a
 * definition at all: a declaration ends at `;` and a call is followed by neither.
 */
function bodyOpen(text, paramsEnd) {
  const rest = text.slice(paramsEnd + 1, paramsEnd + 400)
  const direct = /^\s*(?:noexcept\s*)?(?:const\s*)?(?:override\s*)?\{/.exec(rest)
  if (direct) return paramsEnd + direct[0].length
  if (!/^\s*(?:noexcept\s*)?(?:const\s*)?(?:override\s*)?:/.test(rest)) return -1
  let depth = 0
  for (let i = paramsEnd + 1; i < text.length; i++) {
    const c = text[i]
    if (c === '(') depth++
    else if (c === ')') depth--
    else if (depth <= 0 && (c === ';' || c === '}')) return -1
    else if (c === '{' && depth <= 0) return i
  }
  return -1
}

function indexFunctions(text) {
  const funcs = new Map()
  const head = /([A-Za-z_]\w*)\s*\(/g
  let m
  while ((m = head.exec(text))) {
    const name = m[1]
    if (NOT_A_DEFINITION.has(name)) continue
    const openParen = m.index + m[0].length - 1
    const paramsEnd = closeParen(text, openParen)
    if (paramsEnd < 0) continue
    const open = bodyOpen(text, paramsEnd)
    if (open < 0) continue
    const close = closeBrace(text, open)
    if (close < 0) continue
    const params = splitArguments(text.slice(openParen + 1, paramsEnd)).map(paramName)
    if (!funcs.has(name)) funcs.set(name, [])
    // `from` is where the definition's own text begins: the parameter list ends there, and an
    // initializer list -- between that `)` and the body's `{` -- is part of the definition even
    // though it is not part of the body the raises are read from.
    funcs.get(name).push({ name, params, from: paramsEnd, open, close })
  }
  return funcs
}

/** The identifier a parameter declares: its last word, after `const`, `&`, `*` and a default. */
function paramName(text) {
  const withoutDefault = text.split('=')[0]
  const words = withoutDefault.match(/[A-Za-z_]\w*/g) ?? []
  return words.length ? words[words.length - 1] : ''
}

// ---- following a call along the branch it selects --------------------------

/** `Enum::Value` to `Value`, the label a `switch` on that enum would write. */
function enumeratorOf(expression) {
  const m = /^([A-Za-z_]\w*)\s*::\s*([A-Za-z_]\w*)$/.exec(expression.trim())
  return m ? m[2] : null
}

/**
 * The body with every `switch (param) { ... }` replaced by the one arm `value` selects, so that a
 * raise under another arm is not read as one this call can reach. A `switch` with no such arm and no
 * `default` is left as it is: nothing is known about which arm runs, so every arm is still read.
 */
function restrictSwitches(body, bindings) {
  if (!Object.keys(bindings).length) return body
  const re = /switch\s*\(\s*([A-Za-z_]\w*)\s*\)\s*\{/g
  let out = body
  let m
  while ((m = re.exec(out))) {
    const param = m[1]
    if (!(param in bindings)) continue
    const open = m.index + m[0].length - 1
    const close = closeBrace(out, open)
    if (close < 0) continue
    const arm = selectArm(out.slice(open + 1, close), bindings[param])
    if (arm === null) continue
    out = out.slice(0, m.index) + '{ ' + arm + ' }' + out.slice(close + 1)
    re.lastIndex = 0
  }
  return out
}

/** The text of the arm `value` selects, the `default` arm when no label matches, or null. */
function selectArm(block, value) {
  const labels = []
  const re = /(?:^|\n)[ \t]*(?:case\s+([\w:]+)\s*:|default\s*:)/g
  let m
  while ((m = re.exec(block))) labels.push({ at: m.index, label: m[1] ? m[1].split('::').pop() : 'default' })
  if (!labels.length) return null
  const armOf = index => {
    const start = labels[index].at
    const end = index + 1 < labels.length ? labels[index + 1].at : block.length
    return block.slice(start, end)
  }
  for (let i = 0; i < labels.length; i++) if (labels[i].label === value) return armOf(i)
  for (let i = 0; i < labels.length; i++) if (labels[i].label === 'default') return armOf(i)
  return null
}

// ---- the raises of one operator -------------------------------------------

const DIRECT_RAISE = /(?:throwRuntimeFault|RuntimeFault::make)\s*\(\s*(?:[\w:]+::)*RuntimeDiag::(\w+)/g
const THROW = /\bthrow\b(?!\w)/g
const FAULT_TYPE = /^\s*(?:[\w:]+::)*RuntimeFault\s*\(/
const NOT_A_FAULT = /^\s*(?:[\w:]+::)*(?:ProcessExit|ExitRequest)\b/

/**
 * Every raise in one piece of code: the diagnostic it names, or the class of raise where it names
 * none. `exit` leaves the run through `ProcessExit` -- the scheduler catches it and sets the exit
 * code (`kernel/execute/pass.cpp`), so the operator returns no value without failing. Everything
 * else that is not a `RuntimeFault` is counted under `unexpected-throw`: the operator leaves without
 * a value, which is a path `panic` has to cover whether or not the run reports it well today.
 */
function raisesOf(text) {
  const out = []
  DIRECT_RAISE.lastIndex = 0
  let m
  while ((m = DIRECT_RAISE.exec(text))) out.push({ kind: m[1], at: m.index })
  THROW.lastIndex = 0
  while ((m = THROW.exec(text))) {
    const rest = text.slice(m.index + 5, m.index + 400)
    let depth = 0
    let end = rest.length
    for (let i = 0; i < rest.length; i++) {
      const c = rest[i]
      if ('([{'.includes(c)) depth++
      else if (')]}'.includes(c)) depth--
      else if (c === ';' && depth <= 0) {
        end = i
        break
      }
    }
    const thrown = rest.slice(0, end)
    if (NOT_A_FAULT.test(thrown)) continue
    if (!thrown.trim()) continue // a rethrow propagates what the caller raised
    const named = /(?:[\w:]+::)*RuntimeDiag::(\w+)/.exec(thrown)
    if (FAULT_TYPE.test(thrown)) out.push({ kind: named ? named[1] : 'RuntimeFault', at: m.index })
    else out.push({ kind: 'unexpected-throw', at: m.index })
  }
  return out
}

/**
 * The raises an operator can reach from `name`, following the helpers of its own file and the arms
 * a constant argument selects. `seen` holds the name plus the bindings it was scanned with, so a
 * recursive helper is walked once per branch rather than for ever.
 */
function collectRaises(funcs, text, name, bindings = {}, depth = 0, seen = new Set(), sites = []) {
  if (depth > 6) return sites
  const signature = name + '|' + JSON.stringify(bindings)
  if (seen.has(signature)) return sites
  seen.add(signature)
  const definitions = funcs.get(name)
  if (!definitions) return sites
  for (const definition of definitions) {
    const body = restrictSwitches(text.slice(definition.open, definition.close + 1), bindings)
    for (const raise of raisesOf(body)) sites.push({ ...raise, from: name, internal: INTERNAL_RAISES.has(raise.kind) })
    const callRe = /\b([A-Za-z_]\w*)\s*(?:<[^<>;{}()]*>\s*)?\(/g
    let m
    while ((m = callRe.exec(body))) {
      const callee = m[1]
      if (callee === name || !funcs.has(callee)) continue
      const callOpen = body.indexOf('(', m.index + m[0].length - 1)
      const callClose = closeParen(body, callOpen)
      if (callClose < 0) continue
      const passed = splitArguments(body.slice(callOpen + 1, callClose))
      const calleeBindings = {}
      for (const definition2 of funcs.get(callee)) {
        definition2.params.forEach((param, index) => {
          if (!param || index >= passed.length) return
          const asEnum = enumeratorOf(passed[index])
          if (asEnum) calleeBindings[param] = asEnum
          else if (/^[A-Za-z_]\w*$/.test(passed[index].trim()) && passed[index].trim() in bindings)
            calleeBindings[param] = bindings[passed[index].trim()]
        })
      }
      collectRaises(funcs, text, callee, calleeBindings, depth + 1, seen, sites)
    }
  }
  return sites
}

// ---- the registrations and the declarations -------------------------------

/** `{"op/div_i", __builtin__div__}` inside a function that builds an `operator_t` map. */
const MAP_HEAD =
  /(?:^|\n)[ \t]*[^\n{]*?(?:unordered_map\s*<\s*std::string\s*,\s*operator_t\s*>|(?:operatorKernels|Kernels|OpsMap)\s*\(\s*\))[^\n{]*\{/g
const PAIR = /\{\s*"([A-Za-z_][\w./]*)"\s*,\s*(__\w+__|[A-Za-z_]\w*)\s*(?:,|\})/g

/**
 * The registrations, and the map each one is written in. The map is a region of one file: its key
 * is what a module is later attributed to (see `readMapModules`), and the pair carries that key so
 * that a registration and its module never drift apart.
 */
function readRegistrations(files, raws) {
  const pairs = []
  const regions = []
  for (const file of files) {
    const text = blank(readTracked(file))
    const raw = raws.get(file) ?? ''
    MAP_HEAD.lastIndex = 0
    let m
    while ((m = MAP_HEAD.exec(text))) {
      const open = m.index + m[0].length - 1
      const close = closeBrace(text, open)
      if (close < 0) continue
      const region = `${file}|${open}`
      regions.push({ key: region, file, open, close })
      // The pair's URI is a string literal, which `blank` erases: the entries are read off the raw
      // text at the same offsets (the two texts have one length).
      const body = raw.slice(open, close + 1)
      PAIR.lastIndex = 0
      let p
      while ((p = PAIR.exec(body))) {
        pairs.push({ uri: p[1], kernel: p[2], file, region, line: lineAt(text, open + p.index) })
      }
    }
  }
  return { pairs, regions }
}

function lineAt(text, offset) {
  let line = 1
  for (let i = 0; i < offset && i < text.length; i++) if (text[i] === '\n') line++
  return line
}

/**
 * A URI split at its first `:`: the module it answers on, and the name it has inside that module.
 *
 * Both entrances write a URI, and the two sides of this check are written differently: a
 * declaration writes the whole URI (`array:min_i`, `:op/div_i` -- the empty prefix is the core),
 * while an implementation map is keyed by the module-internal name alone (`min_i`). The module is
 * the dimension that tells two operators apart: `re:find` and `str:find` are two operators with
 * one name, and so are `array:min_i` and `math:min_i`, and only the first of each pair can fail.
 * Comparing the two sides is therefore done on `module:name` -- never on the bare name, which
 * would fold those pairs into one and let a declaration of one answer for the other.
 */
function uriParts(uri) {
  const at = uri.indexOf(':')
  return at < 0 ? { module: '', name: uri } : { module: uri.slice(0, at), name: uri.slice(at + 1) }
}

/** The key both sides are compared on: `module:name`, the empty prefix being the core. */
function uriKey(uri) {
  const { module, name } = uriParts(uri)
  return `${module}:${name}`
}

/**
 * The `panic` a declaration states, by `module:name`. The Camel entrance writes the keyword in the
 * declaration under `#op('<uri>')`; the C++ entrance passes it as the second argument, which has no
 * default.
 */
function readDeclarations(cmlFiles, sourceFiles) {
  const stated = new Map()
  const record = (uri, value, file, line) => {
    const key = uriKey(uri)
    if (!stated.has(key)) stated.set(key, [])
    stated.get(key).push({ uri, key, value, file, line })
  }
  for (const file of cmlFiles) {
    const lines = readTracked(file).split('\n')
    for (let i = 0; i < lines.length; i++) {
      const marker = /^\s*#op\(\s*'([^']+)'\s*\)/.exec(lines[i])
      if (!marker) continue
      // The declaration is the `func` line under the marker, up to its body (an operator has none)
      // or the first blank line: `panic` is written among `pure`, `uses` and the result type.
      const declaration = []
      for (let k = i + 1; k < Math.min(i + 8, lines.length); k++) {
        if (/^\s*$/.test(lines[k])) break
        declaration.push(lines[k])
        if (/[{(]/.test(lines[k])) break
      }
      record(marker[1], /\bpanic\b/.test(declaration.join(' ')), file, i + 1)
    }
  }
  for (const file of sourceFiles) {
    const text = blank(readTracked(file))
    const re = /CppRegistration\s*(?:&)?\s*\w*\s*\(\s*"([^"]+)"\s*,\s*(true|false)\s*\)/g
    let m
    while ((m = re.exec(text))) record(m[1], m[2] === 'true', file, lineAt(text, m.index))
  }
  return stated
}

/**
 * The module each implementation map answers on, read from the C++ that states it.
 *
 * An implementation map is keyed by the module-internal name (`min_i`, `find`, `op/div_i`), so the
 * module is what a key means. The module is not the directory a file sits in -- `lib/random/`
 * answers the protocol `rand` -- and it is not inferred from the declaration side, which would be
 * the check reading its own answer. It is stated twice in C++, and this joins the two:
 *
 *   1. `registerExecutorFactory("<module>", ...)` names the protocol and writes the executor that
 *      answers it (`lib/re/module.cpp`, `lib/std/builtin/modules/globals.cpp`);
 *   2. that expression names the map, directly or through the function or the class it builds
 *      from: `getArrayOpsMap()` outright, `createReExecutor` -> `getReOpsMap()`, `GraphExecutor`'s
 *      initializer list -> `kernels()`, `BasicBuiltinExecutor::create` -> `getOpsImplMap()` ->
 *      `makeOpsImplMap()`.
 *
 * So the walk starts at a factory's expression and follows the names it writes into the
 * definitions of this repository, stopping at the region a map is written in. A name with more
 * than one definition is followed only to the one written in the same file (`kernels` is four
 * modules' word for four different maps); a name with several definitions and none in the same
 * file is not followed at all (`create` is every module's), because guessing which one is meant is
 * the defect this whole check exists to avoid.
 *
 * A map answered by two modules, or by none, is a reading the run cannot take, and the caller
 * refuses (exit 2) rather than filing those operators under a module that may not be theirs.
 */
function readMapModules(regions, loadedRegions, files, texts, raws) {
  const claimed = new Map() // map region key -> Set of modules
  const FACTORY = /registerExecutorFactory\s*\(/g
  const definitionsIn = (file, definition) =>
    texts.get(file).slice(definition.from, definition.close + 1)
  for (const file of files) {
    const text = texts.get(file)
    const raw = raws.get(file)
    FACTORY.lastIndex = 0
    let m
    while ((m = FACTORY.exec(text))) {
      const open = m.index + m[0].length - 1
      const close = closeParen(text, open)
      if (close < 0) continue
      // The protocol is a string literal, which `blank` erases: it is read off the raw text.
      const named = /^\s*"([^"]*)"/.exec(raw.slice(open + 1, close + 1))
      if (!named) continue
      const module = named[1]
      let frontier = text.slice(open + 1, close).match(/[A-Za-z_]\w*/g) ?? []
      const followed = new Set()
      for (let depth = 0; depth <= 4 && frontier.length; depth++) {
        const next = []
        for (const name of frontier) {
          if (followed.has(name)) continue
          followed.add(name)
          const candidates = definitionsByName.get(name) ?? []
          const here = candidates.filter(candidate => candidate.file === file)
          const definition = here.length === 1 ? here[0] : here.length ? null : candidates.length === 1 ? candidates[0] : null
          if (!definition) continue
          let loadedHere = false
          for (const region of regions) {
            if (
              region.file === definition.file &&
              region.open >= definition.definition.from &&
              region.close <= definition.definition.close
            ) {
              if (loadedRegions.has(region.key)) loadedHere = true
              if (!claimed.has(region.key)) claimed.set(region.key, new Set())
              claimed.get(region.key).add(module)
            }
          }
          // A map with registrations in it is the end of the road: what it writes are kernel
          // names, and no map is reachable through them. A shape that only looks like one (a
          // function returning a map, say) is walked through, because the map it builds is past it.
          if (loadedHere) continue
          next.push(...(definitionsIn(definition.file, definition.definition).match(/[A-Za-z_]\w*/g) ?? []))
        }
        frontier = next
      }
    }
  }
  return claimed
}

// ---- the run ---------------------------------------------------------------

/**
 * The run's subject: the repository's files, with the build residue cut out. `git ls-files` is the
 * list; a path whose components include a build directory is left out **and counted**, because a
 * tracked file that the scan skips is a reading somebody has to see.
 */
const residue = []
const all = []
for (const relative of trackedPaths) {
  const parts = relative.split('/')
  // Only the directories on the way to the file count: `build/x.cpp` is residue, `builder.cpp` is a
  // source, and a test that reads the name of the file would confuse them.
  const directories = parts.slice(0, -1)
  const outside = directories.find(part => BUILD_RESIDUE.test(part))
  if (outside) {
    residue.push(`${relative} (${outside}/)`)
    continue
  }
  all.push(path.join(scanRoot, ...parts))
}

const sourceFiles = all.filter(f => SOURCE.test(f))
const cmlFiles = all.filter(f => CAMEL.test(f))

const texts = new Map()
const raws = new Map()
const functions = new Map()
const definitionsByName = new Map()
for (const file of sourceFiles) {
  const raw = readTracked(file)
  raws.set(file, raw)
  const text = blank(raw)
  texts.set(file, text)
  const funcs = indexFunctions(text)
  functions.set(file, funcs)
  for (const [name, definitions] of funcs) {
    if (!definitionsByName.has(name)) definitionsByName.set(name, [])
    for (const definition of definitions) definitionsByName.get(name).push({ file, definition })
  }
}

/**
 * Where a kernel is written. A map names the kernel and the kernel is defined beside it or in
 * another file of the same module, so the definition nearest the registering file wins: the same
 * name in two modules is two functions, and the module that registers it is the one that means it.
 */
function resolveKernel(name, registeringFile) {
  const candidates = definitionsByName.get(name) ?? []
  if (!candidates.length) return null
  const dir = path.dirname(registeringFile)
  const rank = candidate => {
    const other = path.dirname(candidate.file)
    if (other === dir) return 0
    const shared = commonPrefix(other.split(path.sep), dir.split(path.sep))
    return 100 - shared
  }
  return [...candidates].sort((a, b) => rank(a) - rank(b))[0]
}

function commonPrefix(a, b) {
  let n = 0
  while (n < a.length && n < b.length && a[n] === b[n]) n++
  return n
}

const { pairs: registrations, regions: mapRegions } = readRegistrations(sourceFiles, raws)
if (!registrations.length) {
  console.error(`[${NAME}] no operator registration found under ${scanRoot}: nothing to check`)
  process.exit(2)
}

const mapModules = readMapModules(mapRegions, new Set(registrations.map(r => r.region)), sourceFiles, texts, raws)

/** The one module a map answers on, or null when the C++ does not say exactly one. */
const moduleOfMap = region => {
  const modules = mapModules.get(region)
  return modules && modules.size === 1 ? [...modules][0] : null
}

const unattributed = new Set()
const unresolved = new Set()
const operators = new Map() // `module:name` -> { uri, kernels: Set, sites: [], files: Set, first }
for (const registration of registrations) {
  const module = moduleOfMap(registration.region)
  if (module === null) {
    const named = [...(mapModules.get(registration.region) ?? [])].sort().join(', ') || 'none'
    unattributed.add(
      `${registration.file}:${registration.line}: the map is answered by ${named === 'none' ? 'no module' : `${named} -- more than one`}`,
    )
    continue
  }
  const key = `${module}:${registration.uri}`
  if (!operators.has(key)) {
    operators.set(key, { uri: key, kernels: new Set(), sites: [], files: new Set(), first: registration })
  }
  const operator = operators.get(key)
  operator.kernels.add(registration.kernel)
  operator.files.add(registration.file)
  const resolved = resolveKernel(registration.kernel, registration.file)
  if (!resolved) {
    unresolved.add(`${registration.file}:${registration.line}: ${key} -> ${registration.kernel}`)
    continue
  }
  const funcs = functions.get(resolved.file)
  for (const site of collectRaises(funcs, texts.get(resolved.file), registration.kernel)) {
    operator.sites.push({ ...site, file: resolved.file })
  }
}

const declarations = readDeclarations(cmlFiles, sourceFiles)

// ---- the scan root's own check --------------------------------------------
//
// Every read above went through `readTracked`, which stops on a path `git ls-files` does not list.
// What is checked here is the other direction: that the run read exactly the files it enumerated.
// A file that was enumerated and never read, or a read that came from somewhere the enumeration
// does not cover, is the same defect seen from the other side -- a check reading a tree it cannot
// name -- and both are answered by naming the difference rather than by passing.

const unaccounted = [...readPaths].filter(relative => !tracked.has(relative))
if (unaccounted.length) {
  for (const relative of unaccounted) {
    console.error(`[${NAME}] ${relative} was read and \`git ls-files\` does not list it`)
  }
  console.error(`[${NAME}] ${unaccounted.length} files the scan cannot account for: a scan of the wrong tree is refused, never passed`)
  process.exit(2)
}
const expectedReads = sourceFiles.length + cmlFiles.length
if (readPaths.size !== expectedReads) {
  const read = new Set(readPaths)
  const missed = [...sourceFiles, ...cmlFiles]
    .map(file => path.relative(scanRoot, file).split(path.sep).join('/'))
    .filter(relative => !read.has(relative))
  for (const relative of missed) console.error(`[${NAME}] ${relative} was enumerated and never read`)
  console.error(`[${NAME}] ${readPaths.size} files read of ${expectedReads} enumerated: the run is not the scan it says it is, and is refused`)
  process.exit(2)
}

const classify = kind => {
  if (kind === 'DivisionByZero') return 'division-by-zero'
  if (kind === 'IndexOutOfRange') return 'index-out-of-range'
  if (kind === 'ProgramFault') return 'fail'
  if (kind === 'unexpected-throw') return 'unexpected-throw'
  return 'other-runtime-fault'
}

/** The same raise reached by five operators of one kernel is one place in the source, counted once. */
const distinctSites = new Map()
for (const operator of operators.values()) {
  for (const site of operator.sites) {
    if (site.internal) continue
    const key = `${site.file}|${site.from}|${site.at}`
    if (!distinctSites.has(key)) distinctSites.set(key, classify(site.kind))
  }
}

const kindTotals = new Map()
const distinctTotals = new Map()
const red = []
const green = []
const internalOnly = []
const unkept = []
for (const operator of [...operators.values()].sort((a, b) => a.uri.localeCompare(b.uri))) {
  const program = operator.sites.filter(site => !site.internal)
  const internal = operator.sites.filter(site => site.internal)
  for (const site of program) {
    const key = classify(site.kind)
    kindTotals.set(key, (kindTotals.get(key) ?? 0) + 1)
  }
  const stated = declarations.get(operator.uri) ?? []
  const promisesFault = stated.some(s => s.value)
  const reading = {
    ...operator,
    kinds: [...new Set(program.map(s => classify(s.kind)))].sort(),
    rawKinds: [...new Set(program.map(s => s.kind))].sort(),
    internalKinds: [...new Set(internal.map(s => s.kind))].sort(),
    promisesFault,
    stated,
  }
  if (program.length && !promisesFault) red.push(reading)
  else if (!program.length && promisesFault) unkept.push(reading)
  else {
    green.push(reading)
    if (internal.length && !program.length) internalOnly.push(reading)
  }
}

const line = message => console.log(message)
line(`[${NAME}] ${sourceFiles.length} source files read from \`git ls-files\` under ${scanRoot}; ${operators.size} operators registered in ${new Set(registrations.map(r => r.file)).size} implementation maps`)
const protocols = [...new Set(operators.keys())].map(key => uriParts(key).module)
const colours = [...new Set(protocols)].sort((a, b) => (a === '' ? -1 : b === '' ? 1 : a.localeCompare(b)))
line(`[${NAME}] the maps answer on ${colours.length} protocols (a map's key is a name inside one of them, so \`find\` in \`re\` and \`find\` in \`str\` are two operators): ${colours.map(module => (module === '' ? '\'\' (the core)' : module)).join(', ')}`)
const totals = [...kindTotals.entries()].sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k} ${v}`).join(', ')
const siteCount = [...kindTotals.values()].reduce((a, b) => a + b, 0)
line(`[${NAME}] ${siteCount} raises reachable from an operator's implementation: ${totals || '(none)'}`)
const distinctKinds = new Map()
for (const kind of distinctSites.values()) distinctKinds.set(kind, (distinctKinds.get(kind) ?? 0) + 1)
const distinctLine = [...distinctKinds.entries()].sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k} ${v}`).join(', ')
line(`[${NAME}] ${distinctSites.size} distinct raise sites, the same site reached by several operators counted once: ${distinctLine || '(none)'}`)
line(`[${NAME}] ${red.length} operators raise and state no \`panic\`; ${green.length} agree; ${unkept.length} state a fault their implementation does not raise`)
line(`[${NAME}] ${readPaths.size} files read, every one of them listed by \`git ls-files\`; ${residue.length} tracked paths under ${BUILD_RESIDUE.source} were not scanned`)
if (residue.length) {
  for (const skipped of residue.slice(0, 10)) line(`  not scanned: ${skipped}`)
  if (residue.length > 10) line(`  not scanned: and ${residue.length - 10} more`)
}

if (listAll) {
  for (const operator of [...red, ...green, ...unkept].sort((a, b) => a.uri.localeCompare(b.uri))) {
    const reading = operator.promisesFault ? 'panic' : 'no panic'
    const kinds = operator.rawKinds.length ? operator.rawKinds.join(',') : '(nothing raised)'
    line(`  ${operator.uri} [${reading}] ${kinds}  <- ${[...operator.kernels].join(',')} (${operator.first.file}:${operator.first.line})`)
  }
}

for (const operator of red) {
  const where = `${path.relative(scanRoot, operator.first.file)}:${operator.first.line}`
  line(`  ${where}: ${operator.uri} raises (${operator.rawKinds.join(', ')}) and nothing states \`panic\``)
}
for (const operator of unkept) {
  const where = `${path.relative(scanRoot, operator.first.file)}:${operator.first.line}`
  line(`  ${where}: ${operator.uri} states \`panic\` and its implementation raises nothing`)
}
if (internalOnly.length) {
  line(`[${NAME}] ${internalOnly.length} more operators raise only a compiler defect (${[...INTERNAL_RAISES].join(', ')}), which is not a fault of the program and is not counted`)
}

if (unattributed.size) {
  for (const missing of [...unattributed].sort()) line(`  ${missing}`)
  console.error(`[${NAME}] ${unattributed.size} registrations are in a map no single module answers on: an operator filed under a module that may not be its own is refused, never guessed`)
  process.exit(2)
}

if (unresolved.size) {
  for (const missing of [...unresolved].sort()) line(`  ${missing}: no implementation found in the file that registers it`)
  console.error(`[${NAME}] ${unresolved.size} kernels could not be found: a check of an implementation it cannot read is refused, never passed`)
  process.exit(2)
}

process.exit(red.length || unkept.length ? 1 : 0)