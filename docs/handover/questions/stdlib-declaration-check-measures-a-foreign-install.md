# `stdlib-declarations.mjs --check` 量到的是别人树的二进制：两个直接跑的 node 脚本与 H149 构建戳（2026-10-25，h492 lane）

本文件只记读数与要请作者定的一条。实测基线 `origin/develop` = `6edd54e6b`，本 lane 的树
`D:\Projects\Camel-wt\h492-stdlib-decl-check`（分支 `handover/h492-stdlib-decl-check`）；构建与计划在 host
`88zjwei`，走 `D:\Projects\Camel-wt\camel-remote.ps1`。本 lane 没有改任何声明、注册表、用例或计划。

## 一、契约（逐字）

`scripts/stdlib-declarations.mjs` 只有一种模式：`main()` 里 `const mode = args[0] || '--check'`，多给一个参数或
给别的模式都打印 usage 并 `process.exit(2)`；有 problem 时 `process.exit(1)`，否则 0。**`--verify-ops` 不是它的
模式**，是 `scripts/stdlib-reference.mjs` 的模式；本脚本用 `spawnSync` 跑它，并把子进程输出的每一行前面加上
`--verify-ops: ` 当作自己的 problem 行。

谁要求它绿（两处，都不是"可选"）：

- `test/plans/feat/modules/stdlib_registry.plan.toml` 的 `registry.migrations_stay_migrated`：
  `case = "../../../../scripts/stdlib-declarations.mjs"`、`args = ["${case}", "--check"]`、`expect_exit = 0`、
  `expect_stdout_contains = ["migrated modules stay migrated"]`。同计划的 `registry.signatures_match` 直接跑
  `scripts/stdlib-reference.mjs --verify-ops`（`expect_exit = 0`）。
- `scripts/release-gate.mjs` 的 `STATIC_CHECKS` 第七项之前那一条：`{ name: 'stdlib declarations: a migrated
  module stays migrated', args: ['scripts/stdlib-declarations.mjs', '--check'] }`；`docs/npm-scripts.md` 的
  "The Release Gate" 一节把它列为七项静态检查之一。

它**不是**"必须先绿"以外的形态：这条脚本本身没有已知失败、不在 `known_fail` 里，develop 上是绿的（见四）。

## 二、两份互相矛盾的读数（逐字）

同一台机器、同一个基线，两棵树：

```
A. D:\Projects\Camel-wt\verify（游离 HEAD，6edd54e6b，工作区干净）
   node scripts/stdlib-declarations.mjs --check
   --verify-ops: stdlib/signatures/globals.cml:412: 'append' names ordinary parameter 1 'xs', and
                 :struct/append_arr registers 'arr': <value: T> (arr: T[]) => T[]
   ...
   --verify-ops: 144 difference(s) between the declarations and the registered operators
   migrated modules stay migrated: 145 problem(s) across 25 declaration module(s)     exit=1
   （该树的 out/latest/bin/camel.exe 时间戳 2026-10-21 20:46:53；out/latest 下没有 build-info.json）

B. 另一条车道自己的树（同基线 + 它的分支）
   migrated modules stay migrated: 13 problem(s) across 25 declaration module(s)      exit=1
   + 抛了一个 Node 异常

C. 本 lane 的树（无 out/，本机无构建）
   node scripts/stdlib-declarations.mjs --check
   --verify-ops: Error: camel inspect --ops failed (exit null): undefined
   --verify-ops:     at registeredOperators (.../scripts/stdlib-reference.mjs:1377:15)
   ...
   migrated modules stay migrated: 13 problem(s) across 25 declaration module(s)      exit=1
```

三份读数的 static 部分是 **0 个 problem**：A 的 145 = 144 条 `--verify-ops` 差异行 + 子进程自己的
`144 difference(s)…` 摘要行（`verifyOps()` 给子进程输出的**每一行**都加前缀，摘要行也被算成 problem）；
C 的 13 = 11 条 Node 栈帧 + 2 条只有一个 `\r` 的空行（`split('\n')` 留下孤立的 `\r`，`filter(Boolean)` 认为它是真的）。

## 三、读数从哪来（事实，非裁定）

- `stdlib-reference.mjs` 的 `camelExe()`：`process.env.CAMEL_EXE`，否则 `<REPO_ROOT>/out/latest/bin/camel`。
  `registeredOperators()` 用 `spawnSync` 跑 `camel inspect --ops`（即 `inspect` 子命令）拿注册表。
- 所以 `--verify-ops` 比的是**本树的 `stdlib/*.cml` 声明**与**`out/latest` 里那个二进制的注册表**。
  A 树的 `out/latest` 是 2026-10-21 的构建，比 develop 的声明早四天；B/C 两树没有可用的安装。
- 逐条对当前源码核过（5 条样例）：

  | A 的读数字样 | 当前源码 |
  |---|---|
  | `globals.cml:412: 'append' … registers 'arr'` | `src/builtin/modules/globals.cpp:2158` 是 `<value: T> (xs: T[]) => T[]` |
  | `globals.cml:379: 'concat' … registers 'other'` | `src/builtin/modules/globals.cpp:2136` 是 `<> (xs: T[], ys: T[]) => T[]` |
  | `globals.cml:598: 'difference' … registers 'a'/'b'` | `src/builtin/operators/collection.cpp:349` 是 `(s: Set<T>, other: Set<T>) => Set<T>` |
  | `globals.cml: no declaration of 'exit' matches the registered os:exit` | `src/builtin/modules/globals.cpp:1889` 是 `process:exit`，`os` 在 `src/`、`include/`、`modules/` 已无残留 |
  | `globals.cml:163-191 'bf16'/'f16' (:op/*tobf, :op/*tof6) is declared but no such operator is registered` | bf16/f16 标量算术是 `b8d5e57c6`（2026-10-23）落的 |
  | `autodiff.cml: 'vjp' is registered but not declared` / `str.cml: 'bytes' is registered but not declared` | `autodiff:vjp`、`str:bytes`、`str:from_bytes` 在 2026-10-21 的 `inspect --ops` 清单里，当前 `src/`、`modules/` 已无 |
  | `tiles.cml:49: 'run_preferred_resident' is declared but no such operator is registered` | `modules/tiles/src/module.cpp:585` 注册了 `tiles:run_preferred_resident` |

  即：A 的 144 条差异全部是**声明（10-25）对注册表（10-21）**，不是仓库里的两处不一致。B/C 的 13 条是
  "没有可用的 `camel` 可跑"。

## 四、正确跑法下的读数（host，同一提交的全新构建）

```
$ node scripts/stdlib-declarations.mjs --check
migrated modules stay migrated: 25 declaration module(s), 92 computed signature(s) left in C++, 0 excluded module(s), 0 problem(s)
every declaration is behind a registered operator (the declarations agree with the 615 registered operator overloads)
EXIT=0
$ node scripts/stdlib-reference.mjs --verify-ops
the declarations agree with the 615 registered operator overloads
EXIT=0
```

本 lane 的闸门（`-Plan smoke,test/plans/feat -Tier fast -Jobs 8`）改前、改后都是
`total: 6177, pass: 6158, fail: 0, skip: 11, known failures: 8`，`RUN_EXIT=0`，
`registry.migrations_stay_migrated` 与 `registry.signatures_match` 两次都 `[PASS]`。

## 五、三类可能（不裁定）

1. **真漂移**：不成立——上表每条差异的当前源码都与声明一致；全新构建下 `--verify-ops` 0 差异。
2. **脚本口径过时**（P4 别名 / `wait` 退役一类遗留）：不成立——没有一条差异指向退役的别名或 `wait`；
   `os:exit` 那条是 10-22 的 `os` → `process` 改名，改名两边都落到了当前树。
3. **脚本本身的 bug**：剩下两条计数/异常形状，本 lane 已按最小改动修掉（不改任何断言、不改退出码）：
   - `scripts/stdlib-reference.mjs` 的 `registeredOperators()`：`spawnSync` 启不动二进制时
     （`error` 有值、`status === null`、`stdout`/`stderr` 都是 `undefined`）只判 `status !== 0`，
     于是消息是 `failed (exit null): undefined`。改为先判 `run.error`，消息写明是哪个路径启不动。
     依据：同仓库 `scripts/stdlib-declarations.mjs` 对**自己**那次 `spawnSync` 就是这么判的。
   - `scripts/stdlib-declarations.mjs` 的 `verifyOps()`：空行与 `\r`、以及子进程自己的摘要行都被算成
     problem。改为按 `\r?\n` 切、`trimEnd()`、并滤掉子进程那行 `N difference(s)…`。
     A 树读数因此由 `144 difference(s)` + `145 problem(s)` 变为两处一致。

## 六、请作者定

`docs/npm-scripts.md` 的 "The Release Gate" 一节写的是：闸门"does not build: build once first, and it checks
the install it finds (`CAMEL_HOME`, else `out/latest`) against that build's stamp — **the rule that a run must
not measure another tree's binary** — refusing (exit 2) when the stamp is missing or does not match this tree's
sources"。`scripts/build-info.js` 的开头把这条记为 H149："`-NoBuild` silently reusing another revision's
`out/latest`, and a gate that measured that binary would report a tree nobody tested"。
`scripts/release-gate.mjs` 的 `checkInstallStamp()` 真的这么做：跑 `build-info.js check --tag <tag>`
（它自己的拒绝码是 4），非 0 就 `die()`（release-gate 的拒绝码是 2，与上面那句一致）。

但**直接**跑 `node scripts/stdlib-declarations.mjs --check` 或 `node scripts/stdlib-reference.mjs --verify-ops`
这两条路都**不**查构建戳，于是可以量到任意 `out/latest`（A 树就是这样）。请作者定：

1. 这两条脚本（或至少 `--verify-ops` 取注册表那一步）要不要也跑 `build-info.js check`，在安装不是本树的
   时候**拒绝**（像 `release-gate.mjs` 那样），而不是报 144 条"差异"？
2. 若要，`CAMEL_EXE` / `CAMEL_HOME` 这两个覆盖开关怎么算：它们是显式覆盖（调用者自己负责新鲜度），
   还是也应当连带被戳检查覆盖？
3. 若不要，那么"直接跑这两条脚本的读数"要不要在 `docs/npm-scripts.md` 里写清"只对同一次构建有效"，
   以免下一个人再拿旧 `out/latest` 的读数当 develop 的缺陷报？

## 七、这条不影响闸门

**这条不影响闸门（闸门 = `test/plans` 的计划）**：闸门（`camel-remote.ps1` 与 `release-gate.mjs`）在跑计划
之前已经用 `build-info.js check` 把安装锁到本树，计划里的 `registry.migrations_stay_migrated` /
`registry.signatures_match` 两次读数都是 `[PASS]`，闸门两次都是 `fail: 0`。上面第一、二节的两串数字
（144/145 与 13）只在"绕过 helper、拿别人或没有的 `out/latest` 直接跑 node 脚本"时出现。