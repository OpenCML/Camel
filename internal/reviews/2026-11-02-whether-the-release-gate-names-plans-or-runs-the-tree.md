# 发布闸门是按名单点名，还是跑整棵树？

**问句**：发布闸门（发版前那一遍）应该继续按 `FAST_TARGETS + SLOW_PLANS` 点名，还是与开发闸门用同一条命令（`-Plan all -Tier fast`）？

## 读数（2026-11-02 08:40 前后，在 `develop` 上量的）

- 全树 **338** 个计划文件（smoke 1 / feat 309 / gpu 6 / perf 21 / debug 1）。
- `-Plan all -Tier fast` 走到 **312** 个，读到 **8429** 条 = 8417 过 + 10 skip + 2 known failure（当时；现在是 3）。
- 走不到的 26 个 = `SLOW_PLANS` 6 + `FORBIDDEN` 18 + 只能靠自身 tier 的 2（`perf/gc_tools` = milestone、`debug/view_escape` = debug）。
- **`FORBIDDEN` 的 18 个文件里有 464 条 fast 用例**（`jit_framed` 57 条里 2 条非 fast、`onnx` 157 里 4 条、`autodiff` 263 里 5 条、`mm/gc` 45 里 43 条）。

**⇒ 那 464 条每轮开发闸门都在跑（`8429/8417/0` 里含 `jit_framed` 51 条），但发布闸门从来不跑它们**：发布闸门按名单点名，`FORBIDDEN` 的文件不进任何名单。`FORBIDDEN` 的理由写的是"its 2 milestone tests are GC stress"——**为了 2 条跑不动的条目，把同一文件里另外 55 条也划走了。**

## 两条路

**A（默认建议）**：发布闸门改成与开发闸门同一条命令（`all -Tier fast` + `SLOW_PLANS`），`FORBIDDEN` 只保留给"这条命令本身跑不起来"的情形。
- 代价：发布闸门多跑 464 条 fast 用例（`jit_framed` 51 条约 40 秒，其余待量）；`FORBIDDEN` 的含义要重写。
- 好处：**发布闸门跑的东西 = 我们每轮声称绿的东西**，两者不再有两套定义。

**B**：继续按名单点名，但把 `FORBIDDEN` 从"文件"改成"条目"。
- 代价：**runner 今天不能按名字选测试**（`scripts/plan-inventory.mjs#plan` 的注释自己说做不到），要先加这个开关；并且要长期维护一份条目名单，名单会腐坏。
- 好处：发布闸门仍然只跑"打算跑的"，耗时可控。

**默认建议：A**，本轮按 A 开工（先量发布闸门改前/改后的目标数与 `[SUMMARY]`，若改后出现红，那就是被 `FORBIDDEN` 藏起来的失败，不许改回去）。

## 一条相关的事实

`jit_framed` 那两条 `gc_stress` 长期被记作"既有红"，实测是计划里写错了参数（`--gc-check verify=1`，CLI 收 `before|after|both`），**从写下的那天起一次都没跑起来过**。改成 `verify=both` 后 **57/57/0**。
⇒ **一条测试如果从来没跑起来过，它的"红"和"绿"都不是读数。**
## 结果：按 A 落地（2026-11-02 11:00 前后，`ea5d5de70`，原 `lane-a3`，该分支已删；`381e55b45`）

**新的快层定义**：`FAST_TARGETS = ['test/plans']`——整个计划树配 `--tier fast`，与 `npm run test` 在同一条命令上。`FORBIDDEN` 改名为 `CANNOT_START`（空表），只留给"这条命令本身起不动"的计划；里程碑层不再靠表，改成**按树读**：每一条 `tier = "milestone"` 的用例，用 runner 自己的 loader 数。

**上面第 10 行那个"464 条"是错的，这一条要改过来**：`FAST_TARGETS` 里 `test/plans/feat` 是**目录**，目录是被走到的、不是被点名的，所以 `feat/` 下那些 `FORBIDDEN` 文件的 fast 用例**一直在跑**（`jit_framed` 51 条也在）。实测：

| | 计划文件 | fast 用例 |
| --- | --- | --- |
| 旧名单（smoke + feat + 两个 gpu 文件） | 308 | **8442** |
| 整棵树 | 309 | **8443** |

**差的是 1 条，而且它有名有姓**：`test/plans/gpu/tiles.plan.toml` 的 1 条 fast 用例（旧名单里两个 `gpu` 文件都不是它）。所以这一改的收益不是覆盖率，而是**报告不再说谎**：25 个文件印着"by policy 不跑"，而同一遍在跑它们的 fast 用例。

**读数**（`node scripts/release-gate.mjs --only fast`，本树）：

```
[gate] not run here: the milestone layer -- 27 plan file(s), 3330 test entries, selected by `--tier milestone`
[SUMMARY] total: 8443, pass: 8427, fail: 0, skip: 10, known failures: 6
[gate] fast layer  PASS (total 8443, pass 8427, fail 0, skip 10; 221.9 s)
[gate] GATE: all selected phases passed        (exit 0)
```

**没有出现被 `FORBIDDEN` 藏起来的红。** 但第一次量的时候出现过两个：`agree_with_fvm`、`every_fault_program_says_what_fvm_says`，同时 skip 从 10 涨到 43——原因是**闸门没在计划跑批的环境里起**（裸 ssh：没有 eval venv 的 `PATH`、没有 `LD_LIBRARY_PATH=$C/lib`、没有 `CC=clang`）。在正常环境里那个计划是 **55/55**，闸门是 `fail 0, skip 10`。⇒ **闸门量的是树，所以必须用跑树的方式起它。**

**里程碑那两个数不是一回事**：这些文件**声明 3330 条**，而 `--tier milestone` 实跑报 **`tests: 3404`**（`backends` 会展开、`compare_to` 的伴随用例会加入）。闸门印的是前者，并且写的是"test entries"，不写"cases"。