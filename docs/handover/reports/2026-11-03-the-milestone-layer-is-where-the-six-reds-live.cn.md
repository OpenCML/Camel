# 六条 milestone 红：`fail 0` 没有撒谎，但它是"这一层没测"的 0

> 2026-11-03 00:25。`h-fold2` 报它在自己树上跑 `test/plans/feat/linear/jit_framed.plan.toml` 读到 `57 / 51 / 6`。这一页回答"是不是它弄红的"，并把结论记成一条债。

## 结论：不是它弄红的，是 milestone 层自己的既有红

在 `develop`（`bf64149aa` = `63bf99814` + 当轮已并的 `lane-m4b` 三笔，全量 `8375/8363/0`）上跑同一个计划文件，原文：

```
[FAIL] framed.operators.gc_stress.fvm                       184.36 ms
[FAIL] framed.operators.gc_stress.jit                       113.10 ms
[FAIL] framed.operators.gc_stress.inline_jit                 90.13 ms
[FAIL] framed.loops.gc_stress.fvm                            85.14 ms
[FAIL] framed.loops.gc_stress.jit                           100.34 ms
[FAIL] framed.loops.gc_stress.inline_jit                     83.35 ms
[SUMMARY] total: 57, pass: 51, fail: 6
```

**六条名字与 `h-fold2` 贴的一字不差**——同名前缀、同六个后端组合。所以 `lane-fold2` 不背这个。

## 为什么"全量 fail 0"里看不见它们

计划文件里这两条自己写着：

```toml
[[tests]]
name = "framed.operators.gc_stress"
# Slow layer: measured 14.5 s in the full serial run.
tier = "milestone"
```

**它们不是 `fast` 层、也不是 `slow` 层的测试，而是 `milestone` 层**（`test/plans` 下 `tier = "milestone"` 的测试现在 **20 条**）。我们每轮的闸门命令是 `-Plan all -Tier fast`，`fast` 层按 `tier` 跳过它们；而 `scripts/plan-inventory.mjs:121` 又把这个**计划**登记在 `FORBIDDEN` 里（理由："its 2 milestone tests are GC stress"），所以连"整份计划被哪一层跑到"这条兜底也不指向 milestone。

⇒ **`fail 0` 是真的，但它只覆盖 `fast` 层；这 6 条红从来没有被任何一层跑到。** 这正是"绿是因为没测到"。

## 债（怎么还）

1. **不要现在修这 6 条**：它们是 GC stress（`--gc-stress safepoint` + `--gc-check verify`），`agents/test.md` 把这一层定成"由协调者在一台安静的机器上跑"的 milestone 层，改它们要先有那一层的基准。
2. **要修的是"看不见"这件事**：`--tier` 的覆盖面应该有一处**能被读到的账**——谁跑 milestone、多久一次、上一次是什么时候、红的是哪几条。今天这个信息只活在某条车道的临时输出里。
3. **短期**：把这条记成具名待办（milestone 层的 6 条 `jit_framed` gc_stress 红），并在 `agents/test.md` 的 milestone 段里写明"这一层不进每轮闸门，所以它的红只能在被点名时看到"。
4. **反面教训写在这里**：**一个"绿"只有在说清它盖住哪一层之后才是绿。** 报 `[SUMMARY]` 时要带上 `-Tier`，这一条以后每轮都写。