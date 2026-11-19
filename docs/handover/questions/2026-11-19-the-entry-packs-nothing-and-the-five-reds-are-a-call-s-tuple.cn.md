# 入口没有 `var` 形参可打包，五条红是一次调用的元组

**日期**：2026-11-19
**提出者**：车道 N（`lane-ssa`，基线 `83d8bc5b2`）
**状态**：待裁（**本页不填默认**，理由见 §5）
**触发**：按作者 `58e739dcf`「the entry presents its result; no withExitType on the entry; every reader of what a graph hands on calls the derivation」把呈现两处改成取派生第 0 份、`builder.cpp` 撤回零 diff 之后，六计划实测：**一个数没动**。

## 1. 这次实测推翻了作者的一句预言

作者在定四步时写下的那句是：「**`fastvm` 路径下入口仍印元组 ⇒ `compile` 的 5 条不会全部变绿，没绿的那些是 fastvm 那几条**」。实测（`83d8bc5b2`）：**5 条全红，一条都不是 fastvm 独有**，而其中四条是同一个用例的**四个后端全红**：

```
[FAIL] var.parameter_write_is_the_callers_value   expect_stdout_exact differs:
        expected "0 41\n0 42\n0 9\n0\n0 5\n", got "0 41\n0 42\n0 9\n0\n(0, 5) 5\n"
[FAIL] var_parameter.write_outside_sync.nvm       Error: unexpected output: (0, 1)
[FAIL] var_parameter.write_outside_sync.fvm       Error: unexpected output: (0, 1)
[FAIL] var_parameter.write_outside_sync.jit       Error: unexpected output: (0, 1)
[FAIL] var_parameter.write_outside_sync.prl       Error: unexpected output: (0, 1)
```

**五条同因**：体内**一个调用**被当成元组呈现 —— `println(store(x))` 印 `(0, 1)`、`println(f(d))` 印 `(0, 5)`，而两处期望的都是那个调用的**结果**。`nvm` 与 `prl` 也在里面，所以「没绿的是 fastvm」这一句与读数不符。**这不是新坏的一类**：`2026-11-19-a-sync-body-takes-the-call-s-tuple-as-the-statement-s-value.cn.md` 问的就是调用点这一半。

## 2. 两处「取第 0 份」在这些计划里一次都不触发（这是测量，不是失败）

代码形状按作者的话落好了：`program_emitter.cpp` 的 `main` 返回类型、`entry_args.cpp` 的 `writeEntryResultTo`，在出口是**打包造出来的**元组时取第 0 份；判据是「签名里有 `var` 形参，且声明的结果本身不是元组 / 不是 `T!`」。

而**入口根本没有 `var` 形参**：全树 **250 处 `func main(`，没有一处声明 `var` 形参**（`func main\([^)]*\bvar\b` 零命中，`test/cases` 里也没有一处签名跨行的例外）。由 `builder_helpers.h:102` 的 `packsVariableParameters`，没有 `var` 形参的签名**不打包**，于是：

- 入口图的出口就是它声明的那一个（`int`），`graphExitTypeOf` 对它返回同一个 `int`；
- 两处呈现读到的从来不是元组，第 0 份那条分支**一次也不进**。

⇒ 所以「入口图也打包」这个方向虽然被作者确认（`b69635919`、`58e739dcf`），**它在本仓库当前的用例里是空转的**：不是它错了，是**没有一页用例能让它成立**。六计划一个数不动，正是这条空转的读数。

## 3. 真正把元组推到用户面前的，是调用点的取值

`println(store(x))` 里 `store` 是 `(var y: int): int` 的闭包，调用交出的值是 `(0, 1)`（结果 + `y` 的终值），而用户写的是一个**表达式**，他要的是那个结果。这条路上够得着的读者是 builder 的 call value（`builder_expr.cpp#callValueTypeOf` / `callValuePartOf`）与 `println` 的实参，**它不是入口**，也不在作者那四步的名单里。

⇒ 换句话说：四步里第 2/3 步的**方向**（呈现取第 0 份）是对的，只是**位置**落在入口；而红落在调用点。

## 4. 六计划原行（`83d8bc5b2`，`killed 0` / `timeout 0`）

```
feat/compile  total: 2635, pass: 2614, fail: 5, known failures: 16
feat/linear   total: 869, pass: 751, fail: 113, skip: 2, known failures: 3
feat/opt      total: 1617, pass: 1613, fail: 4
feat/parse    total: 126, pass: 126, fail: 0
feat/para     total: 104, pass: 104, fail: 0
feat/tools    total: 70, pass: 69, fail: 1
```

与基线逐字相同（`linear` 的 `113` 没减），`opt 4` / `tools 1` 没涨，`killed` / `timeout` 都是 0。这一笔**只留在车道，没有 push**。

## 5. 要问作者的那一句（本页不填默认）

> **`func main(): int` 的入口没有 `var` 形参 ⇒ 那 `(0, 1)` 到底是谁的包？**
> ——是**调用点**把调用当成了「交回形参」（`store(x)` 那一次，结果 + `x` 的终值），
> 还是**体内的一次调用**被当元组印了出来（那是打印/取值那一侧的事）？

两条路的代价不同：前者改 builder 的 call value（一处的类型与取值），后者改呈现端（`println` 的实参取值）；两者也**都可以**说成「呈现取第 0 份」，所以要作者点名是哪一层，才知道第 0 份该写在哪儿。

**本页不填默认**：上一页的默认（「呈现取第 0 份」）已按作者的话落成代码，而它**一次都不触发**（§2）；在同一个方向上再填第二个默认，等于在一条已被证明空转的路上再走一步。