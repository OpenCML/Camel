# 草稿关闭之后仍被当活着的读：一个测出来的帧，和它修好后的样子

**性质**：实测帧 + 两笔修（`draftOf` / `viewOf` / `nodesKernel` 补 `disposed()` 检查；两例钉进计划）。
**枝**：`lane/erase-constant-crash`（rebase 到 `origin/integration@1b55a42200` 之后），三笔：
`3a7d796893`（实现）、`245da2487a`（用例 + 计划）。`src/` 无遗留探针，`dirty=0`。

## 1. 最小程序（12 行，块体写法；这就是当时崩的那个）

```cml
import { Node, add_constant, erase } from graph

func inner(x: int) => int {
    return x + 1
}

func main() => int {
    let g = edit d = inner {
        let c = add_constant(d, 7)
        erase(c)
    }
    let h = g !! inner
    return h(1)
}
```

## 2. 帧（崩溃时；`e403d93000` / `6304e319e3` 的二进制上各测到一次 `rc=139`）

三处临时探针（`materialize` / `eraseKernel` 入口 / `replaceNode`）的最后输出，以及 gdb：

```
PROBE materialize slotIndex=2 dataIndex=-2 flags=2            ← draft.cpp materializeStaticValue
PROBE erase-entry index=3 txn=0x6064a0b7eec0 draftwrapper=0x7d7a1d1ff1d8 disposed=1
PROBE erase-before-draftOf
<Segmentation fault>
```

```
#0 camel::runtime::GraphDraft::alive(unsigned int) const
#1 camel::graphmod::(anonymous namespace)::eraseKernel(...)
#2.. FastVMSchedPass::invokeNativeOperator / call / apply → applyPassesDetailed → main
```

⇒ 崩在 **`src/stdlib/graph/txn.cpp:201`**（`draftOf` 里的 `draft.alive(node->index())`），因为
`node->draft()` 那个草稿包装**已被 dispose**，`transaction->graph()` 是悬垂引用。
`rewrite.cpp:421/:430` 的探针**一次都没到** ⇒ 与 `dataIndex` 负数编码、静态值槽都无关。

**A/B（同一判据，只换写法）**：`open`/`commit` + `sync main` 的同义程序 —— `disposed=0`，探针全打出并跑绿：

```
PROBE erase-entry index=3 txn=0x58768ae13910 draftwrapper=0x71bf6e5ff0f0 disposed=0
PROBE erase-after-draftOf ref=3
PROBE erase ref=3 kind=0 dataIndex=-2 flags=2 isConst=1 alive=1
PROBE replaceNode id=3 repl=4294967295 kind=0 dataIndex=-2 flags=2 isConst=1
rc=0 out=[2]
```

## 3. 同族的三条旁证（原样留档，供对 CC 主线的块体问题）

1. **`add_constant` 之后只读 `nodes(d)` 是绿的**（`rc=0`，输出 `2`）⇒ `add_constant` 本身不 dispose 草稿。
2. **`nodes(d)` 写在 `filter`/`foreach` 的闭包体里**（块体）⇒ 崩在 `nodesKernel`（gdb `#0 graphmod::nodesKernel`），不是 `erase`。
3. **块体里根本不用 `add_constant`、只擦两个图里原有的常量**（`nodes(d)->filter<is_constant>`）也 `rc=139`。
4. **`open`/`commit` 写在非 `sync` 的 `main` 里也崩**（`disposed=1`）；同写法在 `sync main` 里绿
   ⇒ 触发条件是"草稿跨帧/语句边界被 dispose 而句柄仍在用"，**块体只是最短写法，不是根因**。

## 4. 修了之后：判据不是"崩不崩"，是"这个状态一定被拒"

**口径（这一笔不许宣称修掉了那个 139）**：那 13 行程序今天不崩，不代表那行代码安全。缺陷是**一个可到达的
悬垂解引用**：`transaction->graph()` 在 `node->draft()` 已经 dispose 之后被调用。它本身就该被拒，与哪一次
二进制是否踩到坏内存无关；把同一个状态在**所有门**上都变成确定性诊断，在任何时序下都对。

### 4.1 受控 A/B（同一棵树、只退 `src/` 到 `1b55a42200` 的那几处门，各建一次）

| | 旧 `src/`（无检查，`1b55a42200`） | 新 `src/`（本枝） |
|---|---|---|
| 13 行块体程序 ×10 | **`rc=139` ×10/10** | `rc=0` ×10/10（输出 `2`）—— 见口径，**不叫"修好"** |
| `draft_gone_erase_refused.cml` | **SIGSEGV（rc=139）** | `rc=1` + `graph.erase: the draft was committed or aborted already` |
| `draft_gone_nodes_refused.cml` | **SIGSEGV（rc=139）** | `rc=1` + `graph.nodes: the draft was committed or aborted already` |
| `graph_edits.plan.toml` | `total: 9, pass: 7, fail: 2, **killed by a signal: 2**`（`expected exit 1, got signal SIGSEGV (rc=139)`，两条 `both empty: the run produced no explanation`） | `total: 9, pass: 9, fail: 0` |

**读法**：旧树上那个解引用是**可到达的**（10/10、两条用例都 139、计划把它记成"killed by a signal"）；
新树上同一批程序要么不进入该状态、要么进入后被拒 —— 无论哪种，**"草稿已关闭还被用"不再有解引用的出口**。
崩溃只出现在无检查的那一支，说明那行代码是缺陷，不是"某个二进制碰巧"。

### 4.2 新判据原文（单程序级，`std::fvm`）

```
$ camel draft_gone_erase_refused.cml std::fvm
rc=1
error[RuntimeDiag::InvalidArgument]: Invalid argument of 'graph.erase': the draft was committed or aborted already.
   --> .../draft_gone_erase_refused.cml:20:5

$ camel draft_gone_nodes_refused.cml std::fvm
rc=1
error[RuntimeDiag::InvalidArgument]: Invalid argument of 'graph.nodes': the draft was committed or aborted already.
   --> .../draft_gone_nodes_refused.cml:16:17
```

计划级（`graph_edits.plan.toml`，`--jobs 1`）：`[SUMMARY] total: 9, pass: 9, fail: 0`，两条新例
`erase_of_a_gone_draft_is_refused` / `nodes_of_a_gone_draft_is_refused` 各 `[PASS]`。

### 4.3 检查只有一份（`git grep -n requireOpenDraft`）

```
src/stdlib/graph/read.cpp:238:        requireOpenDraft(handle, "graph.nodes");
src/stdlib/graph/read.cpp:268:        requireOpenDraft(handle, "graph.params");
src/stdlib/graph/read.cpp:1052:       requireOpenDraft(handle, site);          // regionOf
src/stdlib/graph/resolve.h:98:        requireOpenDraft(handle, site);          // viewOf
src/stdlib/graph/txn.cpp:199:         requireOpenDraft(node, site);            // draftOf
src/stdlib/graph/txn.cpp:1291: void requireOpenDraft(const Handle *handle, const char *site) {
src/stdlib/graph/txn.h:31:  void requireOpenDraft(const Handle *handle, const char *site);
```

新增的 `disposed()` 判断**只在实现里出现一次**；`txn.cpp:171`（`draftResource`）与 `:1164`/`:1220`
（`commit`/`abort` 自己那支）是既有的，本笔未改。
## 5. 动了什么（一笔一条理由）

| 文件 | 改什么 | 为什么 |
|---|---|---|
| `src/stdlib/graph/txn.h` | 声明 `requireOpenDraft(handle, site)` | 三个门问同一个状态，答案只能有一份 |
| `src/stdlib/graph/txn.cpp` | 实现它（`handle->draft()` 的 `disposed()`，抛 `the draft was committed or aborted already`）；`draftOf` 在取 `transaction->graph()` **之前**调用 | 编辑门：曾经的段错误点 `:201` |
| `src/stdlib/graph/resolve.h` | `viewOf` 在 `transaction->graph()` 之前调用 | 所有按节点读的算子都从这里过 |
| `src/stdlib/graph/read.cpp` | `nodesKernel` / `paramsKernel` / `regionOf` 的草稿分支调用 | 读门：`nodes` 是实测崩过的另一处 |

**没有**做"自动 keep alive"：草稿在帧边界被 dispose 是否应当另说，那是语义决定；本笔只让同一个状态
在三个门上都以诊断回答，而不是解引用悬垂引用。