# 生成器的状态只剩 `pc`：`halt` 位删掉的那一笔

- 日期：2026-11-18
- 车道：`lane-genhalt`；基线 `origin/develop` = `e471db459`
- 裁定：作者 `6ce3bf1f2` 的提交说明第二段，**「answer: drop the generator's halt bit」**
- 前史：`docs/handover/questions/2026-11-18-generator-halt-has-no-reader.cn.md`（问句、两条路、默认建议走的是路 B）；
  `docs/handover/reports/2026-11-16-the-state-tuple-holds-nothing-but-pc-and-halt.cn.md`（`S = (pc, halt)` 那页读数，
  **一个字节没动**：它是记录，不是关于今天的树的断言）

## 1. 结论

`Builder::visitGeneratorBody` 的状态从 `S = (pc, halt)` 收成 `S = (pc)`。删的是**一位**，不是把名字换掉：
状态类型少一处 `push_back`、零值从两项收成一项、打包与解包各少一项、`slotPart` 从 2 起改成 1 起，
外加 `halt` 的四个写点。`git grep -n 'halt' -- src/compile/gct/` 现在只剩两处**注释里的历史说明**。

## 2. 为什么删得掉：停只有一个读者，而且它不在生成器的状态里

删前 `halt` 的六个触点全在 `src/compile/gct/builder.cpp`：

| 触点 | 是什么 | 谁读它 |
|---|---|---|
| `stopHere()` | 写 1 | 无 |
| `yield` 臂 | 写 1 | 无 |
| `step` 体首 | 写 0（解包后复位） | 无 |
| `unpackSlot(haltCell, 1)` | 从状态解出一格 | 无 |
| `stateNow()` | 打包（**全树唯一的读**） | 只是把状态交给下一次拉取 |
| 分支比较 | 全树唯一一处 `equalTo(...)` | **只比较 `pc`** |

- 循环停判读的是**轮状态第 1 位**：`LOOP` 的条件是 `__eq__(:accs 1 (__gen_out__), 0)`，而 `__gen_out__` 装的是
  `roundState(answer, answered)` —— 读的是 `answered`，不是 `halt`。
- `roundState(…, intLiteral(1))` 只在两处出现（`stopHere()` 与 `yield` 臂），与 `halt` 写 1 的两处一一对应：
  `halt` 是同一个事实的第二份抄写，而它那份不判任何分支。

⇒ 所以删掉 `halt` 之后，「生成器停在哪里」仍然只有一个真相：**轮状态的 `answered` 位**。这句话删前删后都成立，
  删的是一位纯负重。

## 3. 读数

`feat/compile/stream`（生成器全部用例在这里）**每个改动点各量一次**：

```
改 1  删 stopHere 的 halt 写 1          stream 147 / 142 / 5 fail   逐名不变
改 2  删 yield 臂的 halt 写 1           stream 147 / 142 / 5 fail   逐名不变
改 3  状态 2 元组 → 1 元组              stream 147 / 142 / 5 fail   逐名不变
改 4  删 haltCell 名字 + 注释改历史      stream 147 / 142 / 5 fail   逐名不变
```

那 5 条红是 **`stream.generator{nvm,fvm,jit,prl}` + `stream.generator_default`**，红因仍是
`Invalid argument of 'state.set': the value is not a reference`（Ref 参数在步进里不再是引用，最小复现是
`state.set` 的值不是引用）。**它属于另一条线；删 `halt` 没有让它们变绿，也没有让任何一条新红出现。**

六计划 + `linear`（逐个 target 各量一次，`-Jobs 8`、`-NoGpuWatch`；七个 target 合成一次 gate 的合计是
5319 = 5269 pass / 25 fail / 2 skip / 23 known）：

```
feat/compile   2627 / 2600 pass / 10 fail / 17 known   那 10 名逐条相同，新增 0
feat/opt       1538 / 1534 /  0 fail /  4 known        （e9 在它里面）
  └ e9          212 /  208 /  0 fail /  4 known
feat/para       104 /  104 /  0 fail
feat/parse      121 /  121 /  0 fail
feat/tools       70 /   70 /  0 fail
feat/linear     859 /  840 / 15 fail / 2 skip / 2 known
```

`feat/compile` 的 10 名与基线逐条相同：`stream.generator{nvm,fvm,jit,prl}`、`stream.generator_default`、
`refwrite.shapes{nvm,fvm,jit,prl,default}`。`linear` 那条与任务书上的 839 / 16 差一条（多一条 pass、少一条 fail），
15 个红名是 `element_assigned*` 2、`path_assigned.allocs.*` 4、`var_tensor_local.*` 2、`inplace_write_then_read` 1、
`order_edges_regions.*` 6 —— 全在同一个族里，`test/cases/linear/` 里没有任何 `yield`，这条车道改不到它。

**基线之后 `origin/develop` 前进了 8 笔**（`e471db459` → `dc4761012`），其中 `3c460b37a`
（"a call that answers a stream packs the values it was handed"）正好修掉那 5 条 `stream.generator*`。
所以同一个补丁在 `dc4761012` 上又量了一次 `feat/compile`（读数树，不产分支、已清掉）：

```
feat/compile @ dc4761012 + 本笔   2627 / 2605 pass / 5 fail / 17 known
                                  5 名 = refwrite.shapes{nvm,fvm,jit,prl,default}
```

⇒ 那 5 条变绿的原因是**上游那一笔**，不是删 `halt`：修之前的老基线上，四个改动点每次量都是 5 条红、一条没动。
合并到新基线之后，这条车道应留且只留 `refwrite.shapes` 那 5 条。

## 4. 没做的事

- 11-16 那两页、`docs/technical/61_capture_census.md`、`63_what_the_348_units_are.md` 里的 `__gen_halt__`
  是那几页的读数，**不动**。
- `scripts/check-capture-census.mjs` 里的两处注释同样不动：它的 `capture_ports_generator` 会因为少了一格而下降，
  那是读数移动，不是判据移动。
- 没动 `docs/design-principles.cn.md` 5.4：它写「`S` 含恢复点编号、所有活过某个 `yield` 的局部……」，
  1 元组仍然符合；图上用元组而不是结构体是更早一笔留下的形状，不在这条车道的范围里。