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
 *      keyword written beside `pure` and `uses` (`src/stdlib/**\/*.cml`);
 *   2. the C++ entrance, `CppRegistration`'s second argument, which is required and has no default
 *      (`src/kernel/ops/include/camel/ops/operator_record.h`), written as a call with literals;
 *   3. the same entrance written as a table of `CppPanicStatement` entries, `{":op/add_i", false}`,
 *      which a loop hands to `CppRegistration` (`src/stdlib/std/builtin/executors/builtin_semantics.cpp`).
 *      A table is read by its entry type, because the type is what says that a boolean beside a
 *      URI is the `panic` answer and not some other flag. The URIs and the answers are literals in
 *      both shapes: a registration built from a name the scan would have to evaluate is a
 *      statement no check can read, and the report below counts how many operators it can read so
 *      that such a gap is a number rather than a silence.
 *
 * The declarations state it now: 42 operators declare it, and the two scatters
 * `src/stdlib/rulesets/transpose.cml` declares privately take the count to 44 of the 378 this run reads. So
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
 * that cannot fail -- would be reported as one that can. The `unkept` answer also follows a call
 * into another file; the two directions read different ranges, and "how far the analysis follows a
 * call" below says why.
 *
 * A raise is read by what it raises, not by the word `throw`:
 *
 *   - `throwRuntimeFault(RuntimeDiag::X)`, `RuntimeFault::make(X)` and `throw RuntimeFault(X)` are
 *     faults of the program, classified by `X` -- division by zero, index out of range, `fail`
 *     (`ProgramFault`), and everything else the library reports as a runtime fault;
 *   - `ProcessExit` (and `ExitRequest`) is the program leaving, not failing: the scheduler catches it
 *     and sets the exit code (`src/kernel/execute/pass.cpp`), so `exit` returns no value without faulting;
 *   - any other `throw` is counted under `unexpected-throw`: the operator leaves without a value,
 *     which is a path `panic` has to cover, and today the run does not report it as a fault at all.
 *
 * `OperandTypeUnsupported` and `NullSlotUnwrapped` are **not** counted: both say the compiler emitted
 * a call no type rule would have accepted, which is a defect of the compiler and not a fault the
 * program can make (`null` unwrapped is documented as the one fault `attempt` never isolates). They
 * are counted apart, so that a check does not read as if it had seen nothing there.
 *
 * --- how far the analysis follows a call, and why the two answers differ -----
 * The analysis is syntactic, and it reads the two directions over **different ranges**, because the
 * two questions have different shapes:
 *
 *   - "this implementation raises and the declaration does not say `panic`" needs **one** raise.
 *     The red side therefore stops at the kernel's own file: the kernel, the helpers written beside
 *     it, and the lambdas it passes them. One raise in that text is enough, and a wider search
 *     cannot make this answer wrong.
 *   - "this implementation raises **nothing**" is a claim about everything the kernel can reach.
 *     The `unkept` side therefore follows the call out of the file -- see "the second reading"
 *     below -- and a call it cannot resolve is reported as `unproven`, never as `unkept`. A raise it
 *     cannot find must not read as a raise that does not happen.
 *
 * --- the second reading: following a call out of the kernel's file -------------
 * A raise is one call away from the kernel more often than it is in the kernel. A conversion hands
 * its value to the type layer (`operators/cast.cpp#__cast_value__` calls `Type::castSlotFrom`), and
 * what refuses the value is written in `src/kernel/core/type/composite/array.cpp`. Reading "raises
 * nothing" over the kernel's file alone called two correct declarations (`:op/cast`,
 * `:op/refine_extent`) wrong for as long as it did.
 *
 * So a call is resolved, not skipped:
 *
 *   1. the same file first, by name, as the red side reads it (a helper beside the kernel);
 *   2. a **member** call by the class the receiver is declared with: the class is found from the
 *      parameter's type or from a local declaration in the same definition, and the member's
 *      declaration in that class (or its bases) says whether it is `virtual`. A non-virtual method
 *      reaches the one definition; a virtual one reaches every definition of that name in the
 *      classes that derive from the declaring class, which is what a virtual call can run;
 *   3. a **free** call by name, when exactly one function of the sources takes that many arguments.
 *      Several is an overload the text does not settle, and the call is a gap.
 *
 * The walk is breadth first, level by level, and it stops at the first raise: a level is finished
 * before the next is entered, so the raise a declaration is about -- a call or two away -- is found
 * before the walk dives into a reader every kernel shares. It stops after `MAX_DEPTH` calls or
 * `WALK_BUDGET` definitions, whichever comes first.
 *
 * One limit of the member step, stated because it is the one place a wrong answer is possible rather
 * than refused: a member is found by **name**, not by signature. A class that writes one name twice
 * (an overload) gives one entry, so a call is followed as if it named the member this class writes
 * first. A virtual override cannot differ in arity from the declaration it overrides, which is why
 * the union over derived classes is still right for the virtual case; for a class with two
 * non-virtual overloads the walk may follow the one the call does not name. An overload reached that
 * way can only add a raise that is in the same class, which reads as kept, never as unkept -- so the
 * error is in the direction this check must not make: it cannot turn a raising implementation into
 * an unkept one.
 *
 * What the walk cannot resolve is reported as **`unproven`**, and the report names the call and the
 * reason: a receiver whose type the text does not write (a call of a returned value), a class name
 * two classes answer to, an overload the call site does not settle, a call through a function
 * pointer or a functor, a call through a subscript, a virtual method no source defines, a walk that
 * ran out of depth or of budget. `unproven` fails the run (exit 1) exactly as `unkept` does: an
 * answer that could not be taken is not an answer that the raise is absent.
 *
 * The one thing the walk does **not** treat as unread is a name no source defines at all -- `GUARD`,
 * `toSlot`, `static_cast`. Those are macros, constructors and the C++ library, and a scan that
 * called every one of them unproven would say nothing about any operator. They are counted instead
 * (`N names were called that no source defines`), so the size of that entrance is a number.
 *
 * --- the second direction, which this check does not read -------------------
 * The comparison has two directions and this run takes one of them, plus half of the other. It reads
 * the red side -- an implementation that raises and a declaration that does not say `panic` -- and it
 * reads a declaration that states `panic` while its implementation raises **nothing at all**
 * (`unkept`). What it cannot read is a `panic` that a *type rule* makes unnecessary.
 *
 * `:str/join` is the example. Its kernel raises `InvalidArgument` when the separator is null
 * (`src/stdlib/std/builtin/operators/str.cpp#__join__`), the raise is in the text, and the check counts it;
 * writing `panic` on `:str/join` was measured to change the reading by exactly one `agree` and to say
 * nothing else. That arm is not a fault a call can make: `with <sep: string>`
 * (`src/stdlib/std/cpp/operator_rules.cpp#withArgument`) makes the separator a type the compiler writes, so
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
 * Exit status: 0 when every operator's implementation and declaration agree, 1 when they do not --
 * including a declaration this scan could not prove or disprove (`unproven`, which is neither a pass
 * nor the `unkept` answer) -- 2 when the scan found no operator, could not find a kernel's
 * implementation, could not read what `git ls-files` lists, or was handed a path that
 * `git ls-files` does not list.
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

/**
 * The text with every comment blanked out and every string literal kept, offset for offset. The
 * declarations the C++ entrance states are literals -- a URI and a boolean -- so a scan that
 * blanks them reads none of them; and a shape written inside a comment states nothing, so the
 * comments still have to go. Lengths are preserved, so an offset found here is an offset in the
 * raw text (the two texts agree byte for byte).
 */
function blankComments(t) {
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
      // A literal is kept, and it ends at its own quote: a backslash escapes the next character,
      // and an unterminated literal ends at the end of the file rather than swallowing the rest.
      const quote = c
      i++
      while (i < n && t[i] !== quote) {
        if (t[i] === '\\') i++
        i++
      }
      i++
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
    const written = splitArguments(text.slice(openParen + 1, paramsEnd))
    const params = written.map(paramName)
    const paramTypes = written.map((one, index) => typeOfParameter(one, params[index]))
    if (!funcs.has(name)) funcs.set(name, [])
    // `from` is where the definition's own text begins: the parameter list ends there, and an
    // initializer list -- between that `)` and the body's `{` -- is part of the definition even
    // though it is not part of the body the raises are read from. `owner` is the class a member
    // is written in, `ArrayType::castSlotFrom` or a definition inside the class body; a free
    // function has none, and the walk over the sources fills in the enclosing class for the
    // second shape (`attachOwners`).
    funcs.get(name).push({
      name,
      params,
      paramTypes,
      owner: qualifierBefore(text, m.index),
      from: paramsEnd,
      open,
      close,
    })
  }
  return funcs
}

/** The identifier a parameter declares: its last word, after `const`, `&`, `*` and a default. */
function paramName(text) {
  const withoutDefault = text.split('=')[0]
  const words = withoutDefault.match(/[A-Za-z_]\w*/g) ?? []
  return words.length ? words[words.length - 1] : ''
}

/**
 * The type a parameter is written with: the last identifier before its own name, so that `ArgsView
 * &with` is `ArgsView` and `const std::string &what` is `string`. It is the receiver's static type
 * when the receiver is that parameter, which is how a member call is resolved at all.
 */
function typeOfParameter(text, name) {
  const withoutDefault = text.split('=')[0]
  const at = name ? withoutDefault.lastIndexOf(name) : -1
  const type = at > 0 ? withoutDefault.slice(0, at) : withoutDefault
  const words = type.match(/[A-Za-z_]\w*/g) ?? []
  return words.length ? words[words.length - 1] : ''
}

/** The class a member function is written in, when it is written `Class::name`: `Class`, else null. */
function qualifierBefore(text, at) {
  const m = /([A-Za-z_]\w*)\s*::\s*$/.exec(text.slice(Math.max(0, at - 120), at))
  return m ? m[1] : null
}

// ---- the classes of the sources -------------------------------------------
//
// A member call is resolved through the class that declares the method, so the scan reads the
// class heads and the member declarations they hold. The shape is the language's: a head is
// `class Name` or `struct Name`, an optional `final`, and the bases after `:`; the members are
// what is written at the head's own brace depth, an inline definition's body being skipped.
//
// A member name is what a member call means only together with the class: `slot` is `ArgsView`'s
// reader and also a field of several helpers, and `castSlotFrom` is `Type`'s conversion -- the
// three meanings are told apart by the receiver, never by the name alone. The bases are read for
// the one thing they decide: a **virtual** method's call reaches every override in a class that
// derives from the class declaring it, and a call of a method that is not virtual reaches the one
// definition. That join is what makes a virtual call honest instead of unread.

const CLASS_HEAD = /\b(?:class|struct)\s+([A-Za-z_]\w*)\s*([^{};]*)\{/g

/** `Name : public Base, private Other` to `[Base, Other]`, the qualifiers and `final` dropped. */
function basesOf(tail) {
  const afterColon = /:([\s\S]*)$/.exec(tail)
  if (!afterColon) return []
  const bases = []
  for (const part of afterColon[1].split(',')) {
    const words = (part.match(/[A-Za-z_]\w*/g) ?? []).filter(
      word => !/^(?:public|private|protected|virtual|final)$/.test(word),
    )
    if (words.length) bases.push(words[words.length - 1])
  }
  return bases
}

/**
 * The member functions a class body declares, by name: whether the declaration says `virtual`
 * (or `= 0`), whether it says `override`, and how many parameters it takes. A body's own text is
 * skipped, so a call inside an inline definition is not read as a declaration of the class.
 */
function readMembers(text, open, close) {
  const members = new Map()
  let statementStart = open + 1
  let i = open + 1
  while (i < close) {
    const c = text[i]
    if (c === '{') {
      const end = closeBrace(text, i)
      if (end < 0) break
      i = end + 1
      statementStart = i
      continue
    }
    if (c === ';' || c === '}') {
      statementStart = i + 1
      i++
      continue
    }
    if (!/[A-Za-z_]/.test(c)) {
      i++
      continue
    }
    let j = i
    while (j < close && /[A-Za-z_0-9]/.test(text[j])) j++
    const name = text.slice(i, j)
    let k = j
    while (k < close && /\s/.test(text[k])) k++
    if (text[k] !== '(') {
      i = j
      continue
    }
    const paramsEnd = closeParen(text, k)
    if (paramsEnd < 0) {
      i = j
      continue
    }
    const rest = text.slice(paramsEnd + 1, paramsEnd + 200)
    const tail = endingOf(rest)
    if (!tail) {
      i = j
      continue
    }
    const head = text.slice(statementStart, i)
    const arity = splitArguments(text.slice(k + 1, paramsEnd)).length
    const known = members.get(name)
    // A body written `= 0` is a pure virtual declaration: the method is virtual whether or not
    // the word `virtual` is repeated beside it.
    members.set(name, {
      virtual: /\bvirtual\b/.test(head) || tail.pure || Boolean(known?.virtual),
      override: tail.override || Boolean(known?.override),
      arity: known ? known.arity : arity,
    })
    i = j
  }
  return members
}

/**
 * What follows a member's parameter list: `;` (a declaration), `{` (an inline definition), or null
 * when the shape is neither. It is read word by word rather than by one regular expression: the
 * qualifiers `const`, `noexcept(...)`, `override` and `final` come in any order, and a pattern that
 * tries them in every order backtracks far enough on a long declaration to cost minutes over the
 * whole repository.
 */
function endingOf(rest) {
  let i = 0
  let override = false
  let pure = false
  for (;;) {
    while (i < rest.length && /\s/.test(rest[i])) i++
    if (i >= rest.length) return null
    const c = rest[i]
    if (c === ';') return { declaration: true, pure, override }
    if (c === '{') return { declaration: false, pure, override }
    if (c === '=') {
      let j = i + 1
      while (j < rest.length && /\s/.test(rest[j])) j++
      if (rest[j] === '0') {
        pure = true
        i = j + 1
        continue
      }
      return null
    }
    const word = /^[A-Za-z_]\w*/.exec(rest.slice(i))
    if (!word) return null
    if (word[0] === 'const' || word[0] === 'final' || word[0] === 'noexcept') {
      i += word[0].length
      // `noexcept(...)` carries a condition in parentheses.
      let k = i
      while (k < rest.length && /\s/.test(rest[k])) k++
      if (word[0] === 'noexcept' && rest[k] === '(') {
        const end = closeParen(rest, k)
        if (end < 0) return null
        i = end + 1
      }
      continue
    }
    if (word[0] === 'override') {
      override = true
      i += word[0].length
      continue
    }
    return null
  }
}

/** Every class a file defines: its name, the range of its body, its bases, and its members. */
function indexClasses(text) {
  const classes = []
  CLASS_HEAD.lastIndex = 0
  let m
  while ((m = CLASS_HEAD.exec(text))) {
    // `enum class Kind {` is a scoped enum: its `{` holds enumerators, not members.
    if (/\benum\s+$/.test(text.slice(Math.max(0, m.index - 8), m.index))) continue
    const open = m.index + m[0].length - 1
    const close = closeBrace(text, open)
    if (close < 0) continue
    classes.push({ name: m[1], open, close, bases: basesOf(m[2]), members: readMembers(text, open, close) })
  }
  return classes
}

/** The class whose body holds `at`, innermost when classes are nested, or null. */
function enclosingClass(classes, at) {
  let found = null
  for (const cls of classes) {
    if (at > cls.open && at < cls.close && (!found || cls.open > found.open)) found = cls
  }
  return found
}

/** The class a definition belongs to: the qualifier it is written with, else the body it is in. */
function attachOwners(file, classes, funcs) {
  for (const definitions of funcs.values()) {
    for (const definition of definitions) {
      if (!definition.owner) definition.owner = enclosingClass(classes, definition.open)?.name ?? null
    }
  }
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
 * code (`src/kernel/execute/pass.cpp`), so the operator returns no value without failing. Everything
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
 * The depth every walk stops at: `MAX_DEPTH` nested calls from the kernel, whichever file they are
 * written in. A call beyond it is not followed; the wide reading below says so as a gap.
 */
const MAX_DEPTH = 6

/**
 * The raises an operator can reach from `name`, following the helpers of its own file and the arms
 * a constant argument selects. `seen` holds the name plus the bindings it was scanned with, so a
 * recursive helper is walked once per branch rather than for ever.
 *
 * This is the **in-file** reading, and it is what the red side takes; the wide reading, which is
 * what says whether a `panic` declaration is kept, is `reachAcross` below. The two are separate
 * functions because they answer different questions: see "how far the analysis follows a call" in
 * this file's header.
 */

/** Every name a call was made through that no source defines: a macro, a constructor, the library. */
const unreadCalls = new Set()

/**
 * What the wide reading visits before it stops. The graph a kernel reaches is the repository's own
 * code, and a kernel that hands off to `ArgsView`'s readers reaches them all; a walk with no bound
 * on it costs minutes and says no more than the bound does. Passing the bound is a **gap**: the
 * answer is "not proven", never "raises nothing".
 */
const WALK_BUDGET = 4000

/**
 * The raises a kernel reaches, letting the walk leave the kernel's file, breadth first.
 *
 * Breadth first is what makes the answer cheap as well as honest: the raise a declaration is about
 * is normally a call or two from the kernel (a conversion's type layer, a value's reader), and a
 * level is finished before the next one is entered, so the first raise found ends the walk. A depth
 * first search would follow the first call -- `ArgsView::slot` and every override of it -- to the
 * bottom before it looked at the argument beside it.
 *
 * What the walk cannot resolve is recorded in `gaps` and stops nothing: another call may still be
 * followed to a raise, and one raise is all a `panic` declaration needs. A call the scan cannot
 * resolve is not silence -- it is reported with its reason, and the operator becomes `unproven`
 * rather than `unkept`. That is the direction that matters: a raise this scan cannot find must
 * never read as "there is no raise".
 */
function reachAcross(state, kernels) {
  const out = { sites: [], gaps: [], visited: 0, overBudget: false }
  const seen = new Set()
  let frontier = kernels.map(kernel => ({ file: kernel.file, name: kernel.name, bindings: {}, chain: [] }))
  for (let depth = 0; depth <= MAX_DEPTH && frontier.length; depth++) {
    const next = []
    for (const node of frontier) {
      const signature = `${node.file}|${node.name}|${JSON.stringify(node.bindings)}`
      if (seen.has(signature)) continue
      seen.add(signature)
      if (++out.visited > WALK_BUDGET) {
        out.overBudget = true
        continue
      }
      const where = [...node.chain, node.name]
      const funcs = state.functions.get(node.file)
      const text = state.texts.get(node.file)
      const definitions = funcs?.get(node.name)
      if (!definitions) continue
      for (const definition of definitions) {
        const body = restrictSwitches(text.slice(definition.open, definition.close + 1), node.bindings)
        for (const raise of raisesOf(body)) {
          out.sites.push({
            ...raise,
            from: node.name,
            file: node.file,
            internal: INTERNAL_RAISES.has(raise.kind),
            route: where,
          })
        }
        // A call this scan does not even spell as a call -- `table[i](value)`, a pointer taken out
        // of an array -- reaches a definition the text does not name. A gap, not silence.
        const subscript = /\]\s*\(/g
        let s
        while ((s = subscript.exec(body))) {
          out.gaps.push({
            call: 'a call through a subscript',
            file: node.file,
            route: where,
            reason: 'the call is made through a value the text does not name',
          })
        }
        const callRe = /\b([A-Za-z_]\w*)\s*(?:<[^<>;{}()]*>\s*)?\(/g
        let m
        while ((m = callRe.exec(body))) {
          const callee = m[1]
          if (callee === node.name) continue
          const callOpen = body.indexOf('(', m.index + m[0].length - 1)
          const callClose = callOpen < 0 ? -1 : closeParen(body, callOpen)
          if (callClose < 0) continue
          const passed = splitArguments(body.slice(callOpen + 1, callClose))
          const reached = resolveCallee(callee, receiverOf(body, m.index), passed.length, node.file, definition)
          if (reached.gap) {
            out.gaps.push({ call: callee, file: node.file, route: where, reason: reached.gap })
            continue
          }
          if (reached.unread) {
            unreadCalls.add(callee)
            continue
          }
          const calleeBindings = {}
          for (const target of reached.definitions) {
            target.definition.params.forEach((param, index) => {
              if (!param || index >= passed.length) return
              const asEnum = enumeratorOf(passed[index])
              if (asEnum) calleeBindings[param] = asEnum
              else if (/^[A-Za-z_]\w*$/.test(passed[index].trim()) && passed[index].trim() in node.bindings)
                calleeBindings[param] = node.bindings[passed[index].trim()]
            })
          }
          for (const target of reached.definitions) {
            next.push({ file: target.file, name: callee, bindings: calleeBindings, chain: where })
          }
        }
      }
    }
    // One raise is the whole answer: a `panic` declaration is kept by the first one the walk finds.
    if (out.sites.some(site => !site.internal)) break
    if (depth === MAX_DEPTH && next.length) {
      out.gaps.push({
        call: next[0].name,
        file: next[0].file,
        route: [...next[0].chain, next[0].name],
        reason: `the call is deeper than ${MAX_DEPTH} calls from the kernel`,
      })
    }
    frontier = next
  }
  return out
}

/**
 * The raises an operator reaches **within the kernel's own file**: the kernel, the helpers written
 * beside it, and the arms a constant argument selects. This is what the red side reads, and it is
 * deliberately no wider: one raise is all that answer needs, and following a call out of the file
 * for it would accuse an operator of a fault reached through a name the text does not settle.
 */
function walkRaisesWithin(file, name, bindings, depth, seen, out, chain = []) {
  const where = [...chain, name]
  if (depth > MAX_DEPTH) return out
  const signature = `${file}|${name}|${JSON.stringify(bindings)}`
  if (seen.has(signature)) return out
  seen.add(signature)
  const funcs = functions.get(file)
  const text = texts.get(file)
  if (!funcs) return out
  const definitions = funcs.get(name)
  if (!definitions) return out
  for (const definition of definitions) {
    const body = restrictSwitches(text.slice(definition.open, definition.close + 1), bindings)
    for (const raise of raisesOf(body)) {
      out.sites.push({ ...raise, from: name, file, internal: INTERNAL_RAISES.has(raise.kind), route: where })
    }

    const callRe = /\b([A-Za-z_]\w*)\s*(?:<[^<>;{}()]*>\s*)?\(/g
    let m
    while ((m = callRe.exec(body))) {
      const callee = m[1]
      if (callee === name) continue
      const callOpen = body.indexOf('(', m.index + m[0].length - 1)
      const callClose = callOpen < 0 ? -1 : closeParen(body, callOpen)
      if (callClose < 0) continue
      const passed = splitArguments(body.slice(callOpen + 1, callClose))
      if (!funcs.has(callee)) continue
      const reached = { definitions: funcs.get(callee).map(one => ({ file, definition: one })) }
      const calleeBindings = {}
      for (const target of reached.definitions) {
        target.definition.params.forEach((param, index) => {
          if (!param || index >= passed.length) return
          const asEnum = enumeratorOf(passed[index])
          if (asEnum) calleeBindings[param] = asEnum
          else if (/^[A-Za-z_]\w*$/.test(passed[index].trim()) && passed[index].trim() in bindings)
            calleeBindings[param] = bindings[passed[index].trim()]
        })
      }
      for (const target of reached.definitions) {
        walkRaisesWithin(target.file, callee, calleeBindings, depth + 1, seen, out, where)
      }
    }
  }
  return out
}

/** Every definition named `name` in the sources, with the file it is written in. */
function definitionsInTree(name) {
  return definitionsByName.get(name) ?? []
}

/**
 * The override targets of a class, cached: the walk asks the same base of many call sites, and
 * each answer costs a pass over every class the sources define.
 */
const derivedCache = new Map()

/** Whether `className` is `base` or derives from it, following the bases the sources write. */
function derivesFrom(className, base, seen = new Set()) {
  if (className === base) return true
  if (seen.has(className)) return false
  seen.add(className)
  for (const entry of classesByName.get(className) ?? []) {
    for (const parent of entry.cls.bases) {
      if (derivesFrom(parent, base, seen)) return true
    }
  }
  return false
}

/** Every class name the sources define that is an override target of `base`, `base` included. */
function overrideTargets(base) {
  const cached = derivedCache.get(base)
  if (cached) return cached
  const out = new Set([base])
  for (const name of classesByName.keys()) {
    if (name === base) continue
    if (derivesFrom(name, base)) out.add(name)
  }
  derivedCache.set(base, out)
  return out
}

/** The class declaration of `name` a member is looked up in, or null when several say it. */
function classNamed(name) {
  const entries = classesByName.get(name) ?? []
  return entries.length === 1 ? entries[0] : null
}

/** The declaration of member `name` in `className` or in its bases, or null. */
const memberCache = new Map()

function memberNamed(className, name, seen = new Set()) {
  const key = className + '|' + name
  if (memberCache.has(key)) return memberCache.get(key)
  if (seen.has(className)) return null
  seen.add(className)
  const entry = classNamed(className)
  if (!entry) {
    memberCache.set(key, null)
    return null
  }
  const member = entry.cls.members.get(name)
  if (member) return { member, owner: className }
  for (const parent of entry.cls.bases) {
    const inherited = memberNamed(parent, name, seen)
    if (inherited) {
      memberCache.set(key, inherited)
      return inherited
    }
  }
  memberCache.set(key, null)
  return null
}

/** The identifier a call is made through: `a->b(` and `a.b(` give `a`; `(*b)(` gives `'*'`. */
function receiverOf(body, at) {
  let i = at - 1
  while (i >= 0 && /\s/.test(body[i])) i--
  if (i < 0) return null
  const arrow = body[i] === '>' && body[i - 1] === '-' ? 2 : body[i] === '.' ? 1 : 0
  if (!arrow) return body[i] === '*' ? '*' : null
  let j = i - arrow
  while (j >= 0 && /\s/.test(body[j])) j--
  if (j < 0) return null
  const end = j + 1
  while (j >= 0 && /[A-Za-z_0-9]/.test(body[j])) j--
  const receiver = body.slice(j + 1, end)
  // A receiver that is not a name -- `make().run()`, `array[0].run()` -- is kept as the character
  // it ends with, so that the resolution refuses it by name instead of guessing a class.
  return /^[A-Za-z_]\w*$/.test(receiver) ? receiver : receiver.slice(-1) || body.slice(j + 1, end)
}

/** The type name a variable is declared with in the definition's own text, or null. */
function typeOfVariable(name, definition, text) {
  if (name === 'this') return definition.owner
  const index = definition.params.indexOf(name)
  if (index >= 0 && definition.paramTypes[index]) return definition.paramTypes[index]
  const body = text.slice(definition.from, definition.close + 1)
  // The `g` is what makes `exec` move: without it the same match comes back for ever, and the
  // answer is never returned. The last declaration of the name is the one the call is inside.
  const re = new RegExp(
    `([A-Za-z_]\\w*(?:::\\w+)*)\\s+(?:\\*+|&+)?\\s*${name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b\\s*(?==|;|\\{|,|\\))`,
    'g',
  )
  let found = null
  let m
  while ((m = re.exec(body))) {
    const words = m[1].match(/[A-Za-z_]\w*/g) ?? []
    if (words.length) found = words[words.length - 1]
  }
  return found
}

/**
 * The definitions a call at this site reaches, and -- when it reaches none it can name -- why.
 *
 * Three answers are possible and the difference between them is the point:
 *
 *   - `definitions`: what the call can run. For a member call that is the one definition of a
 *     method that is not virtual, and every definition of a virtual one in the classes that
 *     derive from the class declaring it: a virtual call runs one of them, and every one of them
 *     is a reading of the same call;
 *   - `gap`: the call is a call of something this scan can see and cannot resolve -- an ambiguous
 *     overload, a receiver whose type the text does not write, a class name two classes answer to,
 *     a call through a pointer, a virtual method defined nowhere in the tree. Nothing is claimed
 *     about it, and the operator becomes `unproven`;
 *   - `unread`: the name is defined nowhere in the sources, so it is not a function of the
 *     repository (a macro, a constructor, the C++ library) and the scan has no text to read. That
 *     is counted, because a scan whose entrances are all macros would read nothing and say so.
 */
function resolveCallee(name, receiver, argCount, file, definition) {
  const memoKey = `${file}|${definition.open}|${name}|${receiver}|${argCount}`
  const memo = resolveCache.get(memoKey)
  if (memo) return memo
  const answer = resolveCalleeUncached(name, receiver, argCount, file, definition)
  resolveCache.set(memoKey, answer)
  return answer
}

/** The resolution of one call site, cached: a call site inside a helper is asked once per entry. */
const resolveCache = new Map()

function resolveCalleeUncached(name, receiver, argCount, file, definition) {
  const here = functions.get(file)?.get(name)
  if (here) return { definitions: here.map(one => ({ file, definition: one })), gap: null }
  if (receiver) {
    if (receiver === '*')
      return { definitions: [], gap: `'${name}' is called through a pointer: the definition it runs is not in the text` }
    if (!/^[A-Za-z_]\w*$/.test(receiver) && receiver !== 'this')
      return { definitions: [], gap: `the receiver of '${name}' is not a name the scan can read` }
    const type = typeOfVariable(receiver, definition, texts.get(file))
    if (!type) return { definitions: [], gap: `the type of '${receiver}' is not written where '${name}' is called` }
    const entry = classNamed(type)
    if (!entry) {
      const count = (classesByName.get(type) ?? []).length
      return {
        definitions: [],
        gap: count
          ? `'${type}', the type of '${receiver}', names ${count} classes in the sources`
          : `'${type}', the type of '${receiver}', is not a class the sources define`,
      }
    }
    const declared = memberNamed(type, name)
    if (!declared) return { definitions: [], gap: `'${type}' declares no member '${name}'` }
    const classes = declared.member.virtual ? overrideTargets(declared.owner) : new Set([declared.owner])
    const found = []
    for (const candidate of definitionsInTree(name)) {
      if (!candidate.definition.owner) continue
      if (!classes.has(candidate.definition.owner)) continue
      if (candidate.definition.params.length !== argCount) continue
      found.push(candidate)
    }
    if (!found.length)
      return {
        definitions: [],
        gap: declared.member.virtual
          ? `'${name}' is virtual in '${declared.owner}' and no class the sources read defines it`
          : `'${name}', a member of '${declared.owner}', is defined nowhere the sources read`,
      }
    return { definitions: found, gap: null }
  }
  const candidates = definitionsInTree(name).filter(candidate => candidate.definition.params.length === argCount)
  if (candidates.length === 1) return { definitions: candidates, gap: null }
  if (candidates.length > 1)
    return { definitions: [], gap: `'${name}' is ${candidates.length} functions of the sources that take ${argCount} arguments` }
  // Nothing in the sources is called `name`, so the call is either one of something the scan has no
  // text for (a macro, a constructor, the C++ library) or one of a *value*: a function pointer, a
  // functor, a parameter of function type. The two read differently, and only the second is a gap:
  // a name the body declares is a value here, and what it holds is not in the text.
  if (definition.params.includes(name))
    return { definitions: [], gap: `'${name}' is a parameter of the kernel, so this call goes through a value` }
  const body = texts.get(file).slice(definition.from, definition.close + 1)
  // A value that is called is written either as a declaration of a pointer to a function
  // (`slot_t (*handOff)(...) = ...`) or as any other assignment (`auto f = ...`), and in both the
  // name is bound to a value rather than to a function of the sources.
  const asValue = new RegExp(`\\(\\s*\\*\\s*${name}\\s*\\)|\\b${name}\\b\\s*(?:=|;|\\{|\\[)`)
  if (asValue.test(body))
    return { definitions: [], gap: `'${name}' is a value here, so the call goes through a function pointer or a functor` }
  return { definitions: [], gap: null, unread: true }
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
 * default, either as a call with literals or as a table of `CppPanicStatement` entries.
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
  let calls = 0
  let tables = 0
  for (const file of sourceFiles) {
    const raw = readTracked(file)
    // The URIs and the answers are literals, so the comments are blanked out and the literals are
    // kept: `blank` would erase the very text this entrance states.
    const text = blankComments(raw)
    const re = /CppRegistration\s*(?:&)?\s*\w*\s*\(\s*"([^"]+)"\s*,\s*(true|false)\s*\)/g
    let m
    while ((m = re.exec(text))) {
      record(m[1], m[2] === 'true', file, lineAt(text, m.index))
      ++calls
    }
    tables += readStatements(file, blank(raw), text, record)
  }
  return { stated, calls, tables }
}

/**
 * A table of the C++ entrance's statements: `constexpr CppPanicStatement <name>[] = { ... }`. The
 * type is what the scan matches on, because a table of string and boolean literals says nothing
 * about which of the two the boolean is; the entries are then read off the text with the literals
 * kept. A table whose head this scan finds and whose entries it cannot parse is refused rather
 * than read as an empty statement, because an empty answer and a missing one look the same in a
 * report and only one of them is a reading.
 */
const STATEMENT_TABLE = /constexpr\s+(?:[\w:]+::)*CppPanicStatement\s+[A-Za-z_]\w*\s*\[\s*\]\s*=\s*\{/g
const STATEMENT_ENTRY = /\{\s*"([^"]+)"\s*,\s*(true|false)\s*\}/g

function readStatements(file, blanked, text, record) {
  let count = 0
  STATEMENT_TABLE.lastIndex = 0
  let m
  while ((m = STATEMENT_TABLE.exec(blanked))) {
    const open = m.index + m[0].length - 1
    const close = closeBrace(blanked, open)
    if (close < 0) continue
    const body = text.slice(open, close + 1)
    STATEMENT_ENTRY.lastIndex = 0
    let p
    let entries = 0
    while ((p = STATEMENT_ENTRY.exec(body))) {
      record(p[1], p[2] === 'true', file, lineAt(blanked, open + p.index))
      ++entries
    }
    if (!entries) {
      console.error(`[${NAME}] ${file}:${lineAt(blanked, open)}: the statement table states no entry this scan can read`)
      console.error(`[${NAME}] a table of statements with nothing in it is refused: that answer would read as "nothing to compare"`)
      process.exit(2)
    }
    count += entries
  }
  return count
}

/**
 * The module each implementation map answers on, read from the C++ that states it.
 *
 * An implementation map is keyed by the module-internal name (`min_i`, `find`, `op/div_i`), so the
 * module is what a key means. The module is not the directory a file sits in -- `src/stdlib/`
 * answers the protocol `rand` -- and it is not inferred from the declaration side, which would be
 * the check reading its own answer. It is stated twice in C++, and this joins the two:
 *
 *   1. `registerExecutorFactory("<module>", ...)` names the protocol and writes the executor that
 *      answers it (`src/stdlib/re/module.cpp`, `src/stdlib/std/builtin/modules/globals.cpp`);
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
const classesByName = new Map()
for (const file of sourceFiles) {
  const raw = readTracked(file)
  raws.set(file, raw)
  const text = blank(raw)
  texts.set(file, text)
  const classes = indexClasses(text)
  for (const cls of classes) {
    if (!classesByName.has(cls.name)) classesByName.set(cls.name, [])
    classesByName.get(cls.name).push({ file, cls })
  }
  const funcs = indexFunctions(text)
  attachOwners(file, classes, funcs)
  functions.set(file, funcs)
  for (const [name, definitions] of funcs) {
    if (!definitionsByName.has(name)) definitionsByName.set(name, [])
    for (const definition of definitions) definitionsByName.get(name).push({ file, definition })
  }
}


/** What the wide reading reads: the same indexes; it decides for itself whether to leave a file. */
const across = { texts, functions }

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
    operators.set(key, { uri: key, kernels: new Set(), sites: [], files: new Set(), first: registration, definitions: [] })
  }
  const operator = operators.get(key)
  operator.kernels.add(registration.kernel)
  operator.files.add(registration.file)
  const resolved = resolveKernel(registration.kernel, registration.file)
  if (!resolved) {
    unresolved.add(`${registration.file}:${registration.line}: ${key} -> ${registration.kernel}`)
    continue
  }
  operator.definitions.push({ file: resolved.file, name: registration.kernel })
  const found = walkRaisesWithin(resolved.file, registration.kernel, {}, 0, new Set(), { sites: [] })
  for (const site of found.sites) operator.sites.push(site)
}

const { stated: declarations, calls: statementCalls, tables: statementTable } = readDeclarations(cmlFiles, sourceFiles)

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
const unproven = []
const keptAcross = []
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
  else if (program.length) green.push(reading)
  else if (!promisesFault) {
    green.push(reading)
    if (internal.length) internalOnly.push(reading)
  } else {
    // The declaration states a fault and nothing in the kernel's own file raises. That is the
    // question this run answers a second time, now letting the walk leave the file: "raises
    // nothing" is a claim about everything the kernel can reach, and a helper in another file is
    // one of the ways it is reached.
    const reach = reachAcross(across, operator.definitions)
    const beyond = reach.sites.filter(site => !site.internal)
    if (beyond.length) {
      reading.rawKinds = [...new Set(beyond.map(s => s.kind))].sort()
      reading.acrossSites = beyond
      green.push(reading)
      keptAcross.push(reading)
    } else if (reach.gaps.length || reach.overBudget) {
      reading.gaps = reach.gaps
      reading.overBudget = reach.overBudget
      reading.visited = reach.visited
      unproven.push(reading)
    } else unkept.push(reading)
  }
}

const line = message => console.log(message)
line(`[${NAME}] ${sourceFiles.length} source files read from \`git ls-files\` under ${scanRoot}; ${operators.size} operators registered in ${new Set(registrations.map(r => r.file)).size} implementation maps`)
// The size of what this run can say, stated before what it says: an operator whose declaration it
// cannot read is not an operator it has cleared, and the two look the same in a red list.
const visible = [...operators.keys()].filter(key => (declarations.get(key) ?? []).length > 0).length
line(`[${NAME}] ${visible} of ${operators.size} registered operators state \`panic\` in a declaration this scan can read; the C++ entrance writes ${statementCalls} statements as calls and ${statementTable} in tables. The rest state nothing, which reads as "no statement", not as "answered".`)
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
line(`[${NAME}] ${red.length} operators raise and state no \`panic\`; ${green.length} agree; ${unkept.length} state a fault their implementation does not raise; ${unproven.length} state one this scan could not prove or disprove`)
if (keptAcross.length || unreadCalls.size || unproven.length) {
  line(`[${NAME}] the second reading, which may follow a call into another file: ${keptAcross.length} declarations are kept by a raise that file holds, ${unproven.length} are unproven, and ${unreadCalls.size} names were called that no source defines (a macro, a constructor, the C++ library) and so have no text to read`)
}
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
for (const operator of keptAcross) {
  const seen = new Set()
  const reads = []
  for (const site of operator.acrossSites) {
    const key = `${site.file}|${site.from}|${site.kind}`
    if (seen.has(key)) continue
    seen.add(key)
    reads.push(`${site.kind} at ${path.relative(scanRoot, site.file)}#${site.from} (${site.route.join(' -> ')})`)
  }
  line(`  ${operator.uri} keeps \`panic\`: ${reads.join('; ')}`)
}
for (const operator of unproven) {
  const where = `${path.relative(scanRoot, operator.first.file)}:${operator.first.line}`
  line(`  ${where}: ${operator.uri} states \`panic\` and nothing it reaches is proven to raise; the walk stopped at ${operator.visited} definitions:`)
  if (operator.overBudget) line(`    the walk visited its whole budget of ${WALK_BUDGET} definitions and did not finish`)
  for (const gap of operator.gaps.slice(0, 3)) {
    line(`    ${gap.reason} -- ${path.relative(scanRoot, gap.file)}#${gap.route.join(' -> ')}`)
  }
  if (operator.gaps.length > 3) line(`    and ${operator.gaps.length - 3} more`)
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

if (unproven.length) {
  console.error(`[${NAME}] ${unproven.length} declarations state \`panic\` and the scan could not follow every call they make: an unread call is not a raise that does not happen, so this is neither a pass nor the \`unkept\` answer`)
  process.exit(1)
}

process.exit(red.length || unkept.length ? 1 : 0)
