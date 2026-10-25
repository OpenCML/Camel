# release gate 查一个安装、计划两层量另一个：`CAMEL_HOME` 只在闸门里生效（2026-10-25，h500 lane）

本文件只记事实与读数。裁定原文（H149 与 2026-10-25 的 `the compiler of a run is an argument everywhere`）要求
"量了的安装"与"查过戳的安装"是同一个；本 lane 已把闸门找到的二进制用 `--camel` 传给两条 stdlib 检查器（见报告），
但**计划那两层（fast / slow）无法被传路径**，因此仍可能不一致。未知的是：闸门自己该不该因此拒绝运行。

## 一、事实（逐处）

1. `scripts/release-gate.mjs` 的 `findCamel()`：`[process.env.CAMEL_HOME, <树>/out/latest]` 依次找
   `bin/camel`（Linux）/ `bin/camel.exe`——**`CAMEL_HOME` 优先**；找到的那个交给 `checkInstallStamp()` 查戳，
   并打印 `[gate] compiler: <path>`。
2. `test/tools/run-tests.mjs` 给**每个**计划测试强制设 `env.CAMEL_HOME = path.join(REPO_ROOT, 'out', BUILDS[build].dir)`
   （第 1439 行；`camelHome()` 第 207 行），`preflightBuild()` 也只认 `out/<build>/bin/camel`。计划文件被命名时也走这条
   路径。⇒ 计划两层量的是 `out/latest`（Release），**与环境里的 `CAMEL_HOME` 无关**；`scripts/test.js` 也不接受任何
   "指定安装"的参数（只有 `--build release|debug`，选的是 `out/debug`，而闸门是 Release 闸门）。
3. `scripts/check-doc-snippets.mjs`（闸门第 5 项静态检查）：自己读 `process.env.CAMEL_HOME || <树>/out/latest`
   （第 65、381 行），**不接受 `--camel`**，也不在内部查戳。作者 2026-10-25 裁定的返工项里已点名它要删掉
   `CAMEL_EXE`、改成 `--camel`（与本 lane 无关，未做）。
4. `scripts/stdlib-reference.mjs --verify-ops` 与 `scripts/stdlib-declarations.mjs --check`：都接受 `--camel`，
   默认 `defaultCamel()` = `out/latest`（**不看 `CAMEL_HOME`**），并在内部查戳。本 lane 已让闸门把 `findCamel()`
   的结果显式传下去（这两处不再是缺口）。`--verify-doc` 只读树、不读安装，所以没给它加参数。

## 二、两种读法与代价（未改，等裁定）

- **读法 A（闸门拒绝不一致）**：只要选中的层里有 fast 或 slow，且 `findCamel()` 找到的安装不是计划两层要用的
  `<树>/out/latest`，闸门就 exit 2，点名两个路径，并给出两条出路（`CAMEL_HOME` 指向 `out/latest`，或把某个 tag 建成
  `out/latest`）。代价：一处新增拒绝；但它会连带拒掉现在被允许的一种用法——`checkInstallStamp()` 对 `out/` 之外的
  安装本来就打印"stamp cannot be checked; the caller owns staleness"并继续，读法 A 下那种用法也会被拒。
- **读法 B（闸门量自己的默认安装）**：`findCamel()` 不再让 `CAMEL_HOME` 优先，只认 `out/<tag>`（即与计划两层同一个
  安装）；`CAMEL_HOME` 只作为"安装位置约定"保留给别的脚本。代价：改掉闸门自己的取安装规则（H149 注释里写明它认
  `CAMEL_HOME`），且与"`--camel` 显式传下去"这条规则的方向相反。
- **读法 C（保持现状 + 闸门打印两处）**：什么都不拒，只在每次运行时把"查过戳的安装"与"计划两层将测量的安装"两行
  都打印出来，让读者自己看到不一致。代价：一个"看得见但不拦"的闸门，与"a run against a stale binary measures
  nothing"的初衷相近但不相等。

## 三、本 lane 的读数（Linux 主机）

- `CAMEL_HOME=<本树>/out/latest` ⇒ 与默认同一安装，闸门与检查器一致（见报告的静态层读数）。
- `CAMEL_HOME=<别的树>/out/latest` ⇒ 闸门打印
  `[gate] build stamp: the install is outside out/, so its stamp cannot be checked; the caller owns staleness`，
  然后两条 stdlib 检查器（本 lane 已传 `--camel`）exit 2，逐字点名那个安装与它带的 stamp；
  **旧代码**（未传 `--camel`）在同样的 `CAMEL_HOME` 下会去量本树 `out/latest`——正是"查 A 量 B"。