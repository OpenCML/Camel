# c4 的退回记录：`opt.equal_branch|equal_branch.known_gradient` 是一条真红

**状态：退回记录，不是进队依据。** 枝 `lane/c4-round-source`（`588f35b940`）**不进队**（`甲` 未消），
本轮结束时它 `dirty 0`、未 push、尖未动。本页记的是：这条红在哪里、怎么读出来的、以及**机制到哪一层为止**。

- **枝**：`lane/c4-round-source`，尖 `588f35b94016f3d0420fdbc67992ee3fb372de62`。
- **跑它的工作树**：`D:\Projects\Camel-wt\hdr-channel`（主机 `/home/zjwei/camel-work/hdr-channel`），
  探针一律在**临时枝**上做、用完即删，`lane/c4-round-source` 一个字节没动。
- **日期**：2026-11-28。

## §1 落点

c4 在 `src/stdlib/std/cxx/opt/generic/rewrite.cpp#isPureOper` 新增一条判据：

```
+    if (!draft.orderInputsOf(id).empty()) {
+        // … a node that takes an order edge is written after something …
+        return false;
+    }
```

即 **"带 order 入边的节点一律不可折 / 不可移 / 不可合并"**（原来只问
`OperatorTable::instance().mayBeRecomputed(body->oper)`）。这条判据在 `std::opt` 的引擎里，
不在那些可以单跑的 `opt::*` 遍里。

## §2 读数

**两遍同红（真红，不是不稳）**，同一棵树、单计划、`-Jobs 2`：

```
A 遍 [FAIL] equal_branch.known_gradient   214.06 ms   [SUMMARY] total: 36, pass: 35, fail: 1
B 遍 [FAIL] equal_branch.known_gradient   249.33 ms   [SUMMARY] total: 36, pass: 35, fail: 1
verification output: Error: known gradient did not fold      （两遍同一句、同一断言）
```

断言原文要求 `main` 图里不出现 `:op/mul_d` / `:op/add_d` / `:str/format` 三者之一；命中的是第一句
（第二句 `condition lost` 没到）。`std::fvm` 两侧都打印 `collapse dh=22.0` ⇒ **语义没错，只是不复折**。

`collapse.cml` 的 `main`，两侧同一命令 `std::macro std::opt std::jgir`：

| | base（`origin/integration`） | c4（`588f35b940`） |
|---|---|---|
| `main.nodes` / `edges` | 15 / 24 | 17 / 28 |
| 常量折叠结果 | `"collapse dh=22.0"`（14 字符常量） | **没有**；`"collapse dh={}"` + 独立 `"22.0"` |
| `:str/format` | 无（已折掉） | **在**（`g1n14`） |
| order 边 | **零条** | `ORDER N28(JOIN) -> N2C(:str/format)`、`ORDER N39(JOIN) -> N3D(:str/format)` 两条 |

`std::macro` **单独**跑（`std::macro std::jgir`）两侧**逐项相同**（21 节点 / 38 边、同一份 label 集合、
都有 `Function(graph=grad(h), tupleSlots=0)`、都没有 `autodiff:grad`）⇒ `grad(h)` 两侧都被展开，
**分差点不在 macro，在 `std::opt`**。

单遍两侧逐条相同、**都不折**（`folded=False`、`has_format=True`）：

| 管线 | c4 `nodes` | base `nodes` |
|---|---|---|
| `opt::fold` | 21 | 21 |
| `opt::cse` | 21 | 21 |
| `opt::dce` | 20 | 20 |
| `opt::fold opt::cse` | 21 | 21 |
| `opt::fold opt::cse opt::dce` | 20 | 20 |
| `std::cxx::fold` | 21 | 21 |
| `std::cxx::cse` | 20 | 20 |
| `std::cxx::algebra` | 21 | 21 |
| `std::cxx::dce` | 20 | 20 |
| `std::cxx::fold std::cxx::cse std::cxx::algebra` | 20 | 20 |

⇒ 折只发生在 `std::opt` 自己的引擎循环里；单跑的这些遍在**两侧都不折**，所以"分差在哪一遍"问不出来。

## §3 不是谁造的（这条 order 边），以及**未定量**的那一层

一条 order 边在 `src/compile/src/gir/builder_helpers.h#nodeTakesAnOrder` 里有四个出口，逐一读过：

| 出口 | c4 上命中次数 | 命中里有没有 `:str/format` |
|---|---|---|
| `nodeStandsForItsRegion(node)`（region owner 恒真） | **0** | — |
| Oper：`operatorHasResourceAccesses(id)`（c4 新增条款） | 2+2+2+1（`env:get_env`×2、`:io/println`×2、`graph:relative_cost`×2、`rewrite:rewrite`×1） | **没有** |
| Oper：`!operatorArrangeable(id).all()` | 同上集合 | **没有** |
| 非 Oper：`!nodeIsFreelyArrangeable(...)` | **0** | — |

`nodeTakesAnOrder` 共被调用 **29 次，全部只走到入口打印**（`when=enter`，`kind=11` 全是 Oper）；
`when=region_owner` **0 行**、`when=not_freely_arrangeable` **0 行**。
同一打印在 base 上跑（base 没有 `operatorHasResourceAccesses` 这一支）：命中集合是
`env:get_env`×2、`:io/println`×2、`rewrite:rewrite`×1，**也没有 `:str/format`**。

⇒ **`:str/format` 的 order 入边不是 `nodeTakesAnOrder` 产生的**，它来自**建图 / 重写路线的别处**。

**未定量的那一层**：`isPureOper` 与"这条边该不该有"之间还隔着一环——
*建图侧到底是谁给这个节点加了 order 入边*。这决定了两种完全不同的读法（判据太宽 / 边本身不该有），
而本轮**没有**任何一条读数能在两者之间选边，所以本页**不判**"`isPureOper` 过宽"，也**不判**
"region owner 不该有 order 边"。

**下一步若还要查，从这两处入手**：

1. **`orderInputsOf` 的写者**：`camel::runtime::GraphTransaction` 上谁调 `addOrderEdge` / 谁写
   order 输入——把写者连调用点一起打出来（不是看消费端 `nodeTakesAnOrder`），对比 c4 与 base 的写者集合；
2. **`rewrite.cpp` 之外**：`std::opt` 的引擎里，重写事务重建节点时是否会**继承**源节点的 order 输入
   （若继承，边就是"随重写搬过来"的，而不是新造的）——这要看 `rehomeOrderEdges`
   与重写事务的节点替换路径，而不是 `nodeTakesAnOrder`。

## §4 复算

探针都放在**临时枝**上（`test/tools/probe/*.plan.toml`，主机没有任意命令通道，计划是唯一跑法；
枝用完即删，`test/plans/` 从不参与）。要点与踩过的坑：

- 读数**写文件**（`$CAMEL_ACCESS_LOG` / `$CAMEL_EARLY_LOG`）再由驱动读回来打印成 stdout，
  **不走 stderr**；驱动故意 `sys.exit(1)`，因为失败条目的 stdout 才会被回显。
- 一次 `std::jgir` 的 stdout 是 **JSON + 程序自己的输出**（`collapse dh=22.0` 在后面），
  所以要用 `json.JSONDecoder().raw_decode` 取第一段，别 `json.loads`。
- 回显有截断（`omitted N line(s) above`），完整输出在宿主的
  `test/results/log/<run>/fail/<name>/stdout.txt`，可以直接 `ssh 88zjwei cat` 取，别再重跑一遍补。

命令（每一步都在临时枝上，`-Jobs 2`、`-QueueOnTreeLock`）：

```powershell
& "D:\Projects\Camel-wt\witness\internal\tools\camel-remote.ps1" `
  -Path "D:\Projects\Camel-wt\hdr-channel" `
  -Plan "test/tools/probe/<probe>.plan.toml" -Tier fast -Jobs 2 -QueueOnTreeLock -NoGpuWatch
```

## §5 这一页成立到哪一步

- 成立：**这条红是真红**（两遍同句）；**分差点在 `std::opt` 的引擎，不在 macro、不在单遍**；
  c4 新增的 `isPureOper` 判据确实"看到"了 `:str/format` 的 order 入边。
- 不成立（本页不主张）：`isPureOper` 判据过宽、order 边不该有、c4 该改哪一行。
- 已知缺口：**order 入边的写者**未定量（§3 末的两条入手点）。