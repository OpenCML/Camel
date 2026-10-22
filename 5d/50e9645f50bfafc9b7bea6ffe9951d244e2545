# H40 — The libcamel export list is a build product

Branch: `handover/H36-generated-exports` (started from `develop`; fast-forwarded to `develop`
`58185b55` while the task ran). Everything is in the working tree; nothing was committed, branched
or pushed. Logs are under `D:\Temp`.

The explicit `.def` files are gone. Before every Windows link, `libcamel`'s exports are generated as
the intersection of

* the symbols the object files of every module and tool leave undefined, and
* the symbols libcamel's own object files define,

for the configuration being linked, plus a hand-checked manual file for the two symbols whose caller
is not an ordinary object-file import (section 4). The list is no longer a snapshot of an import
table, and adding a module-level use of a libcamel symbol no longer needs an edit.

## 1. Result at the current `develop`

| | hand file (`cmake/libcamel_exports*.def`) | generated | agreement |
|---|---|---|---|
| Release | 506 common | **501** = 499 derived + 2 manual | all 501 needed; the other 5 are Debug-only (below) |
| Debug | 506 common + 3 debug = **509** | **509** = 507 derived + 2 manual | identical, no difference in either direction |

In other words, the generated list agrees with the hand file exactly in Debug, and in Release it
agrees on every symbol any Release target imports; the five hand entries it does not reproduce are
Debug-only helpers that belong in the Debug export set, not the Release one.

The five entries that the hand file had in the common list but a Release build does not need are
`assertStoredValueIsNotView`, `clearPostAllocDebugHook`, `RegisterType` (`DebugBreakpoint`),
`runtimeDataType` (`GCGraph`) and `SetHandler` (`DebugBreakpoint`). They are defined in Release but
imported by no Release object; a Debug build imports all five and the generated Debug list contains
them. `lld-link` never sees an export it cannot resolve: the generator checks each manual entry
against the symbols libcamel defines in that configuration and fails naming the entry otherwise.

This reproduces every hand-appended batch in the configuration that needs it, including the 13 graph
symbols, the 5 pass/draft-session symbols and the 15 rule-engine/graph-fact symbols. `compileRule`,
`rewrite`, `saturate`, `DraftWritings`, `WrittenSlots`, `returnNodeRef` and the rest of the last
batch are in the Release list because the `rewrite` module's objects import them.

## 2. The cycle, and why the scan needs objects

Modules link `libcamel`; `libcamel` needs the modules' imports to know what to export. Nothing that
links `libcamel` may be a link dependency of `libcamel`, or the dependency graph is a cycle. The way
out is to split compilation from linking:

* every module is built as `<name>_cmo_objects` (OBJECT) and `<name>_cmo` (SHARED) from
  `$<TARGET_OBJECTS:...>`; the shared target links `libcamel`, the object target does not and only
  needs headers (`modules/camel_module.cmake`);
* every tool is built the same way (`tools/libcamel_tool.cmake`);
* the export scan depends on the object targets, never on the shared targets, so it can run before
  `libcamel` links without a cycle.

The objects carry the same undefined symbols the import table would, but they exist earlier and are
current after any edit, which is the difference from H30's snapshot.

## 3. How it is produced

`CMakeLists.txt` builds `libcamel_objects` (OBJECT) and `libcamel` (SHARED) from it. On Windows the
link takes `LINKER:/DEF:${PROJECT_BINARY_DIR}/libcamel_exports_$<CONFIG>.def`, and a custom command
produces that file:

```
cmake/generate_libcamel_exports.mjs
  --nm <llvm-nm> --config <Release|Debug>
  --core    build/exports/core_objects_<CFG>.txt
  --imports build/exports/import_objects_<CFG>.txt
  --manual  cmake/libcamel_exports_manual.def
  --out     build/libcamel_exports_<CFG>.def
```

The manifests are written at configure time with `file(GENERATE)` and contain one object path per
scanned object library, for the active configuration. The custom command's `OUTPUT` is the `.def`,
its `DEPENDS` are the object files, the two manifests, the manual file and the generator itself.
`libcamel` depends on a custom target that depends on the object targets, and the generated `.def` is
in `LINK_DEPENDS`, so:

* the objects compile first, then the scan runs, then `libcamel` links;
* a changed module object re-runs the scan, and a changed export set relinks `libcamel`;
* an unchanged export set rewrites no file (the generator writes only when the content changes), so
  a rebuild does not relink everything.

`llvm-nm -P --extern-only` is run in chunks of 64 objects; `__imp_` is stripped. The generator writes
`[exports] Release: 501 exported symbols (499 derived from 287 objects, 2 manual)` and the same line
for Debug, so a build log says what was exported.

The old `cmake/libcamel_exports.def`, `cmake/libcamel_exports_debug.def` and
`cmake/make_libcamel_def.cmake` are deleted.

## 4. What stays explicit

`cmake/libcamel_exports_manual.def` has two entries, each with a comment:

* **`camel::standalone::bridge::call`** — an inline `Operator::call` in
  `camel/standalone/bridge.h` calls it from a program that `std::cpp` generates. The generated
  program does not exist while the build runs, so no object imports it. The check is
  `test/plans/feat/trans/cpp.plan.toml`, which builds and links the generated programs.
* **`camel::core::rtdata::printSlotCapped`** — the frame log's capped printer (H31). Its only caller
  is the inline `Frame::get`/`FrameView::get` inside `EXEC_WHEN_DEBUG`, so a Release object names it
  only if something else does, and a Debug object names it only while that instantiation is emitted.
  The Debug scan does see it (the generated Debug list matches the old hand list exactly), but it is
  listed so the exported surface is the same in both configurations and does not hinge on an inline
  call site.

Nothing else is explicit. In particular `assertStoredValueIsNotView`, the profiler entry points and
the graph accessors are derived from the objects in the configurations that import them.

## 5. H35 — the graph module's C7-C9 entries

The 13 entries the graph-module work added by hand are in the derived list. The scan includes all
seven `graph` objects (`module`, `handle`, `nodekind`, `read`, `txn`, `facts`, `camel_module_abi`),
and `modules/graph/Release/graph.cmo` and `modules/graph/Debug/graph.cmo` both link on Windows. So
the decorated names are valid and were exercised by a link, not only written down: **H35 is closed
as a side effect for the graph module in both configurations.**

## 6. Experiments

### 6.1 Deleting a derived entry fails the build with the symbol named

The generator was temporarily made to skip `compileRule` (one of the newest hand-added symbols,
imported by `modules/rewrite/module.cpp`). The build failed at the module link:

```
[exports] Release: 500 exported symbols (498 derived from 287 objects, 2 manual)
FAILED: modules/rewrite/Release/rewrite.cmo modules/rewrite/Release/rewrite.lib
lld-link: error: undefined symbol: class std::vector<struct camel::runtime::Rule, ...>
  __cdecl camel::runtime::compileRule(class camel::core::context::Context &,
  struct camel::runtime::RuleSource const &)
>>> referenced by D:/Projects/Camel-wt/h36-exports/modules/rewrite/module.cpp
```

The skip was reverted and the next build restored 501 and linked `rewrite.cmo`.

### 6.2 Deleting the manual bridge entry fails the standalone-program check

With `bridge::call` removed from the manual file, the Release build itself succeeds (500 exports):
no in-tree object imports it. `test/plans/feat/trans/cpp.plan.toml` then fails, and the cppexec
runner names the symbol:

```
[FAIL] agree_with_fvm
C++ compile time: median 0.70 s [0.70, 0.70]
not the same:
  test/cases/modules/stdlib/fault_substr_start.cml: does not build:
  lld-link: error: undefined symbol: struct camel::standalone::bridge::Value
  __cdecl camel::standalone::bridge::call(...)
```

This is exactly why the bridge cannot be derived and why the plan is the check for it. The entry was
restored, the Release build regenerated 501, and the plan passed again (8/8, `agree_with_fvm`
38.01 s).

### 6.3 A new module-level use needs no list edit

A temporary non-static function in `modules/io/module.cpp` called
`camel::core::mm::clearPostAllocDebugHook()` — a libcamel symbol no module imported and the Release
list did not contain. The next build reported

```
[exports] Release: 502 exported symbols (500 derived from 287 objects, 2 manual)
```

and `?clearPostAllocDebugHook@mm@core@camel@@YAXXZ` appeared in the `.def`; the build was green with
no edit to any list. The probe was removed and the next build returned to 501.

## 7. Builds and tests

* Release: green end to end (first full build 603 targets, 121 s; the final incremental at
  `58185b55` took 23 s; 501 exports, 287 scanned objects).
* Debug: green end to end (first full build 611 targets, 108 s; the final incremental at
  `58185b55` took 17 s; 509 exports, 287 scanned objects). The Debug list is identical to the old
  hand-written list, including the three profiler symbols and the five Debug-only entries.
* Final incremental Release after reverting the experiments: 16 s, 501 exports.
* `node scripts/test.js test\plans\feat\trans\cpp.plan.toml test\plans\feat\modules test\plans\smoke`:
  **953 passed, 0 failed**, including `agree_with_fvm` at 38.01 s, the new `rules` library tests and
  graph-fact tests, and all module plans.
* `node scripts/test.js test\plans\smoke --build debug`: **10 passed, 0 failed** at `58185b55`.

Nothing in `src/core/mm/gen.cpp`, `src/compile/gir` storage or the control-region builders was
touched.

## 8. Costs and open questions

1. **Partial builds pay for the scan.** `libcamel` depends on every scanned object target, so a
   build that links `libcamel` compiles all module and tool objects first even when only one tool is
   wanted. That is the price of not having an import snapshot. A build-everything flag already
   dominates routine builds here; a target-scoped variant could scan only the objects of the
   targets being linked, at the cost of a more complicated dependency graph.
2. **External consumers.** The derived list exports what the in-tree targets import. A plugin or
   out-of-tree program that uses a libcamel symbol no in-tree object imports needs an entry in the
   manual file. The five entries dropped from Release are the first candidates if such a consumer
   appears. The generator checks that every manual entry is defined, not that it is needed.
3. **Non-Windows.** The derivation is Windows-only (it exists for `/DEF`). The object-library split
   is unconditional, so ELF/Mach-O builds still compile through the new targets but do not generate
   an export list. The tools used are `node` and `llvm-nm`; configure fails with a message if either
   is missing on Windows.
4. **`printSlotCapped`.** It is in the manual file by the task's instruction. The scan sees it in
   Debug, so the Debug list would be identical without it; the entry makes Release and Debug export
   the same surface, and keeps the symbol working if a generated or debug consumer appears. If a
   future change makes the frame log call it from a real Release object, it will be derived anyway
   (deduplicated with the manual entry).
5. **The Release list is five entries shorter than the old common list.** They are Debug-only
   helpers that no Release target imports. No test or tool needed them; the Release build and all
   checked plans are green without them.

## 9. Files changed

Deleted: `cmake/libcamel_exports.def`, `cmake/libcamel_exports_debug.def`,
`cmake/make_libcamel_def.cmake`.

Added: `cmake/generate_libcamel_exports.mjs`, `cmake/libcamel_exports_manual.def`.

Modified: `CMakeLists.txt` (libcamel objects, generated `.def`, scan target); `modules/camel_module.cmake`
(object/shared split, usage requirements for the objects); `modules/tensor`, `modules/tiles`,
`modules/tree` (definitions and staged object targets); `tools/libcamel_tool.cmake` and every tool's
`CMakeLists.txt` (object/shared split, include usage requirements).