# H5b — an operator declared with its type is found from elsewhere (design principles 5.7)

Worktree `D:\Projects\Camel-wt\h5b-operators`, branch `handover/H5b-operator-module` (from
`develop`, base `6a7231b2`). Everything is left in the working tree: no commit, no new branch, no
push. Builds used only `camel-build.ps1 -Path <worktree>`; test plans used only
`camel-test.ps1 -Path <worktree> -Plan <one plan> -Jobs 8`. Logs and scratch programs are under
`D:\Temp\h5b*`; nothing was written into the tree except the cases, the plan and this report.

## 1. The rule, as 5.7 states it

`docs/design-principles.cn.md` 5.7, first bullet:

> 名字一律在定义处按词法查找；对依赖类型变量的值做运算（如叶子处的 `==`、对回调实参的重载选择）时，另在该值的类型声明所在的模块中查找重载。不看调用者导入了什么。

The rule has three parts:

1. **Names are looked up lexically where they are defined** — where the expression is written.
   For a generic instance this was already true: the instance body is built in the module that
   declares the generic function, so `operatorOverloadsOf` sees that module's own overloads and
   the overloads its imports bring.
2. **For an operation on a value of a dependent type variable, overloads are looked up in
   addition in the module where the value's type is declared.** This is the part that was
   missing. A module could export a type together with its operators, and a generic function in
   another module still could not use them, because the operand type's declaring module was
   never consulted.
3. **What the caller imported is not looked at.** A caller cannot make an operator visible
   inside the instance by importing the module that declares the type; only the definition site
   and the value's declaring module count.

The second bullet of 5.7 — 「要换一种运算，把函数作为参数显式传入（如 `tree_eq<my_eq>(a,
b)`）。」 — is why no other route is added: a caller who wants a different operation passes the
function explicitly.

### What "the value's type" is here

The concrete type substituted for the type variable when the instance is built:

| operand type | declaring module | searched? |
|---|---|---|
| named struct (`Box`, `Models::Box`, `Pair<int>`) | the module that made the `StructDecl` | yes, in addition to the lexical scope |
| named enum (`Color`) | the module that made the `EnumDecl` | yes |
| anonymous struct (`{ v: int }`) | none (no declaration) | no |
| builtin type (`int`, `f32`, `Tensor`) | none | no |
| alias of anything (`type Count = int`, `type Handle = Box`) | none (an alias declares no type) | no; the aliased type's own declaration is what counts (for `Box`, `models`) |
| array/tuple/optional of a named type (`Box[]`, `Box?`) | the array/tuple/optional type is declared in the core, not in `models` | no; the operand's type is not `Box` |

The module the expression is written in is skipped because its overloads are already in the
lexical scope.

### Where the extra lookup runs

Only while an instance body is being built (`instantiationDepth_ > 0`), which is exactly when a
value has a dependent type variable. Ordinary code keeps the lexical lookup it had. It is not
done at the call site (the caller's imports are irrelevant, part 3), and it is not done for a
non-generic value even when its named type comes from another module (see §8.1).

## 2. What was wrong, and what the lookup does now

Reproduction before the change (`D:\Temp\h5b`): `models` declares `Box` and `operator +` for it;
`gen` declares `twice(x: typeas T): T { return x + x }`; a client imports `{ mk } from models`
and `{ twice } from gen`, then calls `twice(mk(2))`:

```text
error[SemanticDiag::NoMatchingFunction]: ... (Box, Box); operator '+' is not defined for these operand types.
   --> D:\Temp\h5b\gen.cml:4:16
```

`Box` was created by `models` and `+` was exported by `models`, but the instance in `gen` only
saw the operators `gen` imported.

Now:

- **The declaration keeps its module.** `StructDecl` / `EnumDecl` record the module name
  (`moduleName()`, `create(name, params, module)`), set in GCT `declareStructType` /
  `declareEnumType` from `module_->name()`. It is not part of identity: interning, mangling,
  printing and `==` are unchanged; `type X = Other` still declares nothing and has no module.
- **`Builder::declaringModulesOf`** maps each operand's value type (after `valueTypeOf`) to
  `(module, type name)` for a named struct or a non-prototype enum; anonymous structs, builtins,
  aliases and other types give nothing. Duplicates collapse; the current module is skipped.
- **`Builder::addDeclaringModuleOverloads`** runs in `visitLinkNode` immediately before an
  operator DREF's overloads are chosen, only inside an instance. It merges what
  `Module::exportedOperatorOverloads(group)` returns into the DREF's set — graphs and generic
  templates, deduplicated by pointer, with `moduleOf` recording the module (used by notes). For
  `!=` the `==` overloads are used and `negatedEqualities_` is set, as `operatorDrefOf` does for
  the overloads the program declares (4.3: `==` gives `!=`).
- **`Module::exportedOperatorOverloads`** is the export half of `importedOperatorOverloads`,
  extracted so both routes produce the same set in the same order. `importedOperatorOverloads`
  now calls it for each imported module; its behavior and order are unchanged.
- **`Builder::moduleNamed`** finds the declaring module among the loaded modules
  (`Context::allUserModules()`); it never loads one.
- **`compoundAssignmentByRebinding`**'s guard also accepts the plain operator when a declaring
  module exports it, so `x += y` inside an instance follows the declaring module's `+` (4.3;
  `test/cases/consistency/operator_overloads/basic.cml`: `+=` on a program type is `x = x + y`
  through its `+`).
- **The failure message names the modules** when the extra lookup applied and nothing fits (§5).

## 3. Module loader

The task asked whether an operator declared in a module the client did not import is reachable
only if that module's library is loaded, and whether that was already true. It was already true,
and no change was needed: the type object (and its `StructDecl` / `EnumDecl`) exists only because
that module was loaded, so its library is loaded before any operand of the type can appear.
`moduleNamed` looks the module up in `Context::allUserModules()` and never imports; the searched
set is exactly the loaded modules. The tests confirm it: `models` is loaded for `{ mk }` in every
case, and the declaring-module lookup finds its operators without any import of `models` in
`gen` / `gen_imports`.

## 4. The cases and what each locks

All cases are in `test/cases/compile/module_operators/`, one directory below the test root so
that imports (`import { mk } from models`) resolve to the file of that name.

| # | case (plan test) | what it locks |
|---|---|---|
| 1 | `models.cml` + `gen.cml` + `client.cml` / `operator_in_declaring_module` | A named type declared in one module with `+`, `==` and `!=` beside it, used by a generic function in another module that imports none of it. Prints `4`, `true`, `true`. Before the change: `NoMatchingFunction` at `gen.cml` for `(Box, Box)`. |
| 2 | `gen_imports.cml` + `client_doubled.cml` / `declaring_module_operator_is_not_doubled` | The expression's module imports the declaring module too (only for the type): the same overload is found through the import and through the declaration and stays **one** candidate — no `AmbiguousCall`. Prints `6`. |
| 3 | `anon_models.cml` + `anon_gen.cml` + `anon_client.cml` / `anonymous_type_has_no_declaring_module` | An operator for `{ v: int }` has no declaring module; the caller imports the module that declares the operator, and that import must not leak into the instance. Still `NoMatchingFunction`; the message has no "searched ..." text, because nothing outside the lexical scope was searched. |
| 4 | `alias_models.cml` (`type Count = int`) + `alias_gen.cml` + `alias_client.cml` / `builtin_type_through_named_type` | A builtin reached through a named alias declares no type of its own: no declaring module is searched, `+` is the builtin one, prints `42`. |
| 5 | `two_models.cml` / `two_left.cml` / `two_right.cml` / `two_gen.cml` / `two_client.cml` / `two_offering_modules_are_not_searched` | Two modules offer `+` for `Box`, neither declares `Box`; the caller imports both. Neither is a candidate (they are not the declaring module, and the caller's imports are not looked at), so the instance has no candidate. The message names `two_gen` (where the expression is) and `two_models` (which declares `Box`), and does **not** contain `two_left` / `two_right`. This locks the "no candidate" answer the principle states for this shape. |
| 6 | `own_gen.cml` + `own_client.cml` / `module_own_operator_wins_over_callers_imports` | The expression's module has its own `+`; the caller imports two other modules offering `+` for the same type. The module's own overload is chosen (prints `300`) and the call is not ambiguous: the caller's imports are not candidates. |
| 7 | `pair_models.cml` + `pair_gen.cml` + `pair_client.cml` / `generic_operator_in_declaring_module` | The declaring module's overload is a **generic operator template** (`operator +` over `Pair<T>`): it is found through the type's declaration and instantiated for the call. Prints `2 4`. |
| 8 | `enum_models.cml` + `enum_gen.cml` + `enum_client.cml` / `enum_operator_in_declaring_module` | An enum declaration keeps its module like a struct one. Prints `true`, `true`. |
| 9 | `gen.cml` + `client_bump.cml` / `compound_assignment_follows_declaring_module_operator` | `x += y` in an instance follows the declaring module's plain `+` (4.3 with 5.7). Prints `3`. |

Case 5's "the client's own" alternative is case 6: the principle's wording puts the *declaring*
module in the search, so when the expression's module declares an overload of its own that
overload is the candidate and the two importing modules are not — the answer locked is the
module's own, not "no candidate". Case 1 locks the ordinary success, case 3 locks that a caller's
import cannot substitute for a declaring module.

## 5. Diagnostic

`NoMatchingFunction`'s first argument (`{0}`, the argument types) gets one more clause, only when
the declaring-module lookup applied (an instance, with a named operand type) and nothing fit:

```text
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (Box, Box); operator '+' is not defined for these operand types; searched the operators visible in module 'two_gen' (where the expression is) and those of module 'two_models' (which declares 'Box').
   --> .../two_gen.cml:9:16
```

The location stays the operator expression, and the existing `note: in the instance 'twice<Box>'
... instantiated by the call` still points at the caller's call. The entry module is called "this
module"; a loaded module is named by the name its source gives it (`module two_gen`), falling
back to the name it was loaded under. For two or more declaring modules the names are joined
`... those of modules 'a' (which declares 'X') and 'b' (which declares 'Y')`.

Existing messages are unchanged: the clause is added only when `declaringModulesOf` is non-empty
and the instance is being built, and no other `NoMatchingFunction` (builtin operands, `int[]`,
`bool`, tuples) gets it. The `diag_render` goldens (`sem_generic_instance.txt`,
`sem_independent.txt`) and `docs/diagnostics.md` are therefore unchanged; the docs entry only
shows the `{0}` / `{1}` templates and the chosen example is an ordinary call
(`err_subscript_key.cml`), not an operator in an instance.

## 6. Verification

| plan | result |
|---|---|
| `test/plans/feat/compile/module_operators.plan.toml` (new) | 9 / 9 pass |
| `test/plans/feat/compile` | 887 pass, 0 fail |
| `test/plans/feat/consistency` | 987 pass, 1 fail — `attempt.refuse.grad`, pre-existing (see below) |
| `test/plans/feat/modules` | 942 pass, 6 fail — all pre-existing (see below) |
| `smoke` | 10 / 10 pass |

No test was changed or weakened. The failures are pre-existing on the branch base: with the
nine source files stashed, rebuilt, and the same case/plan run, `attempt.refuse.grad` fails the
same way and the modules plan gives exactly **942 pass / 6 fail with the same six names**. None
of the failing programs uses an operator over a named type through a generic instance:

- `attempt.refuse.grad` (consistency): expects the refusal at line 18 (the `grad` call), but the
  base reports line 14 (the `attempt(...) !! 0.0` expression) and adds
  `of type 'float!' (at ...)`. The change came with `c254a2e4` ("A gradient that cannot exist is
  refused ..."), which replaced `fail(...)` with `failAt(forward_.nodeOrigin(node), ...)` in
  `modules/autodiff/pullback.cpp`; the plan line is older (`858504f9`). Pre-existing and outside
  H5b's files.
- `gradients`, `gradients_expanded` (modules): the same base commit's new taint refusal fires in
  `std::macro` while evaluating `autodiff:grad` for `softmax` (`stdlib/tensor_definitions.cml`
  line 52): "a value of type 'int' depends on a differentiated parameter but has no gradient
  channel". Not an operator lookup.
- `ort_grad_loop` (modules): the same taint refusal in the case at line 25.
- `ort_loop_values`, `ort_grad_loop_values` (modules): the ONNX exporter refuses a cast to the
  named struct `Run` ("cast of a model-dependent value to 'Run'"), an H5/exporter limitation of
  the base, unrelated to operators.
- `python.phot` (modules): the plan itself carries `failure_note = "The phot wrapper path is not
  stable in the current runtime/environment matrix."`

## 7. Changed expectations

None. No existing plan, golden, case or `docs/diagnostics.md` was edited. The two mismatches
above are reported, not papered over; changing them belongs to the commits that introduced them.

### The binary the counts come from

`out/latest` was built at **16:53** from this worktree with only the nine H5b files below
modified — that build contains H5b's change and nothing else. Every count in §6 is from that
binary (the new plan was re-run on it after that rebuild: 9/9).

At **17:00:56** another agent's uncommitted work landed in this worktree by mistake
(`modules/onnx/exporter.cpp`, `src/passes/opt/generic/generic.cpp`,
`test/plans/feat/modules/onnx.plan.toml`). Those three files are **not** part of H5b, were left
untouched, and are not in the 16:53 binary (their diff is backed up in
`D:\Temp\h5b-foreign-changes.patch`). Because the pre-existence check (§6) had rebuilt
`out/latest` from the bare base, the tree was rebuilt again at **17:08:30** so that the
handed-over binary does contain H5b. That final build necessarily also contains the three
foreign files, since they are in the tree; the new plan was re-run on it (9/9). If a binary of
H5b alone is wanted, rebuild after the foreign agent has recovered its work.

## 8. Open questions for the language author

1. **Non-generic values whose named type is reached through another module.** The sentence
   attaches the extra lookup to 「对依赖类型变量的值做运算」. A non-generic expression has no
   dependent type variable, so I did not extend it. Consequence (`D:\Temp\h5b4`): `wrap` imports
   `Box` from `models` and re-exports `type Handle = Box`; a client that imports only `wrap`
   writes `a + b` on a `Handle` and still gets `NoMatchingFunction` for `(Box, Box)`, because
   `models`' operators are not in the client's lexical scope and the client did not import
   `models`. If the rule is meant for every named type, not only for dependent type variables,
   this is the place to say so.

2. **Exported vs. non-exported overloads of the declaring module.** The sentence says 「在该值的
   类型声明所在的模块中查找重载」 and does not say "exported". The implementation searches the
   module's **exported** overloads (`Module::exportedOperatorOverloads`), consistent with 10.2's
   module boundary: a `func operator +` without `export` in `models` is not found from `gen`
   (verified in `D:\Temp\h5b5`; the diagnostic truthfully says it searched `m`). If a declaration
   module's private overloads are meant to be visible to uses of its exported type, that is a
   deliberate widening and should be stated.

3. **5.7's other example, "对回调实参的重载选择".** This change covers operator lookup only. The
   overload selection for a callback argument of a call is a different lookup (ordinary call
   arguments), and it was not touched; if that example is also meant to search the callback's
   type-declaring module, it needs its own change.

## 9. Files changed

The nine source files of H5b, and nothing else under `src/` / `include/`:

1. `include/camel/core/type/composite/struct.h` — `StructDecl::create(name, params, module = {})`,
   `moduleName()`.
2. `src/core/type/composite/struct.cpp` — the same for `StructDecl`.
3. `include/camel/core/type/enum.h` — the same for `EnumDecl`.
4. `src/core/type/enum.cpp` — the same for `EnumDecl`.
5. `src/compile/gct/builder.cpp` — pass `module_->name()` when declaring a struct or enum type.
6. `include/camel/core/module/module.h` — `exportedOperatorOverloads`.
7. `src/core/module/module.cpp` — `exportedOperatorOverloads`; `importedOperatorOverloads`
   refactored to use it (same order).
8. `src/compile/gir/builder.h` — declarations of `declaringModulesOf`,
   `addDeclaringModuleOverloads`, `hasDeclaringModuleOperator`, `moduleNamed`,
   `moduleDisplayName`, `operatorSearchScopeText`.
9. `src/compile/gir/builder_operators.cpp` — their definitions, the call in `visitLinkNode`, the
   `+=` guard, the message clause.

New files: `test/cases/compile/module_operators/` (25 `.cml` files),
`test/plans/feat/compile/module_operators.plan.toml`, this report.

Present in the worktree but **not** part of this change (another agent's, left untouched, see
§7): `modules/onnx/exporter.cpp`, `src/passes/opt/generic/generic.cpp`,
`test/plans/feat/modules/onnx.plan.toml`. The diff of the nine H5b files is also backed up at
`D:\Temp\h5b-my-changes.patch`.

## 10. What the build does at the end (for the next handover)

No commit, branch or push was made. `out/latest` is the 17:08:30 build of the nine files above
(cases, plan and report are untracked; the three foreign files described in §7 are in the tree
and therefore in that binary). The counts in §6 are from the 16:53 build, which had H5b only.
The last test run on the final binary is the new plan, 9/9. If the worktree is rebuilt after the
foreign agent has removed its work, `out/latest` becomes a binary of H5b alone.