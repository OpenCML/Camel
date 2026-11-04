# 账数的是词，不是连接：`runtime.graph_draft_ledger` 的一次误红

## 一、这次红是什么

`lane-rc03` 合进 `develop` 之后，全量闸门多了一条红，名字不在既有欠账里：

```
[LEDGER] RED entered files: src/passes/opt/generic/generic.cpp
         (first at src/passes/opt/generic/generic.cpp:323: gc_node_ref_t branchPlaceInChain(const camel::runtime::GraphDraft &draft, gc_node_ref_t node) {)
[LEDGER] RED the list is test/cases/runtime/graph_draft_ledger/baseline.json: 1 entered, 0 left
```

`rc03` 只加了一个三行的辅助函数，它拿的 `draft` 是调用方已经持有的那个对象（`joinBlockFragments`
里的 `transaction.edit()`、`pruneBranch` 里的 `draft`），没有新建、没有取得第二个持有者。**它没有新增
任何直接连接，只是把类型名写进了签名。**

## 二、判据到底是什么（读的脚本，不是猜的）

`test/cases/runtime/graph_draft_ledger/check.mjs` 里的三个数全是 `git grep`：

| 账 | 命令形态 | member 是什么 |
|---|---|---|
| `files` | `git grep -w GraphDraft` | **文件路径** |
| `constructions` | `git grep 'make_unique<[^>]*GraphDraft>'` | 文件路径 |
| `decode` | `git grep 'GraphDraft::decode'` | 文件路径 |

`file#line#symbol` 里的 symbol 是**启发式**：从匹配行往上找最近的一个定义行（脚本里的 `DEFINITION`
正则），不是解析结果。**没有 AST，没有类型信息，也不问「这个函数从谁那里拿到对象」。**

所以判据是**拼写计数**：一个文件里出现过这个类型的名字，它就进账。

## 三、量出来的错在哪

同一棵树，只改签名，不改行为：

- 写类型名：`[LEDGER] files 40 member(s)` → 红（`1 entered, 0 left`）
- 改成吃 `camel::runtime::GraphTransaction &`、函数里 `transaction.graph()` 取 draft：
  `[LEDGER] files 39 member(s)` → `[LEDGER] ok: every member is on the list`

**连接一个字节没动，账从 40 掉到 39。**

## 四、这个判据的两个方向都失效

- **误红**：同一份连接，少写一个类型名就绿——本页就是。
- **误绿**：反过来，`auto &draft = transaction.graph();` 之后几百行直连也**不会**进账，因为账只数词。

这不是新发现，账页自己写过两次同一件事：`515 → 517` 那一格向上是「入口必须命名它收的类型」，
`read.cpp` 三处归零是「退路本就是 `auto`，没有类型名可删」（`docs/handover/reviews/graph-store-step2-b-ledger.cn.md`）。
**那两句已经在说：账动的是词，不是连接；词的涨落不能直接读成住户的进出。**

后果正是这次：`--record --force` 会把一个不存在的连接永久记进 `baseline.json`；不记、改签名，才是对的。

## 五、能测「连接」的是什么（只提，不决定）

四个取得点都在同一处事务接口上，数它们比数词稳：`GraphTransaction::edit`、`GraphTransaction::graph`、
`make_unique<...GraphDraft>`（账已有）、`GraphDraft::decode`（账已有）。判据会不会改，等作者定；
这一页只留读数与判据，供那一页引用。

## 六、合并侧的处置

`branchPlaceInChain` 改成吃 `GraphTransaction &`（函数内 `transaction.graph()`），`generic.cpp` 回到
`files 39`。这是本文件二十余处的既有写法（同一文件里 `joinBlockFragments`、`pruneBranch` 都是
`auto &draft = transaction.edit();`），**不是为了通过检查而删掉一个耦合**——真正的判据是「这个函数有没有
多拿一个它调用方没有的对象」，答案是没有。

为什么这一笔由合并侧做、而不是退回车道：`rc03` 的读数只有 `feat/opt` 与 `feat/compile`，**全量才抓得到
这条账**，而账是 `-Tier fast` 里的一等公民（`test/plans/feat/runtime/graph_draft_ledger.plan.toml`）。