# 草稿关闭之后仍被当活着的读：一个测出来的帧，和它修好后的样子

**性质**：实测帧 + 两笔修（`draftOf` / `viewOf` / `nodesKernel` 补 `disposed()` 检查；两例钉进计划）。
**枝**：`lane/erase-constant-crash`（rebase 到 `origin/integration@1b55a42200` 之后），两笔：
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

## 4. 修好之后（`1b55a42200` 上重建，`BUILD_EXIT=0`）

**注意一条读数变化**：rebase 到 `1b55a42200` 后，上面那 12 行程序**不再崩**（连跑 10 次全 `rc=0`、输出 `2`），
新加的检查一次也没抛 ⇒ 那个形状在这棵树上已经不再进入 dispose 状态；崩溃只在早先的两颗二进制上测到，
而这段区间里 `src/stdlib/graph` 与 `src/kernel/runtime` **没有任何提交** —— 所以这条旧读数的复现与否
取决于二进制/时序，不能当作本笔的判据。**本笔的判据改用"草稿必然已经关闭"的形状**：

`commit` 之后再用那个节点（草稿一定没了）：

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

两条都在**单程序级**跑过（未起层级）；`graph_edits.plan.toml` 里的两个 `[[tests]]` 已经按
`expect_exit = 1` + `expect_output_contains = [算子名, 那条消息]` 钉好，**计划还没跑**（等层级放开）。

## 5. 动了什么（一笔一条理由）

| 文件 | 改什么 | 为什么 |
|---|---|---|
| `src/stdlib/graph/txn.h` | 声明 `requireOpenDraft(handle, site)` | 三个门问同一个状态，答案只能有一份 |
| `src/stdlib/graph/txn.cpp` | 实现它（`handle->draft()` 的 `disposed()`，抛 `the draft was committed or aborted already`）；`draftOf` 在取 `transaction->graph()` **之前**调用 | 编辑门：曾经的段错误点 `:201` |
| `src/stdlib/graph/resolve.h` | `viewOf` 在 `transaction->graph()` 之前调用 | 所有按节点读的算子都从这里过 |
| `src/stdlib/graph/read.cpp` | `nodesKernel` / `paramsKernel` / `regionOf` 的草稿分支调用 | 读门：`nodes` 是实测崩过的另一处 |

**没有**做"自动 keep alive"：草稿在帧边界被 dispose 是否应当另说，那是语义决定；本笔只让同一个状态
在三个门上都以诊断回答，而不是解引用悬垂引用。