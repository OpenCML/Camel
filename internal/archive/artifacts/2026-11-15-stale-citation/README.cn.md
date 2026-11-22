# 2026-11-15 stale-citation 产物存档

## 它是怎么来的

这些文件此前只存在于 `D:\Projects\Camel-wt\varsurvey` 这棵**未跟踪**的工作树里，
其中 `stale-citation-fixed.patch` 是 **5 个文件差异的唯一载体**，三个是代码：

- `modules/transforms/transpose.cpp`
- `modules/transforms/vmap.cpp`
- `src/core/error/diagnostics/messages/semantic.cpp`

同目录下还有两份 51 MB 的整仓镜像（`varfix/`、`varfix2/`），已删除：
两份 `status` 逐字节相同（互为副本），20 个已跟踪修改里 15 个与本仓 `develop` 逐字节相同，
余 5 个全在本目录的 `stale-citation-fixed.patch` 里。

## 为什么整个搬进仓库

`D:\Temp\varsurvey-artifacts\` 里有一份相同的副本，但：

> **「在一台机器的临时目录里」和「在仓库里」是两种不同的持久。**
> 而**记下一个 sha256 不等于记下内容**——被清掉之后，我们只会知道它曾经存在。

本目录保存的是**字节本身**，不是它的指纹。

## 指纹

`stale-citation-fixed.patch` sha256 =
`F9188F4152319B56C48168150B7E15B06D86CBD90AA77FEEAE4366CF8AAF44D9`

它与 `D:\Projects\Camel-wt\varsurvey` 里那份、以及 `D:\Temp\varsurvey-artifacts\` 里那份**三处逐字节相同**（2026-11-15 22:39 实测）。

## 这个补丁携带的 19 个文件

`diagnostics/registry.tsv`、`docs/diagnostics.md`、`docs/roadmap.cn.md`、
`docs/technical/36_gc_handle_rules.md`、`docs/test-plans.md`、
`modules/transforms/split.cpp`、**`modules/transforms/transpose.cpp`**、**`modules/transforms/vmap.cpp`**、
`scripts/check-program-holders.mjs`、**`src/core/error/diagnostics/messages/semantic.cpp`**、
`src/passes/trans/cpp/operator_rules.cpp`、
`test/cases/compile/ref/ref_boxes_made_per_call_ten.cml`、
`test/cases/compile/ref/ref_boxes_made_per_call_thousand.cml`、
`test/plans/feat/compile/rulesets.plan.toml`、`state_escape.plan.toml`、`transforms.plan.toml`、
`test/plans/feat/consistency/match.plan.toml`、
`test/plans/feat/modules/onnx.plan.toml`、`selected_protection.plan.toml`

## 它没有做的事

**它没有被应用，也没有被验证。** 这是某次 `varfix` 尝试的工作副本，
未入任何分支、无 remote、HEAD 亦不在本仓。

**下一件要做的事不是「合它」，是「先判断它还对不对」。**
若 `docs.links.citations` 那条红仍在，本补丁可能正是它的修法之一；
若它已由别处修好，本目录只是考古材料。**别把一份未验证的补丁当成待办。**
