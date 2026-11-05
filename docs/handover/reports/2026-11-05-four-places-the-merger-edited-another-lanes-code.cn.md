# 四处「合并者改了别人车道的东西」：为什么必须在这里改

> 时间：2026-11-05。范围：`merge-train-2` → `develop`，从 `d2d43415b` 到 `0be3f0683` 这一段车。
> 用途：将来 `git blame` 落到这四行时，需要知道它**没在原来的车道上改**的原因——**四处都是「车道自己的读数红/绿不成立」，不是风格偏好。**

## 1. `test/plans/feat/compile/transforms.plan.toml` 的 `jvp_*` 条目

`libgrad` 与 `jvpfn` 在同一个 plan 上相撞。`--ours` 保住了基线那一份（含 `repin` 之后的 `<>` 位置 pin），**代价是把车道新加的条目丢了**，闸门 31 立刻以 `fail 2` 答出：`gate.plan_inventory` 与 `gate.case_ledger` 都说 `jvp_refuses_a_call_through_a_value.cml: no plan and no other file of the tree reads it`。**补的是「那一条」，不是「车道那份文件」**——车道那份的 pin 早于 `repin`，整份取过来会把位置 pin 改回旧写法。**之后 `jvpfn` 删掉了那个用例本身，条目也随之删掉，所以今天树里两边都没有它。**

## 2. `src/compile/gir/builder.h` 与 `src/compile/gir/builder_rest.cpp` 的 `RestBinding::owner`

**这一处不在任何车道上：它是作者 23:20 次序里的第 1 件（会让编译器崩溃的那一处），由合并者直接做**，因为排在所有车道之前。改动是给 `RestBinding` 加一个「这条绑定属于哪张图」的字段，`restBindingNamed` 只在同一张图里作答。**放回车道的代价**：这个崩溃与 `lane-lifcall` 的反例、`jvpfn` 的规则在同一个类型-图交界上，等一条车道意味着这条车线上继续留着一次 `SIGABRT`。

## 3. `src/builtin/executors/builtin.cpp` 的 `extent_of` 三个布尔

`extof` 车道删掉了 `extent_of` 的 kernel，而**它的 traits 一直由 impl-map 那条通用循环顺手登记**——kernel 一没，登记也没了，闸门 39 答 `fail 6`，全部是 `operators without a purity declaration: :op/extent_of`。**修法是补一句显式的 `registry.set(":op/extent_of", …)`，不是把 kernel 加回去**：作者裁定 `extent_of` 没有 kernel 是设计，不是缺口。**放回车道的代价**：车道的测量本身是绿的（它只跑自己的 plan），这个红只在**全量闸门**上出现，车道看不见。

## 4. `test/plans/feat/trans/macro.plan.toml` 的 `export_loop.reading`

`errwrite` 车道把 56 个文件里的 91 处 `let _ =` 丢弃改成了真正的检查。`export_loop.reading` 因此变红，**但红的原因不是车道改错了，而是那个条目原本就是假绿**：它从不曾设 `ONNX_OUT`，写失败被 `let _ =` 吞掉，于是「导出成功」这件事从来没被测过。**修在条目上**：给它自己的 `ONNX_OUT`，并把打印出来的那一行数也 pin 住（`[[0.0625, 0.125], [0.1875, 0.25]]`）。**放回车道的代价**：车道按规矩不碰别人的 plan 条目，而这里要改的正是别人的条目。

## 共同的形状

**四处都是「车道自己的闸门绿、全量闸门红」或「车道的测量对象不是它以为自己量的那件事」。** 前者要求合并者改动车道之外的条目（第 1、3、4 处），后者要求合并者在车道上直接修（第 2 处）。**这四行不是例外清单，是判据：只要一个红只在全量闸门上出现，它就不可能由车道的自测发现。**