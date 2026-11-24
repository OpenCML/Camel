# 合并计划（2026-11-25 夜，演练之后的修订版）

这份文件回答一个问题：**下一次有合并位的时候，按什么顺序合、每一步的判据是什么、哪几条不能合。**
它不是推送清单 —— 推送由集成步骤做，这里是「合成与过闸」。

> 上一版（25 步、按层分三段）在演练里被实测改了四处。**改动的地方都标了 `⟵ 实测`，理由写在同一行**，
> 因为「顺序为什么是这样」比「顺序是什么」更容易失传。

## 0. 现在的状态

```
develop              d6503a197c
origin/integration   22aa42ad27   （陈旧快照：整机断网，读数建在未刷新的引用上）
演练分支             seat/merge-rehearsal
第 1 层              c1f68eca3ee1   已验：NEW=0，三条新红转 [XFAIL]，known 27→30，retention 仍 PASS
第 2/3 层+17 修复    adbf4c62a7     恢复点。12 条中的 41 条清掉（onnx 0/23、tiles 0/1 全清），
                                    剩下 48 条**整族归 C-1124-281**（Camel pass 跑完后程序多出模块图）
```

**恢复点 `adbf4c62a7` 的四件事（下一次合并就从这里接着走）：**

1. `⑯ op-overload-merge`、`㉑ gir-opertarget` 仍未并：**以 front-facts 为基 rebase 后重新合成**，不是解冲突。`㉓` 仍跳过。
2. 48 条（图比对族 488/43 + `*.program_still_runs.fvm` 的 `requires a value return node` 族）**是一个根因**，
   不要按两族拆给两位 —— 拆开会让同一个根因变成两条各修一半的账。
3. 1 条新红 `declaration.every_declaration_lists`：未知 pass 名的诊断丢掉了「有哪些可用的」那半句。
   **补回名单、用例不动。** 它转绿 + `C-1124-281` 有结论，才轮到整层读数。
4. SIGSEGV 表**保留六列**（B / 14 / 15 / 17 / 22 / 17-fix），不缩成一行：
   `host_resident_parallel` FAIL→139→139→139→139→PASS、`native_parallel.scheduler` FAIL→PASS→139→PASS→139→PASS、
   `native_parallel.linear` FAIL→139→PASS→139→139→139。三次 PASS↔SIGSEGV 跳动 = 布局相关的未初始化/悬垂，不挂第 17 步。

**第 1 层是唯一一份「零新红」的读数，它是后面所有比较的基线。** 与它比要用 `plan_id + 用例名`，
不要比 `fail` 总数：`test-refactor-2` 把约 390 例移出快层（9919 → 9529），**移层的 `removed` 不记 `fixed`**。

## 1. 第 1 层：已验，可按原序重放

```
① retention-transcript      c3f32a4244
② parity-third-column       a65d859afd
③ remote-failure-log        9904a90094
④ plan-target-truth         45a3e1875e
⑤ runner-unknown-option     bc3b7a9637
⑥ red-table-context         0e23e69e43
⑦ merge-readings-cache      11566b36e0
⑧ merge-tooling             e1257e13c2
⑨ merge-tooling-2           e579cec744
⑫ test-refactor-2           ba7ef0b4f5 → 修复 ce87d3f645（必须带上，否则三条 xfail 变红）
⑬ misc                      6ae7563435
```

- `⑫` 与它的修复是**一对**：只合 `ba7ef0b4f5` 会得到 3 条新红（`ort_loop_values_dynamic`、
  `stated_mean_axis_gradient.nvm`、`stated_mean_axis_element_gradient.nvm`），因为
  `known_fail` 的 `prints` 条件读了 runner 自己的判词（`output does not contain '…'` 引用了要找的那行）。
  ⟵ 实测
- `⑬` 里 `scripts/hooks/pre-push` 取集成侧：`D-1124-78` 保留，`lane/misc` 的 `docs_only` 单亲豁免**不采纳**。
- `⑬` 里 `merge-lane.ps1` 的第二块**只留一份**（`Resolve-SeatPath`）：车道的载入期 worktree 块硬写
  `camel-gate.local.json`、无视 `-LocalConfig`，两份都留时 `merge-lane-checkout.test.ps1` 从 50/50 掉到 5 失败。⟵ 实测

## 2. 第 2 层：按这个顺序，但 `⑰` 要挪到最后

```
⑭ front-facts               3124a4efe2   → 合并后 470b424a80
⑮ graph-gc-sweep            b399e7bf78   → fbea2d7df4（零冲突，它下自 ⑭，顺序不能换）
⑱ 3a-transforms-autodiff    1746f3dbd8   → 23e3e29a0e
⑲ route5-one-autodiff       fa3f699772   → ea85a2ff0f
⑳ route5-nn-laws            ef30244e30   → 39bb92988a
⑰ std-nop                   ef6e6e8eda   ⟵ **移到最后**（见 §3）
㉒ oper-target-honest        62d26d3af6   → 48d221306e（纯重构，嫌疑最低）
```

- `⑭` 的 stdlib 冲突取车道侧：它同时带来编译器侧的 `72db33d786` 与 stdlib 的 `for i in 0 .. n`，
  依赖在同一个 merge 里到达。
- `⑱` 的 `internal/questions/README.md`、`⑲` 的 `docs/reference/camel-ai-*.md` 是**生成物**：
  取一侧后交给生成器重写（`node scripts/check-doc-links.mjs --questions-index --write`、
  `node scripts/camel-for-ai.mjs`），不手合。
- `⑳` 的 `rulesets/jvp.cml` / `transpose.cml` 是**相邻 import 行**：取并集，两侧定律一条不丢。

## 3. `std-nop`（原 ⑰）为什么移到最后

它在自己的基上就红，不是合并造成的：

```
8 个 test/cases 脚本里有 7 个今天红着，原因是 dump 之后 fallback 把程序跑了，
程序的 exit code 与输出混进脚本读数（补 std::nop 后 linear.cml 给纯 JSON，exit 0）
约 54 处 subprocess.run/invoke，一行修不完 ⇒ 需要一次按脚本的扫
```

**已经修好的部分（都不改语义，可以合）：**

- 一处判据：`NopPass::kName` + `NopPass::executesTheProgram() → false`；driver 的参数检查与 observer
  都读它，早检查 `passListEndsWithoutExecuting` 读同一个名字常量（它在任何 pass 构造之前跑）。
- **`std::nop` 不再被要求入口参数**：`main(x)` + 末位 `std::nop` 不报「参数没有值」，
  `+std::nvm` 仍报（正反各一条自测）。onnx 的 13 条同形红因此消失。
- 扫描器只认双引号 ⇒ 单引号/裸词/逗号连写/`test/cases` 都读得到；`--audit` 用**独立**的引号正则做集合差，
  非空 exit 3。落地 42 条 plan pipeline + 2 条 onnx 脚本常量。

**还没解决、不能靠补名单修的：**

- `passListTranslatesProgram → passListEndsWithoutExecuting` 之后，Camel 写的 pass 跑完，
  程序里多出该模块的图（`cse.by_camel` 节点 43→488，`srcfile` 指向 `stdlib/opt.cml`）。
  「模块装载晚了」这个假设**被实验否掉了**（在 build 前显式装载，888/43 不变）。
  下一个假设指向 `installRuntimeProgramIfCurrent` 装进来的 produced 程序带着工具程序的图 —— 未验证。
- `json.loads` 的 `Extra data` **不是**「这条用例靠程序被执行才成立」的证据，恰恰是「程序在 dump 之后被跑了」
  的症状。⟵ 实测：一处 16 条的理由按这个写错了定性，已改为「实测红、需按脚本扫」。

## 4. 不能合：需要 rebase 后重新合成，不是解冲突

```
16 op-overload-merge     cfbc2220c3
21 gir-opertarget        d25c7fcda2
```

两条车道的编译器改动（6 个文件、`name_layers.h` 重构）与 `⑭ front-facts` 的五个提交
（`72db33d786`、`8e32566f18`、`d80fcd54d2`、`7b51098916`、`80c9c10a9d`）在回答同一批问题，
**且都不含 front-facts 的任何提交** ⇒ 取哪一侧都是替另一侧决定「哪个重载被选中」。

`op-overload-merge` 还改了用户可见诊断：`comprehension.err_not_an_array` 的期望被换成
`SemanticDiag::GenericFunctionAsValue`，HEAD 的 `ComprehensionSourceRefused` + `"has type 'int'"` 被替换。
它自己那条用例是绿的（**不是带红交回**），但仍要一次语义裁决。

## 5. 跳过（保持跳过）

```
⑩ revert-batch-merge   f6c2240088   取消 D-1124-58 ③（改用 merge-batch.ps1），待裁定
⑪ plan-set             cbd5274b30   （按令）
㉓ view-graph-identity  26a06f7a6b   17 条新红 = 已挂起的 C-1124-136
```

## 6. 闸门与推送（不要混在一起）

- **合并后必须重跑第 1 层**（`-Jobs 8 -NoGpuWatch -Plan all -Tier fast`），比 `plan_id + 用例名`。
- **一次过闸的产物不止是读数，还有「这次过闸发生过」那条记录。** 演练两次都在 `[SPLIT]` 之后卡在收尾 ssh：
  读数完整、退出码未取到、`merge-record` 的 pass 记录没落 ⇒ **那两次都不算合规过闸**，交回里要写明。⟵ 实测
- `exit=1` **分不出**「跑死了」与「跑完了且有用例失败」；分得出的是有没有 `[SUMMARY]` 那一行。⟵ 实测
- SIGSEGV 单独一张表：`host_resident_parallel` 从 `⑭` 起稳定 rc=139，`native_parallel.scheduler`/`.linear`
  在测点之间 PASS↔SIGSEGV 来回跳 ⇒ **布局相关的未初始化/悬垂**，不挂在任何一步名下。⟵ 实测