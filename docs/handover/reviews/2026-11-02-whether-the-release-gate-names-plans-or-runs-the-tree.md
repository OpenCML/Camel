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
- 代价：**runner 今天不能按名字选测试**（`scripts/plan-inventory.mjs:75-77` 的注释自己说做不到），要先加这个开关；并且要长期维护一份条目名单，名单会腐坏。
- 好处：发布闸门仍然只跑"打算跑的"，耗时可控。

**默认建议：A**，本轮按 A 开工（先量发布闸门改前/改后的目标数与 `[SUMMARY]`，若改后出现红，那就是被 `FORBIDDEN` 藏起来的失败，不许改回去）。

## 一条相关的事实

`jit_framed` 那两条 `gc_stress` 长期被记作"既有红"，实测是计划里写错了参数（`--gc-check verify=1`，CLI 收 `before|after|both`），**从写下的那天起一次都没跑起来过**。改成 `verify=both` 后 **57/57/0**。
⇒ **一条测试如果从来没跑起来过，它的"红"和"绿"都不是读数。**