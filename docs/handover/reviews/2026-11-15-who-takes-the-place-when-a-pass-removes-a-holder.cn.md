# 一个 pass 取走持位节点之后，位置由谁接？

**状态**：待裁。本页问的是**架构**问题，不是单个 pass 的语义：同一个漏口今天已经出现在两个 pass
里，修补该留在每个 pass 内，还是上提到提交前的统一一步。

## 1. 漏口是什么

链上一个节点同时持有两侧的位置（`holdsChainPlace`，`src/passes/opt/generic/rewrite.h`：

```
syncUsersOf(n) 非空                   // 写在它后面的语句等它
syncInputsOf(n) 非空 或它是 chain GATE // 它等写在它前面的语句
```

pass 把这样的节点取走（折成常量、合并进另一个节点、删掉），**它带的那次等待就没人接**：
真正做拼接的只有 `closeChainOver`（`src/runtime/draft.cpp`，`eraseNode` 调它），而它只在能命名一个
仍然存在、且与后继同块的前驱时才拼接，**做不到时静默什么都不做**。剩下的语句只带着 `ORDER` 边，
而 `ORDER` 不携带位置（`docs/design/effects/edges.cn.md` 1.5、5.3），于是 `std::opt::dce` 报

```
graph '<g>' has effect node <n> ('<oper>') with neither a read of its value nor a place in its chain
```

## 2. 今天已经有两个 pass 撞上它

| pass | 取走节点的方式 | 读数 |
|---|---|---|
| `std::opt::fold` | 折成常量（`replaceFoldResult`） | `test/cases/opt/e9/rc00_seed30.cml`：`std::opt` exit 1 → 修补后 exit 0，四个后端打印与不过 pass 逐字相同 |
| `std::opt::cse` | 合并成同值节点（`replaceNode`） | `test/cases/compile/function_values/value_positions.cml`：`std::opt std::nvm` exit 1（`graph '__root__' has effect node 8 (':io/println') with neither a read of its value nor a place in its chain`） |

`fold` 侧已修（`src/passes/opt/generic/generic.cpp#placeLostStatements`，链头被折时后继接任链头；
中间那种由 `closeChainOver` 做）。`cse` 侧未修。

## 2.1 `cse` 侧为什么没顺手修掉：Camel 那边没有写 sync 边的能力

`std::opt::cse` 与 `opt::cse`（`stdlib/opt.cml`，用 graph 模块写的同一件事）必须逐节点一致
（`test/plans/feat/opt/camel_passes.plan.toml` 的 `by_cpp` / `by_camel` 两条）。而 graph 模块**没有**
写 sync 边的原语：可写面只有 `open/replace/erase/add_constant/commit/abort`（`stdlib/graph.cml`
"transactions" 一节），`sync_inputs`/`users`/`order_inputs` 都是只读。所以在 C++ 里补、在 Camel 里
补不出来——除非给模块加原语（会改变模块的公开面），或者换形状（`cse` 干脆不合并这样的节点）。

这条本身就是 (iii) 的证据：**每个 pass 用自己的编辑词汇修补同一个漏口，最后两边对不上。**

## 3. 问题

「谁把持位节点取走，位置就由谁交给后继」这件事，应当

- **(i)** 留在每个 pass 内各自同形修补（今天 `fold` 是这样），还是
- **(ii)** 上提到提交前的统一一步：`closeChainOver` 做不到时不再静默，而是把这次丢失交给一个统一的
  修补（或直接拒绝这次编辑，让 pass 自己知道它取走了一个没人接的位置）？

## 4. 默认（本页写死）

**暂不上提，先在每个 pass 内同形修补；等第三个 pass 出现同一漏口再上提。**

理由：
1. 今天有**两个** pass 有同一漏口（`fold`、`cse`），两个点还画不出一条线；上提意味着改 `closeChainOver`
   的契约（它现在"做不到就什么都不做"是有意的：`eraseNode` 的调用点太多），那是跨 pass 的语义变更，
   属作者裁量。
2. 每个 pass 的修补需要它自己的判据（`fold` 折的是纯计算，`cse` 合并的是同值节点），同形但不共享。
3. **上提的信号写死在这里：第三个 pass 出现同一漏口（举例：`std::opt::algebra`、`std::opt::dce` 的
   `keepThroughValue`、或任何走 `eraseNode` 收走持位节点的变换），就说明这不是"各 pass 的细节"，
   而是提交路径缺了一步——那时上提，并把本页转成缺陷记录。**

## 5. 复现

```
camel test/cases/opt/e9/rc00_seed30.cml std::opt                 # fold 侧（已修）
camel test/cases/compile/function_values/value_positions.cml std::opt std::nvm   # cse 侧（未修）
camel test/cases/opt/camel_passes/cse_subject.cml std::opt::cse std::gir          # 两个实现的对照面
```
## 决策者答复（2026-11-15）

**取 (ii)，现在就上提，不等第三个遍。** 原则 9.7 已经写明删除只经专门的操作，接链由那个操作负责；"同一类事一条规则"也不允许每个遍各自同形修补。做法：

1. 草稿的编辑接口（`eraseNode`、`replaceNode`、合并节点，以及 Camel 侧 `graph` 模块的 `erase`/`replace`）取走一个持位节点时，**自己**把位置交给后继：`P --SYNC--> F --SYNC--> S` 改为 `P --SYNC--> S`；`F` 是链头，`S` 接任链头。`closeChainOver` 不再"做不到就什么都不做"。
2. 真的交不出去时（没有同块的后继，也接不上前驱），编辑**失败并说明原因**，不静默成功；遍拿到失败就知道自己取走了一个没人接的位置。
3. `fold` 的 `placeLostStatements`、`cse` 里的同形修补，以及 `graph` 模块为此加的 `set_sync_predecessor`，在编辑接口接手后删除。
4. 验收：`rc00_seed30` 与 `fold_cast` 读数不变；`cse` 那条红随之变绿；E9 慢层的三个数不升。

归第 2 路，优先级一。`closeChainOver` 的调用点多，正说明它应当在一处做对。
