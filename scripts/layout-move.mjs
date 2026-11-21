#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The move generator: reads the one table (`docs/design/architecture/layout-map.cn.md`) and
// produces everything a physical move needs, without performing it on its own.
//
// What it produces, all of it deterministic (two runs are byte-identical):
//   * `git-mv.txt`      the `git mv` sequence, in an order that never collides;
//   * `rewrites.tsv`    one line per rewritten occurrence in a build-tool file, plus the
//                       include strings it would rewrite (today: none -- see "why no include
//                       string moves" below);
//   * `layout-blocks.cmake`  one build target per block, with only the lower layers' headers on
//                       its include path;
//   * `violations.tsv`  every cross-layer `#include` in the moved tree, one line each, with the
//                       block it comes from and the block it reaches for;
//   * `coverage.txt`    the coverage reading: top-level entries vs table rows.
//
// Why no include string moves: a block keeps its own `camel/`-rooted header tree at the same
// spelling (`include/camel/core/x.h` -> `kernel/include/camel/core/x.h`, still named
// `camel/core/x.h`), and a `src/`-rooted spelling (`core/module/userdef.h`) keeps working because
// the block's root moves with it (`-Isrc` -> `-Ikernel`). So the physical move changes no
// `#include` text at all; what it changes is which roots are on the compile line, and that is
// `layout-blocks.cmake`. The scan still runs and still says so -- "0 rewritten" is a reading, not
// a skip.
//
// Usage:
//   node scripts/layout-move.mjs --out <dir>              generate into <dir>, touch nothing else
//   node scripts/layout-move.mjs --apply --root <tree>    move and rewrite inside <tree>
//   node scripts/layout-move.mjs --check                  coverage and violations only
//
// `--apply` refuses a branch named develop, main or master: the drill is a throwaway branch in a
// throwaway tree, never the mainline.

import fs from 'node:fs'
import path from 'node:path'
import { execFileSync } from 'node:child_process'

const MAP_REL = 'docs/design/architecture/layout-map.cn.md'
const GENERATED = 'cmake/layout-blocks.cmake'
const BLOCK_PREFIX = 'camel_layout_'

// The layer order the include scan judges by: index = depth. A file may include its own block and
// every block below it; reaching above is a cross-layer reference and is listed, not hidden.
const LAYERS = [
  'third_party',
  'kernel_utils',
  'kernel_core',
  'kernel_runtime',
  'parse',
  'compile',
  'lib_std',
  'lib_module',
  'unplaced',
  'tools',
]
const layerOf = (block) => {
  const i = LAYERS.indexOf(block)
  return i < 0 ? LAYERS.indexOf('unplaced') : i
}

// ---------------------------------------------------------------------------------------------
// the table
// ---------------------------------------------------------------------------------------------

function readTable (root) {
  const file = path.join(root, MAP_REL)
  if (!fs.existsSync(file)) fail(`the map is missing: ${MAP_REL}`)
  const lines = fs.readFileSync(file, 'utf8').split('\n')
  const tables = [] // {heading, rows: [{cells, line}]}
  let current = null
  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i]
    if (/^##\s/.test(raw)) {
      current = { heading: raw.replace(/^##\s+/, '').trim(), rows: [] }
      tables.push(current)
      continue
    }
    if (!current) continue
    if (!/^\s*\|/.test(raw)) continue
    // The header row is the one whose next line is the `|---|` separator. Recognising it by that
    // rule (and not by its wording) is what keeps a second table with different column names from
    // counting its own header as a row.
    if (lines.slice(i + 1).find((l) => l.trim())?.match(/^\s*\|[\s:|-]+\|\s*$/)) continue
    const cells = raw
      .trim()
      .replace(/^\|/, '')
      .replace(/\|$/, '')
      .split('|')
      // A path cell is written in backticks so it reads as a path in the rendered page; the table
      // is the input, so the backticks come off here rather than being spelled twice.
      .map((s) => s.trim().replace(/^`(.*)`$/, '$1'))
    // The rule row and the separator row.
    if (cells.every((c) => /^:?-{3,}:?$/.test(c))) continue
    
    current.rows.push({ cells, line: i + 1 })
  }
  return tables
}

function pickTable (tables, prefix) {
  const t = tables.find((x) => x.heading.startsWith(prefix))
  if (!t) fail(`the map has no table headed "${prefix}"`)
  return t
}

// The four container rows in table A name table B; the expansion below is what makes them
// concrete. Anything the expansion does not know about is an error, never a silent skip.
const CONTAINERS = new Set(['src/', 'include/', 'modules/', 'stdlib/'])

function parseRows (table) {
  return table.rows.map(({ cells, line }) => {
    if (cells.length < 5) fail(`${MAP_REL}:${line}: a row must have 5 cells, it has ${cells.length}`)
    const [from, to, block, status, note] = cells
    if (!['搬', '留'].includes(status)) {
      fail(`${MAP_REL}:${line}: unknown status "${status}" (the two are 搬, 留: every path is decided)`)
    }
    return { from, to, block, status, note, line }
  })
}

// `modules/<模块>/`, `stdlib/<模块>.cml` and `stdlib/signatures/<模块>.cml` are rule rows: the
// placeholder expands to every actual name, so the table stays one line per rule and the script
// does the enumerating. The names come from the directory the placeholder names, so a name added
// there is a row the script refuses to guess about.
function expandRow (row, root) {
  if (!row.from.includes('<模块>')) return [row]
  const isModuleDir = row.from.startsWith('modules/')
  const dir = path.posix.dirname(row.from)
  const names = fs
    .readdirSync(path.join(root, dir), { withFileTypes: true })
    .filter((e) => (isModuleDir ? e.isDirectory() : e.isFile() && e.name.endsWith('.cml')))
    .map((e) => (isModuleDir ? e.name : e.name.replace(/\.cml$/, '')))
    .sort()
  const out = []
  for (const name of names) {
    if (dir === 'stdlib' && name === 'prelude') continue // the exception row names it
    out.push({
      ...row,
      from: row.from.replace('<模块>', name),
      to: row.to.replace(/<模块>/g, name),
      block: row.block.replace(/<模块>/g, name === 'std' ? 'std' : name),
    })
  }
  return out
}

// ---------------------------------------------------------------------------------------------
// coverage: every top-level entry has exactly one row in table A
// ---------------------------------------------------------------------------------------------

function readTopLevel (root) {
  return fs
    .readdirSync(root, { withFileTypes: true })
    .map((e) => (e.isDirectory() ? `${e.name}/` : e.name))
    .sort()
}

function checkCoverage (rowsA, root) {
  const top = readTopLevel(root)
  const seen = new Map()
  for (const row of rowsA) {
    const key = row.from
    if (seen.has(key)) fail(`table A names ${key} twice (lines ${seen.get(key)} and ${row.line})`)
    seen.set(key, row.line)
  }
  const missing = top.filter((name) => !seen.has(name))
  const extra = [...seen.keys()].filter((name) => !top.includes(name))
  const out = []
  out.push(`top-level entries: ${top.length}`)
  out.push(`table A rows:      ${rowsA.length}`)
  out.push(`equal:             ${top.length === rowsA.length && !missing.length && !extra.length ? 'yes' : 'no'}`)
  if (missing.length) out.push(`missing from the table: ${missing.join(', ')}`)
  if (extra.length) out.push(`in the table, not on disk: ${extra.join(', ')}`)
  if (top.length !== rowsA.length || missing.length || extra.length) {
    process.stdout.write(out.join('\n') + '\n')
    fail('the table does not cover the top level; see the lines above')
  }
  return out
}

function checkExpansion (rowsB, root) {
  // `rowsB` is the expanded list: a rule row has already become one row per actual name, so
  // "every name has a row" is the same check as "every entry on disk is listed".
  const problems = []
  const readings = []
  const need = (dir, depth) => {
    const actual = fs
      .readdirSync(path.join(root, dir), { withFileTypes: true })
      .map((e) => `${dir}/${e.name}${e.isDirectory() ? '/' : ''}`)
      .sort()
    const listed = rowsB
      .filter((r) => {
        if (!r.from.startsWith(`${dir}/`)) return false
        const rest = r.from.slice(dir.length + 1).replace(/\/$/, '')
        if (!rest) return false // the container's own row names the container, not a child
        return rest.split('/').length === depth
      })
      .map((r) => r.from)
    const uncovered = actual.filter((a) => !listed.includes(a))
    const phantom = listed.filter((l) => !actual.includes(l))
    if (uncovered.length) problems.push(`${dir}: not in table B: ${uncovered.join(', ')}`)
    if (phantom.length) problems.push(`${dir}: in table B, not on disk: ${phantom.join(', ')}`)
    readings.push(`${dir}/ at depth ${depth}: ${actual.length} on disk, ${listed.length} rows`)
  }
  need('src', 1)
  need('src/passes', 1) // `src/passes/trans/rulesets/` is a second level below `src/`
  // The containers whose entries the decisions split by file or by hand: a file left out of the
  // table would be a file the move silently leaves behind, so each is checked against the disk.
  for (const dir of [
    'src/execute',
    'src/service',
    'src/service/codegen',
    'src/service/profiler',
    'src/passes/sched',
    'src/passes/trans',
  ]) {
    need(dir, 1)
  }
  need('include/camel', 1)
  for (const dir of ['include/camel/execute', 'include/camel/passes', 'include/camel/passes/trans']) {
    need(dir, 1)
  }
  need('modules', 1)
  need('stdlib', 1)
  need('stdlib/signatures', 1)
  if (problems.length) {
    process.stdout.write(problems.join('\n') + '\n')
    fail('table B does not cover its container')
  }
  return readings
}

// ---------------------------------------------------------------------------------------------
// table C: the decisions
// ---------------------------------------------------------------------------------------------

// Table C is the decision record, not a question list. Every item carries 已定 and the target the
// decision named; the table A/B row for that path has to agree with it. Reading it here is what
// keeps it from drifting back into a list of open questions: an item without a decision stops the
// run instead of quietly waiting for a reader.
function checkDecisions (table) {
  const decided = []
  for (const { cells, line } of table.rows) {
    if (cells.length < 7) fail(`${MAP_REL}:${line}: a table C row must have 7 cells, it has ${cells.length}`)
    const [id, , , , , status, target] = cells
    if (!/^U\d+$/.test(id)) fail(`${MAP_REL}:${line}: a table C row has no U-number ("${id}")`)
    if (status !== '已定') fail(`${MAP_REL}:${line}: ${id} is "${status}", not 已定`)
    if (!target) fail(`${MAP_REL}:${line}: ${id} has no target`)
    decided.push(id)
  }
  return decided
}

// ---------------------------------------------------------------------------------------------
// moves
// ---------------------------------------------------------------------------------------------

function planMoves (rowsA, rowsB) {
  const moves = []
  const push = (row) => {
    if (row.status !== '搬') return
    if (row.to === '—') fail(`line ${row.line}: status 搬 but no target`)
    // A row whose target is prose ("拆到表 B", "拆到本表以下四行") names a container, not a move:
    // its children carry the real targets. Reading it as a path would move a directory to a name
    // that is not a path at all, which is how this was caught.
    if (!row.to.includes('/')) {
      if (!row.from.endsWith('/')) fail(`line ${row.line}: ${row.from} is not a directory but its target is prose`)
      return
    }
    moves.push({ from: row.from.replace(/\/$/, ''), to: row.to.replace(/\/$/, ''), row })
  }
  for (const row of rowsB) push(row)
  for (const row of rowsA) {
    if (CONTAINERS.has(row.from)) continue
    push(row)
  }
  // Directories first (they create the parent the file moves land in), then files; inside each
  // group by target so the sequence is deterministic and a rerun is identical.
  const rank = (m) => (m.row.from.endsWith('/') ? 0 : 1)
  moves.sort((a, b) => rank(a) - rank(b) || a.to.localeCompare(b.to) || a.from.localeCompare(b.from))
  const targets = new Set()
  for (const m of moves) {
    const key = m.to.toLowerCase() // Windows: two targets differing only in case are one target
    if (targets.has(key)) fail(`two rows move to ${m.to}`)
    targets.add(key)
    if (!fs.existsSync(path.join(ROOT, m.from))) fail(`line ${m.row.line}: ${m.from} is not on disk`)
  }
  return moves
}

// ---------------------------------------------------------------------------------------------
// the block model: which directories a block owns after the move, and its include roots
// ---------------------------------------------------------------------------------------------

// The root a moved directory contributes is derived from the move and from the root the spelling
// used to be resolved against. Three old roots existed: `include` (`camel/core/x.h`), `src`
// (`core/module/x.h`), and the repository root (`modules/tensor/type.h`). A move keeps its
// spellings working by contributing one new root per old root:
//
//   `include/camel/parse` -> `parse/include/camel/parse`
//     the old spelling was `camel/parse/parse.h`; the target ends with `/camel/parse`, so the new
//     root is the target minus that suffix: `parse/include`.
//   `src/core` -> `kernel/core`
//     the old spelling was `core/module/userdef.h`; the target ends with `/core`, so the new root
//     is `kernel`.
//   `src/parse` -> `parse/src`
//     the target does not end with the old suffix, so the target itself is the root, and the old
//     spelling `parse/parse.h` resolves under it.
//
// The repository root is a root of every block: a spelling that names a moved directory from the
// top (`lib/tensor/type.h`, after the `#include` rewrite) resolves there and nowhere else, so it
// cannot hide a cross-layer reference -- the scan still attributes it to the owning block.
const OLD_ROOTS = ['include', 'src']

function deriveRoots (moves, root) {
  const roots = new Map() // block -> Set(root)
  const add = (block, r) => {
    // A root that sits *inside* an `include` tree turns a project header name into a system header
    // name: with `.../kernel/include/camel/utils` on the path, the `<assert.h>` that libstdc++'s
    // `<cassert>` includes resolves to `camel/utils/assert.h`, and `assert` stops being declared --
    // which is how the drill found this. The `include` root above it serves every spelling that
    // tree answers (a spelling from inside the tree is an own-directory include), so the inner
    // roots are dropped.
    const segments = r.split('/')
    const includeAt = segments.lastIndexOf('include')
    if (includeAt >= 0 && includeAt !== segments.length - 1) return
    if (!roots.has(block)) roots.set(block, new Set())
    roots.get(block).add(r)
  }
  add('unplaced', 'src')
  add('unplaced', 'include')
  add('third_party', 'third_party')
  for (const block of ['third_party', 'kernel_utils', 'kernel_core', 'kernel_runtime', 'parse', 'compile', 'lib_module', 'unplaced']) {
    add(block, '.')
  }
  // `modules/rewrite/CMakeLists.txt` puts `${CMAKE_SOURCE_DIR}/modules` on its include path so
  // `verify/translate.h` resolves; after the move that root is `lib`, so a module block keeps it.
  add('lib_module', 'lib')
  // `tools/` does not move, so its roots are the ones it has today: each tool's own `src/` and
  // `include/` (the debugger spells `core/debug_core.h` against `tools/debugger/src`).
  add('tools', 'tools')
  for (const e of fs.readdirSync(path.join(root, 'tools'), { withFileTypes: true })) {
    if (!e.isDirectory()) continue
    for (const sub of ['src', 'include']) if (fs.existsSync(path.join(root, 'tools', e.name, sub))) add('tools', `tools/${e.name}/${sub}`)
  }
  for (const m of moves) {
    const block = blockOfTarget(m.to)
    // A single file moves like a one-file directory: what a block owns, and what answers a
    // spelling, is its parent. `include/camel/execute/pass.h` lands at
    // `kernel/include/camel/execute/pass.h`, and `camel/execute/pass.h` still resolves because the
    // new root is `kernel/include`.
    const isDir = m.row.from.endsWith('/')
    const toDir = isDir ? m.to : path.posix.dirname(m.to)
    const fromDir = isDir ? m.from : path.posix.dirname(m.from)
    // The block's own roots: the directory, and its `include/` tree when it has one. A module
    // declares `EXTRA_INCLUDES <module>/include` today (`tiles/program.h` is spelled from there),
    // so the block-local include tree is what keeps that spelling working after the move. Only a
    // directory that has one contributes it: `include/camel/parse` gains no `.../parse/include`,
    // which would be a root nothing answers for.
    add(block, toDir)
    if (isDir && fs.existsSync(path.join(root, m.from, 'include'))) add(block, `${toDir}/include`)
    for (const R of OLD_ROOTS) {
      const prefix = `${R}/`
      if (!fromDir.startsWith(prefix)) continue
      const oldSuffix = fromDir.slice(prefix.length)
      add(block, toDir.endsWith(`/${oldSuffix}`) ? toDir.slice(0, toDir.length - oldSuffix.length - 1) : toDir)
    }
  }
  return roots
}

function blockOfTarget (p) {
  if (p.startsWith('kernel/')) {
    if (p.startsWith('kernel/utils')) return 'kernel_utils'
    if (p.startsWith('kernel/runtime')) return 'kernel_runtime'
    return 'kernel_core'
  }
  if (p.startsWith('parse/')) return 'parse'
  if (p.startsWith('compile/')) return 'compile'
  if (p.startsWith('lib/std/')) return 'lib_std'
  if (p.startsWith('lib/')) return 'lib_module'
  if (p.startsWith('tools/')) return 'tools'
  if (p.startsWith('third_party/')) return 'third_party'
  return 'unplaced'
}

function blockOfPath (p) {
  if (p.startsWith('kernel/')) {
    if (p.startsWith('kernel/utils')) return 'kernel_utils'
    if (p.startsWith('kernel/runtime')) return 'kernel_runtime'
    return 'kernel_core'
  }
  if (p.startsWith('parse/')) return 'parse'
  if (p.startsWith('compile/')) return 'compile'
  if (p.startsWith('lib/std/')) return 'lib_std'
  if (p.startsWith('lib/')) return 'lib_module'
  if (p.startsWith('tools/')) return 'tools'
  if (p.startsWith('third_party/')) return 'third_party'
  return 'unplaced' // residual `src/`, residual `include/`, the repository root
}

// ---------------------------------------------------------------------------------------------
// the build files the move edits, and the two patches that are not a path prefix
// ---------------------------------------------------------------------------------------------

const REWRITE_TARGETS = [
  'CMakeLists.txt',
  'bindings/python/CMakeLists.txt',
  'scripts/diagnostics-catalog.mjs',
  'scripts/common.js',
  'scripts/collect-out.js',
]

// A build file names the tree by path, so every build file is rewritten. This is a glob and not a
// list: a list is a file somebody forgets, and the forgetting shows up as a build that quietly
// found nothing instead of as an error.
function rewriteTargetsFrom (tree) {
  const out = new Set(REWRITE_TARGETS)
  // This script is not a target. It names the old paths as data -- the block names, the external
  // heads, the comments -- and a rewrite would edit the table reader into agreeing with itself.
  const self = path.relative(tree, new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')).split(path.sep).join('/')
  const add = (dir, filter) => {
    if (!fs.existsSync(dir)) return
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      if (e.isDirectory()) {
        if (e.name === 'node_modules' || e.name === '.git') continue
        add(path.join(dir, e.name), filter)
      } else if (filter(e.name)) {
        const rel = path.relative(tree, path.join(dir, e.name)).split(path.sep).join('/')
        if (rel !== self) out.add(rel)
      }
    }
  }
  const isCmake = (n) => n === 'CMakeLists.txt' || n.endsWith('.cmake')
  for (const dir of ['modules', 'tools', 'bindings', 'lib']) add(path.join(tree, dir), isCmake)
  add(path.join(tree, 'cmake'), (n) => n.endsWith('.cmake'))
  // The build, install and SDK-sync scripts name the source tree too; a stale path here is a green
  // build that collected nothing.
  add(path.join(tree, 'scripts'), (n) => n.endsWith('.js') || n.endsWith('.mjs'))
  return [...out].sort()
}

// The eight hooks in the top-level `CMakeLists.txt` that named `libcamel_objects`, replaced by the
// generated block API. Each pair is matched literally and must match exactly once: a hook that is
// gone is a build file this script no longer understands, and guessing would move the wrong thing.
const CMAKE_HOOKS = [
  [
    'add_library(libcamel_objects OBJECT ${LIBCAMEL_SRC})\nadd_library(libcamel SHARED $<TARGET_OBJECTS:libcamel_objects>)',
    'include(${PROJECT_SOURCE_DIR}/cmake/layout-blocks.cmake)\ncamel_layout_add_blocks()\nadd_library(libcamel SHARED ${CAMEL_LAYOUT_ALL_OBJECTS})',
  ],
  [
    'target_compile_features(libcamel_objects PRIVATE cxx_std_23)\nset_target_properties(libcamel_objects PROPERTIES\n    CXX_STANDARD 23\n    CXX_STANDARD_REQUIRED ON\n    CXX_EXTENSIONS OFF\n    POSITION_INDEPENDENT_CODE ON\n)',
    'camel_layout_block_standards()',
  ],
  ['target_compile_definitions(libcamel_objects PRIVATE CAMEL_DLL_EXPORTS)', 'camel_layout_block_definitions()'],
  [
    'target_include_directories(libcamel_objects PRIVATE\n    ${PROJECT_SOURCE_DIR}/include\n    ${PROJECT_SOURCE_DIR}/src\n    ${PROJECT_SOURCE_DIR}/third_party\n    ${PROJECT_SOURCE_DIR}\n)',
    'camel_layout_block_includes()',
  ],
  [
    'target_link_libraries(libcamel_objects PRIVATE\n    camel_options\n    camel_warnings_libcamel\n    fmt::fmt\n    gtest::gtest\n    nlohmann_json::nlohmann_json\n    Taskflow::Taskflow\n)',
    'camel_layout_block_links()',
  ],
  ['camel_taskflow_with_worker_stack(libcamel_objects)', 'camel_layout_block_taskflow()'],
  ['set(_core_objects "$<TARGET_OBJECTS:libcamel_objects>")', 'set(_core_objects ${CAMEL_LAYOUT_ALL_OBJECTS})'],
  [
    'add_dependencies(libcamel_exports_scan libcamel_objects ${_camel_export_scan_targets})',
    'add_dependencies(libcamel_exports_scan camel_layout_blocks ${_camel_export_scan_targets})',
  ],
  // The two remaining references to the one object library the build used to have: its flags go to
  // every block, and its dependency is the aggregate over the blocks.
  ['add_dependencies(libcamel_objects ', 'add_dependencies(camel_layout_blocks '],
  ['target_compile_options(libcamel_objects PRIVATE', 'camel_layout_block_options('],
  // A module and a tool each name the tree's include roots themselves, and those roots no longer
  // hold the kernel's headers. The transitional catch-all is what keeps them compiling while the
  // references that need a lower layer are listed; without it the drill stops at the first module.
  // It cannot go in this call: a tool adds its own `src/` afterwards, and a catch-all placed
  // earlier would answer `state.h` with another module's header before that. So the target is
  // recorded here and the catch-all is appended to it at the end, after every other root.
  [
    'cmake/camel_module.cmake',
    '  target_include_directories(${_objects} PRIVATE\n    ${CMAKE_CURRENT_SOURCE_DIR}\n    ${CMAKE_SOURCE_DIR}/include\n    ${CMAKE_SOURCE_DIR}/third_party\n    ${CMAKE_SOURCE_DIR}\n    ${ARG_EXTRA_INCLUDES}\n  )',
    '  set(_camel_module_roots\n    ${CMAKE_CURRENT_SOURCE_DIR}\n    ${CMAKE_SOURCE_DIR}/include\n    ${CMAKE_SOURCE_DIR}/third_party\n    ${CMAKE_SOURCE_DIR}\n    ${ARG_EXTRA_INCLUDES})\n  target_include_directories(${_objects} PRIVATE ${_camel_module_roots})\n  set_property(GLOBAL APPEND PROPERTY CAMEL_LAYOUT_LATE_CATCHALL_TARGETS ${_objects})',
  ],
  [
    'tools/libcamel_tool.cmake',
    '  target_include_directories(${target} PRIVATE\n    ${CMAKE_SOURCE_DIR}/include\n    ${CMAKE_SOURCE_DIR}/src\n    ${CMAKE_SOURCE_DIR}/third_party\n    ${CMAKE_SOURCE_DIR}\n    ${CMAKE_CURRENT_SOURCE_DIR}\n  )',
    '  set(_camel_tool_roots\n    ${CMAKE_SOURCE_DIR}/include\n    ${CMAKE_SOURCE_DIR}/src\n    ${CMAKE_SOURCE_DIR}/third_party\n    ${CMAKE_SOURCE_DIR}\n    ${CMAKE_CURRENT_SOURCE_DIR})\n  target_include_directories(${target} PRIVATE ${_camel_tool_roots})\n  set_property(GLOBAL APPEND PROPERTY CAMEL_LAYOUT_LATE_CATCHALL_TARGETS ${target})',
  ],
  // Everything a tool has to say about its own roots is said by the time `tools/` is done, so the
  // catch-all is appended to the recorded targets there: last, where it cannot shadow a header a
  // target names itself.
  ['add_subdirectory(tools)', 'add_subdirectory(tools)\ncamel_layout_append_catchall()'],
]

// `BUILD_INFO_ROOTS` is what `-NoBuild` fingerprints. Left alone, every moved source falls out of
// the fingerprint and a stale install compares equal to a tree it was not built from.
const COMMON_ROOTS =
  "export const BUILD_INFO_ROOTS = ['src', 'include', 'modules', 'tools', 'stdlib', 'third_party', 'cmake']"
const COMMON_ROOTS_NEW =
  "export const BUILD_INFO_ROOTS = ['src', 'include', 'modules', 'tools', 'stdlib', 'third_party', 'cmake', 'kernel', 'parse', 'compile', 'lib']"

// `collect-out` copies `stdlib/` verbatim into `out/<tag>/stdlib`, which is the module search
// path. After the move the `.cml` files live one per `lib/<name>/`, so the install's flat
// directory has to be assembled instead of copied.
const COLLECT_STDLIB_OLD = [
  '    if (fs.existsSync(stdlibSrc)) {',
  '        fs.cpSync(stdlibSrc, stdlibDir, { recursive: true })',
  '    }',
].join('\n')
const COLLECT_STDLIB_NEW = [
  '    // layout-move: the declarations live one per lib/<name>/ now, so the install is assembled.',
  '    const libRoot = path.join(BASEDIR, \'lib\')',
  '    if (fs.existsSync(libRoot)) {',
  '        for (const entry of fs.readdirSync(libRoot, { withFileTypes: true })) {',
  '            if (!entry.isDirectory()) continue',
  '            const dir = path.join(libRoot, entry.name)',
  '            for (const f of fs.readdirSync(dir)) {',
  '                const src = path.join(dir, f)',
  '                if (fs.statSync(src).isFile() && f.endsWith(\'.cml\')) {',
  '                    fs.copyFileSync(src, path.join(stdlibDir, f))',
  '                } else if (fs.statSync(src).isDirectory() && (f === \'rulesets\' || f === \'signatures\')) {',
  '                    fs.cpSync(src, path.join(stdlibDir, f), { recursive: true })',
  '                }',
  '            }',
  '        }',
  '    }',
  '    if (fs.existsSync(stdlibSrc)) {',
  '        fs.cpSync(stdlibSrc, stdlibDir, { recursive: true })',
  '    }',
].join('\n')

// ---------------------------------------------------------------------------------------------
// generation
// ---------------------------------------------------------------------------------------------

function buildBlocks (moves) {
  const roots = deriveRoots(moves, ROOT)
  // Which directories a block compiles: every `src`-like tree that landed under it, plus what
  // stayed behind (`unplaced`).
  const treesFor = (b) => {
    // A module is not part of `libcamel`: it is a shared library of its own, built by its
    // `<name>_cmo` target, and the top-level build never had a module source in `LIBCAMEL_SRC`
    // (`src/**` and `third_party/**`, nothing else). The `lib_module` block therefore owns the
    // directories for the include scan and the ownership table, and compiles nothing: putting the
    // 88 module sources into `libcamel` again would compile every module twice and give the
    // library include paths that only one module's build ever had.
    if (b === 'lib_module') return []
    // A directory brings its whole tree; a single file brings the directory it lands in, so the
    // block that received `kernel/execute/pass.cpp` globs `kernel/execute/*.cpp` -- a split whose
    // halves are files, not directories, still compiles. Only a source file brings a tree: a moved
    // header names no translation unit and its parent has nothing to compile.
    const own = moves
      .filter((m) => blockOfTarget(m.to) === b)
      .filter((m) => m.row.from.endsWith('/') || /\.(cpp|cc|cxx|c)$/i.test(m.to))
      .map((m) => (m.row.from.endsWith('/') ? m.to : path.posix.dirname(m.to)))
    if (b === 'unplaced') own.push('src')
    if (b === 'third_party') own.push('third_party')
    return [...new Set(own)].sort()
  }
  // Every block keeps its include roots -- the catch-all is the union of all of them, and the
  // block that compiles nothing still answers a spelling in the scan. A block with no source of
  // its own gets no build target: `tools/` builds through `add_subdirectory(tools)` and the
  // modules through their own `.cmo`, and a target that globs nothing reports success for
  // compiling no files, which is the shape of green this whole exercise is trying to stop
  // trusting.
  const blocks = [...roots.keys()].sort((a, b) => layerOf(a) - layerOf(b) || a.localeCompare(b))
  return { blocks, roots, treesFor }
}

function cmakeText (blocksInfo) {
  const { blocks, roots, treesFor } = blocksInfo
  const L = []
  L.push('# GENERATED by scripts/layout-move.mjs from docs/design/architecture/layout-map.cn.md.')
  L.push('# Do not edit: edit the table and regenerate. A hand edit here is a second source of truth')
  L.push('# for the layout, and the first one is the one under review.')
  L.push('')
  // `option()` takes one description string. A description wrapped over two lines is read as extra
  // arguments, and the configure stops before it compiles anything.
  L.push('option(CAMEL_LAYOUT_TRANSITIONAL')
  L.push('    "Give every block the union of the include roots: a half-moved tree compiles, and the cross-layer references stay in the violation sheet instead of being compiled out."')
  L.push('    ON)')
  L.push('')
  L.push('set(CAMEL_LAYOUT_BLOCKS')
  for (const b of blocks) L.push(`    ${b}`)
  L.push(')')
  L.push('')
  L.push('# Strictly the lower layers: the own roots plus every block below it in the layer order.')
  for (const b of blocks) {
    L.push(`set(CAMEL_LAYOUT_BLOCK_${b}_ROOTS`)
    for (const r of [...roots.get(b)].sort()) L.push(`    \${PROJECT_SOURCE_DIR}/${r}`)
    L.push(')')
  }
  L.push('')
  L.push('set(CAMEL_LAYOUT_CATCHALL_ROOTS')
  const all = new Set()
  for (const b of blocks) for (const r of roots.get(b)) all.add(r)
  for (const r of [...all].sort()) L.push(`    \${PROJECT_SOURCE_DIR}/${r}`)
  L.push(')')
  L.push('')
  // A module's own build file still names `${CMAKE_SOURCE_DIR}/include` for the tree's headers;
  // that directory no longer holds them. This variable is what those lines name instead: the
  // catch-all while the transition is on, and nothing once every reference has been cleared.
  L.push('if(CAMEL_LAYOUT_TRANSITIONAL)')
  L.push('    set(CAMEL_LAYOUT_TRANSITIONAL_ROOTS ${CAMEL_LAYOUT_CATCHALL_ROOTS})')
  L.push('else()')
  L.push('    set(CAMEL_LAYOUT_TRANSITIONAL_ROOTS "")')
  L.push('endif()')
  L.push('')
  L.push('set(CAMEL_LAYOUT_ALL_OBJECTS "")')
  L.push('set(CAMEL_LAYOUT_BLOCK_TARGETS "")')
  L.push('')
  L.push('function(camel_layout_add_blocks)')
  L.push('    foreach(_block IN LISTS CAMEL_LAYOUT_BLOCKS)')
  L.push('        set(_sources "")')
  for (const b of blocks) {
    L.push(`        if(_block STREQUAL "${b}")`)
    L.push('            file(GLOB_RECURSE _sources')
    const trees = treesFor(b)
    // `dir/*.cpp` and not `dir/**/*.cpp`: `GLOB_RECURSE` already walks every subdirectory, and a
    // `**/` in the expression adds a level instead of allowing none -- which left every source
    // sitting directly in a block's own directory (`kernel/runtime/definitions.cpp`,
    // `parse/src/parse.cpp`, all nine of `kernel/utils/`) out of the library, and the drill found
    // it as two undefined symbols at the link of a check executable.
    for (const t of trees) L.push(`                "\${PROJECT_SOURCE_DIR}/${t}/*.cpp"`)
    L.push('            )')
    // A checkout older than the handwritten front end can still carry `parse/src/src/antlr/` on
    // disk. The build excluded that path before the move and has to exclude it after.
    L.push('            list(FILTER _sources EXCLUDE REGEX "/parse/src/src/antlr/")')
    L.push('        endif()')
  }
  L.push('        if(NOT _sources)')
  L.push('            continue()')
  L.push('        endif()')
  L.push(`        add_library(${BLOCK_PREFIX}\${_block}_objects OBJECT \${_sources})`)
  L.push('        set_property(GLOBAL APPEND PROPERTY CAMEL_LAYOUT_BLOCK_TARGETS ' + BLOCK_PREFIX + '${_block}_objects)')
  L.push('    endforeach()')
  L.push('    get_property(_targets GLOBAL PROPERTY CAMEL_LAYOUT_BLOCK_TARGETS)')
  L.push('    set(_objects "")')
  L.push('    foreach(_t IN LISTS _targets)')
  L.push('        list(APPEND _objects "$<TARGET_OBJECTS:${_t}>")')
  L.push('    endforeach()')
  L.push('    set(CAMEL_LAYOUT_ALL_OBJECTS "${_objects}" PARENT_SCOPE)')
  L.push('    add_custom_target(camel_layout_blocks DEPENDS ${_targets})')
  L.push('endfunction()')
  L.push('')
  L.push('function(camel_layout_block_standards)')
  L.push('    get_property(_targets GLOBAL PROPERTY CAMEL_LAYOUT_BLOCK_TARGETS)')
  L.push('    foreach(_t IN LISTS _targets)')
  L.push('        target_compile_features(${_t} PRIVATE cxx_std_23)')
  L.push('        set_target_properties(${_t} PROPERTIES')
  L.push('            CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)')
  L.push('    endforeach()')
  L.push('endfunction()')
  L.push('')
  L.push('function(camel_layout_block_definitions)')
  L.push('    get_property(_targets GLOBAL PROPERTY CAMEL_LAYOUT_BLOCK_TARGETS)')
  L.push('    foreach(_t IN LISTS _targets)')
  L.push('        target_compile_definitions(${_t} PRIVATE CAMEL_DLL_EXPORTS)')
  L.push('    endforeach()')
  L.push('endfunction()')
  L.push('')
  L.push('function(camel_layout_block_includes)')
  L.push('    get_property(_targets GLOBAL PROPERTY CAMEL_LAYOUT_BLOCK_TARGETS)')
  L.push('    foreach(_t IN LISTS _targets)')
  L.push('        string(REPLACE "' + BLOCK_PREFIX + '" "" _block "${_t}")')
  L.push('        string(REPLACE "_objects" "" _block "${_block}")')
  L.push('        set(_roots ${CAMEL_LAYOUT_BLOCK_${_block}_ROOTS})')
  L.push('        if(CAMEL_LAYOUT_TRANSITIONAL)')
  L.push('            list(APPEND _roots ${CAMEL_LAYOUT_CATCHALL_ROOTS})')
  L.push('        endif()')
  L.push('        list(REMOVE_DUPLICATES _roots)')
  L.push('        target_include_directories(${_t} PRIVATE ${_roots})')
  L.push('    endforeach()')
  L.push('endfunction()')
  L.push('')
  L.push('function(camel_layout_block_options)')
  L.push('    # The flags the top-level build gave its one object library, given to each block, so a')
  L.push('    # block compiles with exactly the options the library it came from did.')
  L.push('    get_property(_targets GLOBAL PROPERTY CAMEL_LAYOUT_BLOCK_TARGETS)')
  L.push('    foreach(_t IN LISTS _targets)')
  L.push('        target_compile_options(${_t} PRIVATE ${ARGN})')
  L.push('    endforeach()')
  L.push('endfunction()')
  L.push('')
  L.push('function(camel_layout_block_links)')
  L.push('    get_property(_targets GLOBAL PROPERTY CAMEL_LAYOUT_BLOCK_TARGETS)')
  L.push('    foreach(_t IN LISTS _targets)')
  L.push('        target_link_libraries(${_t} PRIVATE')
  L.push('            camel_options camel_warnings_libcamel fmt::fmt gtest::gtest')
  L.push('            nlohmann_json::nlohmann_json Taskflow::Taskflow)')
  L.push('    endforeach()')
  L.push('endfunction()')
  L.push('')
  L.push('function(camel_layout_block_taskflow)')
  L.push('    get_property(_targets GLOBAL PROPERTY CAMEL_LAYOUT_BLOCK_TARGETS)')
  L.push('    foreach(_t IN LISTS _targets)')
  L.push('        camel_taskflow_with_worker_stack(${_t})')
  L.push('    endforeach()')
  L.push('endfunction()')
  L.push('')
  // The modules and tools are built by their own object targets, and a tool adds its own `src/`
  // after `libcamel_tool.cmake` has said what it says. The catch-all is therefore appended here,
  // last of all, so that a header a target names itself is always answered by that target's own
  // directory: the transition adds roots under everything the tree already asks for.
  L.push('function(camel_layout_append_catchall)')
  L.push('    if(NOT CAMEL_LAYOUT_TRANSITIONAL)')
  L.push('        return()')
  L.push('    endif()')
  L.push('    get_property(_targets GLOBAL PROPERTY CAMEL_LAYOUT_LATE_CATCHALL_TARGETS)')
  L.push('    if(NOT _targets)')
  L.push('        return()')
  L.push('    endif()')
  L.push('    list(REMOVE_DUPLICATES _targets)')
  L.push('    foreach(_t IN LISTS _targets)')
  L.push('        if(NOT TARGET ${_t})')
  L.push('            continue()')
  L.push('        endif()')
  L.push('        target_include_directories(${_t} PRIVATE ${CAMEL_LAYOUT_CATCHALL_ROOTS})')
  L.push('    endforeach()')
  L.push('endfunction()')
  L.push('')
  return L.join('\n')
}

// ---------------------------------------------------------------------------------------------
// the include scan
// ---------------------------------------------------------------------------------------------

const HEADER_EXT = /\.(h|hpp|hh|hxx|inl|inc)$/i

function walk (dir, root, out, skip) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, e.name)
    const rel = path.relative(root, full).split(path.sep).join('/')
    if (skip.some((s) => rel === s || rel.startsWith(`${s}/`))) continue
    if (e.isDirectory()) walk(full, root, out, skip)
    else out.push(rel)
  }
}

// A spelling that names a moved directory at its head (`modules/tensor/type.h`,
// `include/camel/core/x.h`) moves with it; a spelling that resolves through an include root
// (`camel/core/x.h`, `core/module/x.h`) does not, because the root moves with the block. The
// first class is the `#include` rewrite; the second is why the count is small rather than 10,000.
function prefixRules (moves) {
  const out = []
  for (const m of moves) {
    if (!m.row.from.endsWith('/')) continue
    out.push({ from: m.from, to: m.to })
    // When the target does not end with the old suffix, no include root can absorb the move:
    // `src/compile/...` was spelled `compile/...` against the `src` root, and the target is
    // `compile/src`, so the spelling itself has to become `compile/src/...`. This is the one class
    // of `#include` the move genuinely rewrites.
    for (const R of OLD_ROOTS) {
      const prefix = `${R}/`
      if (!m.from.startsWith(prefix)) continue
      const oldSuffix = m.from.slice(prefix.length)
      if (!m.to.endsWith(`/${oldSuffix}`) && oldSuffix !== m.to) out.push({ from: oldSuffix, to: m.to })
    }
  }
  return out.sort((a, b) => b.from.length - a.from.length)
}

function rewriteSpelling (spelling, rules) {
  for (const r of rules) {
    if (spelling === r.from) return r.to
    if (spelling.startsWith(`${r.from}/`)) return r.to + spelling.slice(r.from.length)
  }
  return null
}

// Which stage-0 step removes a reference. The three inversions are named in the plan:
//
//   0.2  the pass driver (`src/execute/pass.cpp` and its two neighbours) stops naming concrete
//        passes: it keeps only the registry.
//   0.3  `core/` stops including `parse/` and `compile/`: the kernel declares a source-module
//        loader and `compile/` registers an implementation.
//   0.4  nothing in the kernel or in `modules/` includes `parse/ast/`: a pass reads a compiled
//        rule value, never the syntax tree.
//
// Everything else is a lane's work in a later stage, and `后续` says so rather than implying that
// this stage owes it.
function stage0Step (file, landed) {
  // 0.4 first, and narrowly: a syntax-tree header reached from the kernel or from a module. The
  // plan's 0.4 names `include/camel/core/forward.h#forward` as its example, and its completion condition
  // is that nothing in the kernel or in `modules/` includes `parse/ast/`. `parse/` reading its own
  // tree is not a violation and must not be listed as one.
  if (/^(kernel_|lib_module)/.test(blockOfPath(file)) && landed.startsWith('parse/') && landed.includes('/ast/')) return '0.4'
  // 0.3: any other reach from the kernel into the parser or the compiler -- the kernel calling them
  // to load a source module, which is the call 0.3 replaces with a registered loader. The kernel's
  // headers are spelled against `kernel/include` after the move and against `include` before it;
  // both spellings are read here, because the table is read before the move is applied.
  if (/^(kernel\/|kernel\/include\/camel\/(core|utils|runtime)\/|include\/camel\/(core|utils|runtime)\/)/.test(file) &&
      /^(parse\/|compile\/)/.test(landed)) return '0.3'
  // 0.2: the pass driver naming a concrete pass or a parser, instead of the registry. U2 put the
  // driver in `kernel/execute/`; the passes it must stop naming are the `lib/std/` ones, the test
  // passes in `lib/test/`, and the two front-end blocks.
  if (/^(kernel\/execute\/(pass|camel_pass|pass_declarations)\.cpp|src\/execute\/(pass|camel_pass|pass_declarations)\.cpp)$/.test(file) &&
      /^(lib\/std\/|lib\/test\/|lib\/rulesets\/|parse\/|compile\/)/.test(landed)) return '0.2'
  return null
}

function simulateIncludeScan (root, moves, blocksInfo) {
  const allRoots = new Set()
  for (const [, set] of blocksInfo.roots) for (const r of set) allRoots.add(r)
  const files = []
  walk(root, root, files, ['node_modules', '.git', '.cache', 'out', 'build', 'cmake-build-debug', 'third_party/clipp'])
  const movedPrefix = moves
    .map((m) => ({ from: m.from, to: m.to }))
    .sort((a, b) => b.from.length - a.from.length)
  const rules = prefixRules(moves)
  const postMove = (rel) => {
    for (const m of movedPrefix) {
      if (rel === m.from) return m.to
      if (rel.startsWith(`${m.from}/`)) return `${m.to}${rel.slice(m.from.length)}`
    }
    return rel
  }
  // spelling -> the post-move paths that answer it. The block is derived from the path, so the index
  // carries the path: a violation is attributed to the step that removes it by where the header
  // landed, not by the name it was spelled with.
  const bySpelling = new Map()
  const allFiles = []
  for (const rel of files) allFiles.push(postMove(rel))
  for (const p of allFiles) {
    for (const r of allRoots) {
      const prefix = r === '.' ? '' : `${r}/`
      if (prefix && !p.startsWith(prefix)) continue
      const spelling = prefix ? p.slice(prefix.length) : p
      if (!spelling) continue
      if (!bySpelling.has(spelling)) bySpelling.set(spelling, [])
      bySpelling.get(spelling).push(p)
    }
  }
  const violations = []
  const stage0 = []
  const unresolvedList = []
  const includeRewrites = []
  const headers = /^(\s*#\s*include\s*)([<"])([^>"]+)([>"])/
  let includeLines = 0
  let unresolved = 0
  const cppFiles = files.filter((f) => /\.(c|cc|cpp|cxx|h|hpp|hh|hxx|inl|inc)$/i.test(f))
  const headerExt = /\.(h|hpp|hh|hxx|inl|inc)$/i
  for (const rel of cppFiles) {
    const target = postMove(rel)
    const from = blockOfPath(target)
    const targetDir = path.posix.dirname(target)
    const text = fs.readFileSync(path.join(root, rel), 'utf8')
    for (const line of text.split('\n')) {
      const m = headers.exec(line)
      if (!m) continue
      includeLines++
      const quoted = m[2] === '"'
      const original = m[3]
      // A relative spelling (`../../compile/gir/x.h`) names a position, and the move changes that
      // position: it is recomputed between the two files' new homes instead of being carried over.
      // Left alone it points at the old depth in the new tree, and the scan counted two of those.
      let rewritten = null
      if (quoted && original.startsWith('.')) {
        const oldTargetPath = path.posix.normalize(path.posix.join(path.posix.dirname(rel), original))
        const newTargetPath = postMove(oldTargetPath)
        if (newTargetPath !== oldTargetPath) rewritten = path.posix.relative(path.posix.dirname(target), newTargetPath)
      }
      if (!rewritten) {
        // A quoted spelling that answers beside the including file is that file's own neighbour,
        // and the compiler asks that question first, so the answer wins over a head-prefix rewrite:
        // `camel/core/error/diagnostics.h` says `diagnostics/arguments.h` about the directory
        // beside it, not about the top-level `diagnostics/` that U17 moved. Reading it as a prefix
        // left five headers answering nothing after the drill, which is how this was caught.
        const sibling = quoted ? path.posix.normalize(path.posix.join(targetDir, original)) : null
        if (!sibling || !bySpelling.has(sibling)) rewritten = rewriteSpelling(original, rules)
      }
      // Only a spelling that actually changes is an edit. Two siblings that both move keep their
      // relative spelling (`../tensor/tensor.h`); counting those would report work that is not
      // there, and the first run of this scan did exactly that.
      if (rewritten === original) rewritten = null
      const spelling = rewritten ?? original
      if (rewritten) includeRewrites.push({ file: target, from: original, to: rewritten, block: from })
      // A quoted include resolves against the including file's own directory first, which is what
      // the compiler does. Without it, a bare `"module.h"` is answered by whichever `module.h`
      // happens to be indexed last, and three kernel headers were reported as reaching into a
      // module for that reason.
      let owners = null
      if (quoted) {
        const sibling = path.posix.normalize(path.posix.join(targetDir, spelling))
        if (bySpelling.has(sibling)) owners = bySpelling.get(sibling)
      }
      if (!owners) owners = bySpelling.get(spelling)
      if (!owners) {
        // A system header (`<vector>`, `<cstdint>`) names no file in this tree and is not the
        // scan's business. A spelling that *does* name a path and answers to nothing is a header
        // the move lost, and it is counted rather than dropped -- a silently skipped include is
        // the green that proves nothing.
        const looksProject = !isSystemOrExternal(spelling)
        if (looksProject) {
          unresolved++
          unresolvedList.push({ file: target, spelling, block: from })
        }
        continue
      }
      const ownDepth = layerOf(from)
      const ownerBlocks = owners.map(blockOfPath)
      const ok = ownerBlocks.some((o) => o === from || layerOf(o) <= ownDepth)
      const landed = owners.slice().sort()[0]
      if (!ok) violations.push({ file: target, spelling, from, to: blockOfPath(landed), landed })
      // A stage-0 dependency inversion is not a layer violation -- the two ends can sit in the same
      // block -- so it is checked separately, on every include, and the two lists are merged. A
      // path is attributed to the earliest step that removes it; `后续` means no step of stage 0
      // does, and the entry is a lane's work in a later stage.
      const step = stage0Step(target, landed)
      if (step) stage0.push({ file: target, spelling, from, to: blockOfPath(landed), step })
    }
  }
  return { includeLines, unresolved, unresolvedList, violations, stage0, includeRewrites, headersScanned: cppFiles.length }
}

// One list out of the two checks: a reference appears once, with the step that clears it. A
// reference that both breaks the layer order and is a stage-0 inversion keeps the stage-0 step,
// because that is the step that will remove it.
function violationRows (scan) {
  const byKey = new Map()
  const add = (v, step) => {
    const key = `${v.file}\t${v.spelling}\t${v.from}\t${v.to}`
    const prior = byKey.get(key)
    if (!prior || (prior.step === '后续' && step !== '后续')) byKey.set(key, { ...v, step })
  }
  for (const v of scan.violations) add(v, '后续')
  for (const v of scan.stage0) add(v, v.step)
  return [...byKey.values()].sort((a, b) => a.step.localeCompare(b.step, 'zh') || a.from.localeCompare(b.from) || a.file.localeCompare(b.file) || a.spelling.localeCompare(b.spelling))
}

function violationLines (scan) {
  return violationRows(scan).map((v) => `${v.step}\t${v.from}\t${v.to}\t${v.file}\t${v.spelling}`)
}

function stepCounts (scan) {
  const per = new Map()
  for (const v of violationRows(scan)) per.set(v.step, (per.get(v.step) || 0) + 1)
  return [...per.entries()].sort((a, b) => a[0].localeCompare(b[0], 'zh'))
}

function summarizeRewrites (scan) {
  const per = new Map()
  for (const r of scan.includeRewrites) per.set(r.file, (per.get(r.file) || 0) + 1)
  return [...per.entries()].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]))
}

// ---------------------------------------------------------------------------------------------
// apply
// ---------------------------------------------------------------------------------------------

function git (tree, args) {
  return execFileSync('git', ['-C', tree, ...args], { encoding: 'utf8' }).trimEnd()
}

function applyMoves (tree, moves) {
  const done = []
  for (const m of moves) {
    // `git mv` renames into an existing parent but does not create a missing intermediate
    // directory for a directory move, so the parent is made first. Without this the sequence stops
    // at `include/camel/core` on a tree where `kernel/core` has just created `kernel/`.
    fs.mkdirSync(path.join(tree, path.posix.dirname(m.to)), { recursive: true })
    git(tree, ['mv', m.from, m.to])
    done.push(`git mv ${m.from} ${m.to}`)
  }
  return done
}

// The two build files a hook rewrites in place. They name the tree's include roots themselves and
// the hook is what gives those roots their transitional stand-in, so the generic old-root rewrite
// leaves them to it.
const HOOK_OWNED_FILES = new Set(['cmake/camel_module.cmake', 'tools/libcamel_tool.cmake'])

function applyRewrites (tree, moves, scan) {
  const edits = []
  // The `#include` lines first: they are the ones the compiler would otherwise fail on.
  const byFile = new Map()
  for (const r of scan.includeRewrites) {
    if (!byFile.has(r.file)) byFile.set(r.file, new Map())
    byFile.get(r.file).set(r.from, r.to)
  }
  let includeFiles = 0
  for (const [rel, pairs] of [...byFile.entries()].sort()) {
    const file = path.join(tree, rel)
    if (!fs.existsSync(file)) fail(`${rel} should be there after the move; the include rewrite cannot find it`)
    let text = fs.readFileSync(file, 'utf8')
    for (const [from, to] of pairs) {
      text = text.replace(new RegExp(`^(\\s*#\\s*include\\s*[<"])${escapeRe(from)}([>"])`, 'gm'), `$1${to}$2`)
    }
    fs.writeFileSync(file, text)
    includeFiles++
    edits.push({ file: rel, occurrences: pairs.size })
  }
  // The build files: the same prefix rule over every occurrence of a moved directory.
  const rules = prefixRules(moves)
  for (const rel of rewriteTargetsFrom(tree)) {
    const file = path.join(tree, rel)
    if (!fs.existsSync(file)) continue
    let text = fs.readFileSync(file, 'utf8')
    const before = text
    let count = 0
    for (const r of rules) {
      // A build file names a directory either with a trailing segment (`modules/ckpt/src`) or as a
      // whole token (`add_subdirectory(modules/ckpt)`), so the boundary after it is a slash or the
      // end of the token. The boundary before it stops at a word character, a dot or a hyphen, and
      // allows a slash: `${CMAKE_SOURCE_DIR}/modules/ckpt` is an absolute path to the same
      // directory. Requiring the trailing slash is what left all 29 `add_subdirectory` calls
      // pointing at a directory that no longer existed.
      const re = new RegExp(`(?<![\\w.-])${escapeRe(r.from)}(?=[/\\s)"'\`]|$)`, 'g')
      const snapshot = text
      // A path that already resolves from the file it appears in is that file's own path, not a
      // path into the moved tree: `tools/debugger/CMakeLists.txt` names its sources `src/core/...`,
      // which is `tools/debugger/src/core/...`, and `lib/tiles/CMakeLists.txt` names its cases
      // `../../test/cases/modules/tiles/...`, which the move does not touch. This runs after the
      // moves, so a moved directory no longer resolves and the check cannot swallow a real edit.
      const keepLocal = (offset) => {
        // The whole path token, not just the part the rule matched: in
        // `../../test/cases/modules/tiles/plan_check.cpp` the rule matches `modules/tiles`, and the
        // path to test for existence is the whole of it, relative to this file.
        let a = offset
        while (a > 0 && !/[\s()"'`,;]/.test(snapshot[a - 1])) a--
        let j = offset
        while (j < snapshot.length && !/[\s()"'`,;]/.test(snapshot[j])) j++
        const token = snapshot.slice(a, j)
        const local = path.posix.normalize(path.posix.join(path.posix.dirname(rel), token))
        return fs.existsSync(path.join(tree, local))
      }
      text = text.replace(re, (match, offset) => {
        if (keepLocal(offset)) return match
        count++
        return r.to
      })
    }
    // A build file that names the tree's old include roots -- a module's own check targets do, and a
    // tool that links nothing of Camel spells `camel/utils/...` against `${PROJECT_SOURCE_DIR}/
    // include` -- names a directory that no longer holds the kernel's or the parser's headers.
    // Those tokens become the transitional stand-in: the catch-all while the transition is on,
    // nothing after. The two files a hook rewrites are left alone (their hook says the same thing
    // in place), and so is the top-level file, whose own hook owns the whole call.
    const guarded = HOOK_OWNED_FILES.has(rel) || rel === 'CMakeLists.txt'
    for (const prefix of guarded ? [] : ['${CMAKE_SOURCE_DIR}', '${PROJECT_SOURCE_DIR}']) {
      for (const oldRoot of ['include', 'src']) {
        const token = `${prefix}/${oldRoot}`
        if (!text.includes(token)) continue
        // A spelled path that still exists is a file the move did not touch, so the root it names
        // still holds it. The token on its own is the root itself (`.../include)` names the
        // directory the kernel's headers left), and a directory that is still there but no longer
        // holds them is not that root.
        text = text.replace(new RegExp(`${escapeRe(token)}(?![\\w.-])`, 'g'), (match, offset) => {
          let j = offset + match.length
          while (j < text.length && !/[\s()"'`,;]/.test(text[j])) j++
          const tail = text.slice(offset + match.length, j)
          if (tail.startsWith('/') && fs.existsSync(path.join(tree, oldRoot + tail))) return match
          count++
          return '${CAMEL_LAYOUT_TRANSITIONAL_ROOTS}'
        })
      }
    }
    // The module loop names the directory through a variable, so no per-module rule can see it.
    const genericHits = text.match(/modules\/\$\{/g)
    if (genericHits) {
      count += genericHits.length
      text = text.replace(/modules\/\$\{/g, 'lib/${')
    }
    // U16 put `camel_module.cmake` in `cmake/`, so the 29 moved modules reach it by an absolute
    // path instead of `../`.
    const anchor = 'include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)'
    if (text.includes(anchor)) {
      count += text.split(anchor).length - 1
      text = text.split(anchor).join('include(${PROJECT_SOURCE_DIR}/cmake/camel_module.cmake)')
    }
    if (text !== before) fs.writeFileSync(file, text)
    edits.push({ file: rel, occurrences: count, build: true })
  }
  return { edits, includeFiles }
}

// A spelling whose head names somebody else's include root is not this tree's header, and the
// scan must not call it lost. The list is written out rather than derived so a new external
// dependency appears here as a line somebody added, not as a count that drifted.
const EXTERNAL_HEADS = new Set([
  'nlohmann',
  'fmt',
  'gtest',
  'gmock',
  'taskflow',
  'clipp',
  'concurrentqueue',
  'oneapi',
  'leveldb',
  'z3',
  'onnx',
  'pybind11',
  'reproc',
  'spdlog',
  'Python',
  'pyplot',
  'numpy',
  // The platform's own namespaces: `sys/wait.h`, `mach-o/dyld.h`, `linux/limits.h`. They are
  // spelled with a slash and are nobody's project directory.
  'sys',
  'mach',
  'mach-o',
  'linux',
  'netinet',
  'arpa',
  'asm',
])

function isSystemOrExternal (spelling) {
  if (!spelling.includes('/')) return true // `windows.h`, `unistd.h`, `vector`: no path, not ours
  return EXTERNAL_HEADS.has(spelling.split('/')[0])
}

function escapeRe (s) {
  return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
}

function applyHooks (tree) {
  const applied = []
  // A hook is `[from, to]` against the top-level `CMakeLists.txt`, or `[file, from, to]` against a
  // named build file. Either way it must match exactly once: a hook that is gone is a build file
  // this script no longer understands, and guessing would move the wrong thing.
  for (const hook of CMAKE_HOOKS) {
    const [rel, from, to] = hook.length === 3 ? hook : ['CMakeLists.txt', hook[0], hook[1]]
    const file = path.join(tree, rel)
    const text = fs.readFileSync(file, 'utf8')
    const count = text.split(from).length - 1
    if (count !== 1) fail(`${rel}: hook matched ${count} times, expected 1:\n${from}`)
    fs.writeFileSync(file, text.replace(from, to))
    applied.push(`${rel}: ${from.split('\n')[0].trim()}`)
  }
  // The two JavaScript patches.
  const common = path.join(tree, 'scripts/common.js')
  let ctext = fs.readFileSync(common, 'utf8')
  if (!ctext.includes(COMMON_ROOTS)) fail('scripts/common.js: BUILD_INFO_ROOTS is not the expected line')
  fs.writeFileSync(common, ctext.replace(COMMON_ROOTS, COMMON_ROOTS_NEW))
  applied.push('scripts/common.js: BUILD_INFO_ROOTS gains the four new roots')
  const collect = path.join(tree, 'scripts/collect-out.js')
  let gtext = fs.readFileSync(collect, 'utf8')
  if (!gtext.includes(COLLECT_STDLIB_OLD)) fail('scripts/collect-out.js: the stdlib copy is not the expected block')
  fs.writeFileSync(collect, gtext.replace(COLLECT_STDLIB_OLD, COLLECT_STDLIB_NEW))
  applied.push('scripts/collect-out.js: the install stdlib is assembled from lib/<name>/')
  return applied
}

// ---------------------------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------------------------

let ROOT = findRoot()
function findRoot () {
  // The default tree is the one this script lives in.
  return path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')), '..')
}

function fail (msg) {
  process.stderr.write(`layout-move: ${msg}\n`)
  process.exit(1)
}

function main () {
  const args = process.argv.slice(2)
  if (args.includes('--help') || args.includes('-h')) {
    process.stdout.write(fs.readFileSync(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'), 'utf8').split('\n').slice(0, 40).join('\n') + '\n')
    return
  }
  const argOf = (name) => {
    const i = args.indexOf(name)
    return i >= 0 ? args[i + 1] : ''
  }
  const apply = args.includes('--apply')
  const outDir = argOf('--out')
  if (apply) {
    const tree = argOf('--root')
    if (!tree) fail('--apply needs --root <tree>')
    ROOT = path.resolve(tree)
    if (!fs.existsSync(path.join(ROOT, '.git'))) fail(`${ROOT} is not a git worktree`)
    const branch = git(ROOT, ['rev-parse', '--abbrev-ref', 'HEAD'])
    if (['develop', 'main', 'master', 'HEAD'].includes(branch)) {
      fail(`refusing to move a tree on branch ${branch}: the drill runs on a throwaway branch`)
    }
    if (git(ROOT, ['status', '--porcelain'])) fail(`${ROOT} is dirty; the drill starts from a clean tree`)
  }

  const tables = readTable(ROOT)
  const rowsA = parseRows(pickTable(tables, '表 A'))
  const rowsBraw = parseRows(pickTable(tables, '表 B'))
  const rowsC = checkDecisions(pickTable(tables, '表 C'))
  const rowsB = rowsBraw.flatMap((r) => expandRow(r, ROOT))

  const coverage = checkCoverage(rowsA, ROOT)
  const expansion = checkExpansion(rowsB, ROOT)
  const moves = planMoves(rowsA, rowsB)
  const blocksInfo = buildBlocks(moves)

  process.stdout.write('layout-move: the table\n')
  for (const line of coverage) process.stdout.write(`  ${line}\n`)
  for (const line of expansion) process.stdout.write(`  ${line}\n`)
  process.stdout.write(`  table C: ${rowsC.length}/${rowsC.length} decided\n`)
  process.stdout.write(`  moves: ${moves.length} (${moves.filter((m) => m.row.from.endsWith('/')).length} directories)\n`)
  process.stdout.write(`  blocks: ${blocksInfo.blocks.length} (${blocksInfo.blocks.join(', ')})\n`)

  const scan = simulateIncludeScan(ROOT, moves, blocksInfo)
  const perFile = summarizeRewrites(scan)
  process.stdout.write('layout-move: the scan\n')
  process.stdout.write(`  translation units scanned: ${scan.headersScanned}\n`)
  process.stdout.write(`  #include lines: ${scan.includeLines}\n`)
  process.stdout.write(`  include strings to rewrite: ${scan.includeRewrites.length} in ${perFile.length} file(s)\n`)
  process.stdout.write(`  cross-layer references: ${scan.violations.length}\n`)
  process.stdout.write(`  the same list, by the step that clears it:\n`)
  for (const [step, n] of stepCounts(scan)) process.stdout.write(`    ${step}: ${n}\n`)
  process.stdout.write(`  unresolvable project headers: ${scan.unresolved}\n`)

  if (apply) {
    const done = applyMoves(ROOT, moves)
    const rewrites = applyRewrites(ROOT, moves, scan)
    const hooks = applyHooks(ROOT)
    fs.mkdirSync(path.join(ROOT, 'cmake'), { recursive: true })
    fs.writeFileSync(path.join(ROOT, GENERATED), cmakeText(blocksInfo))
    process.stdout.write('layout-move: applied\n')
    process.stdout.write(`  git mv: ${done.length}\n`)
    process.stdout.write(`  include files rewritten: ${rewrites.includeFiles}\n`)
    for (const e of rewrites.edits) if (e.occurrences) process.stdout.write(`  rewrote ${e.occurrences} in ${e.file}\n`)
    process.stdout.write(`  build hooks: ${hooks.length}\n`)
    process.stdout.write(`  wrote ${GENERATED}\n`)
  }

  if (outDir) {
    const dir = path.resolve(outDir)
    fs.mkdirSync(dir, { recursive: true })
    fs.writeFileSync(path.join(dir, 'git-mv.txt'), moves.map((m) => `git mv ${m.from} ${m.to}`).join('\n') + '\n')
    fs.writeFileSync(
      path.join(dir, 'coverage.txt'),
      [
        ...coverage,
        ...expansion,
        `table C: ${rowsC.length}/${rowsC.length} decided`,
        `moves: ${moves.length}`,
        `include rewrites: ${scan.includeRewrites.length}`,
        `cross-layer references: ${scan.violations.length}`,
      ].join('\n') + '\n'
    )
    const vlines = violationLines(scan)
    fs.writeFileSync(path.join(dir, 'violations.tsv'), vlines.join('\n') + (vlines.length ? '\n' : ''))
    // The same list in markdown, so the violation sheet is the script's output pasted into the
    // document instead of a table somebody retypes and drifts from.
    const vmd = [
      '| 归属 | 从 | 到 | 文件 | 拼写 |',
      '|---|---|---|---|---|',
      ...violationRows(scan).map((v) => `| ${v.step} | ${v.from} | ${v.to} | \`${v.file}\` | \`${v.spelling}\` |`),
    ]
    fs.writeFileSync(path.join(dir, 'violations.md'), vmd.join('\n') + '\n')
    const rlines = scan.includeRewrites
      .map((r) => `${r.file}\t${r.from}\t${r.to}`)
      .sort()
    fs.writeFileSync(path.join(dir, 'include-rewrites.tsv'), rlines.join('\n') + (rlines.length ? '\n' : ''))
    const ulines = scan.unresolvedList.map((u) => `${u.block}\t${u.file}\t${u.spelling}`).sort()
    fs.writeFileSync(path.join(dir, 'unresolved.tsv'), ulines.join('\n') + (ulines.length ? '\n' : ''))
    fs.writeFileSync(path.join(dir, 'layout-blocks.cmake'), cmakeText(blocksInfo))
    process.stdout.write(
      `layout-move: wrote git-mv.txt, coverage.txt, violations.tsv, include-rewrites.tsv, unresolved.tsv, layout-blocks.cmake into ${dir}\n`
    )
  }
}

main()

