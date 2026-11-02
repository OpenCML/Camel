# P0-3 收尾：只留顺序边的效应今天造不出来，所以这条判据的读数是"报了，但分辨不出"

**2026-11-02。** 这一页记 P0-3 这一笔的**结论与读数**，供复查者按同一口径复现。代码在 `lane-bisect-script`：`595fa5143`（判据 + 测试遍）、`dc029c0e7`（遍自己造顺序边）。

## 读到了什么

把 `effectStandsOnSomething` 的 `orderPreds`/`orderUsers` 去掉之后，用 `test::order_only_effect`（把第一个 `:io/` 效应的 data/`with`/control/sync 边全解掉，再挂一条顺序边）跑：

```
error[RuntimeDiag::PassInternalError]: Pass 'test::order_only_effect' failed with an internal error:
Runtime graph 'f' breaks its edge invariants (1 violation(s)):
  graph 'f': order edge 2 -> 4 enters region 1; an arm or a part of a loop runs as a whole, so the
  order it has with the code around it is written on the node that owns the region
  (docs/design/effects/edges.cn.md 4.6).
```

**报的是 `PassInternalError`——但理由不是凭据那一条。** 作者要的是 `neither a read of its value nor a place in its chain`；实际先开火的是 `4.6`（顺序边跨进了区域），**凭据检查没轮到**。

按元规则"读数要能分辨被审的机制"：**这条读数拿到了，但分辨不出**——它证明"提交会拒这张图"，不证明"拒是因为顺序边不再算凭据"。

## 为什么分辨不出：形状约束

- **跨区域**挂顺序边：先破 `4.6`（上面那条）。
- **同区域**挂（把 anchor 换成效应自己区域里的节点，`nodeRegion(id)` 相同）：**这一版跑下来什么也不报**（`arm_div_unused.cml`、`effects_model_edges.cml`、`branch_keeps_outer_effect.cml` 三个程序，`grep PassInternalError` 空）——那张图在提交时被接受，测试遍也就没有得到它的读数。**这一版没有提交**（它没量出东西，不留无判据的遍）。

所以：**"只留顺序边的效应"今天造不出来——跨区域先破不变量，同区域又落不到拒的那条路上。** 这是**形状约束**，不是判据问题。

## 结论（写进提交信息、也写在这里）

1. **P0-3 不是防今天的程序，是防未来的遍**：下一个把效应留在顺序边上的遍——在同一区域内、不破 `4.6`——今天会被提交接受，收窄之后会被拒（若有谁把凭据检查放到不变量检查之前，这条读数就会分开）。
2. **今日的读数是"报 `PassInternalError`，但不是凭据那一条"**，不是"没拿到"、也不是"改对了"。
3. **代码保留**：`effectStandsOnSomething` 只认两样（值被读 / 链路位），`test::order_only_effect` 作为正式测试遍留在树上（名字就是开关，与 `test::drop_call_argument` 同族）。
4. **"没有顺序边的效应仍不报"成立**：`dce_effect_under_pure_arithmetic` 跑这个遍不报任何错（那张图里没有 `:io/` 节点，遍匹配不到）——**收窄的只是"顺序边算凭据"，不是把检查变严**。

## 边界：`isMember` 那一笔的行号

作者清单写 `graph_runtime_support.cpp:532`/`:534`；**实际改的是 `:509`**（`computeExecutionOrder` 里顺序前驱的访问，`live[edge.pred] || isMember(edge.pred)` → `live[edge.pred]`），而 `:534`（收尾循环）**在此之前就已经是 `live[n]`**（第一笔 `b21d30d8b`）。三点：

- `:532` 在今天这份源码里是注释行（"its members are not, and seeding them here…"），不是代码；
- 复查请按**符号**找：`git grep -n 'isMember' src/execute`，改后只剩定义（`:242`）与两处分组（`:251`/`:261`）；
- `|| isMember` 式取种：**0 处**。
## A4b 与 P0-1 的关系：这条读数分辨不出 `:509`（两侧逐字相同）

**A4b** 是"Region yields node … not in the execution"那一类；`test/cases/trans/reduced/` 在这个工作树上不存在（它在 `verify` 的车道里，没并过来），所以两侧都跑 `docs/handover/reports/eval-defects-2026-11-01/minimal/` 里现成的两支（`recompile_segv.cml`、`cml_segv.cml`），四个组合（`nvm` / `opt nvm` / `fvm` / `opt fvm`）。

**两侧先建好再量**（这是今晚第三次同族教训：二进制与树要同代）：
- **A 侧**：HEAD `a9c9e8d4c`（`:509` = `live[edge.pred]`），二进制 `2026-11-01 18:05:29`；
- **B 侧**：把 `:509` 临时改回 `live[edge.pred] || isMember(edge.pred)`（未提交），二进制 `2026-11-01 18:15:22`。

**读数（两侧逐字相同）**：

| 程序 | 遍 | A：`rc` / stdout | B：`rc` / stdout |
|---|---|---|---|
| `recompile_segv.cml` | `nvm` | `0` / 空 | `0` / 空 |
| 同上 | `opt nvm` | `0` / 空 | `0` / 空 |
| 同上 | `fvm` | `0` / 空 | `0` / 空 |
| 同上 | `opt fvm` | `0` / 空 | `0` / 空 |
| `cml_segv.cml` | `nvm` | `0` / `let 42 101;` | `0` / `let 42 101;` |
| 同上 | `opt nvm` | `0` / `let 42 101;` | `0` / `let 42 101;` |
| 同上 | `fvm` | `0` / `let 42 101;` | `0` / `let 42 101;` |
| 同上 | `opt fvm` | `0` / `let 42 101;` | `0` / `let 42 101;` |

**结论（如实）**：**这条读数分辨不出 `:509`。** 八个格子两侧一字不差。原因与今晚前面四次一致：这两支程序里没有"臂里有人、却没被用到、却仍进了计划"的形状——前端与遍先把它们处理掉了，`:509` 换成任一种写法都不改变谁会执行。**A4b 与 P0-1 不同根。**

**临时改动已恢复**：`git grep -n '|| isMember' src/execute` → **0**，`dirty=0`。

## 作者页第 4 条下要补的一行（P0-1 第一条读数）

> **已修**：`b21d30d8b`（收尾循环只按存活）、`4ea783758`（顺序前驱访问只按存活）、`d7836f2e9`（问题页：`let z = 1/0` 前端判死，按规则删）、`595fa5143`（P0-3 的凭据判据）。**读数**：24 格四后端相同；`let z=1/0` 四后端 × `opt` 全 `2`、不报除零；`nonlive_members` 0/0；A4b 两支 × 四组合两侧逐字相同 ⇒ **这些读数分辨不出机制，改动按 3.1 的"当且仅当"保留，不按读数声称**。
>
> **行号说明**：作者清单写 `:532`/`:534`；实际改的是 **`:509`**（顺序前驱访问），`:532` 是注释行，`:534` 在第一笔起就已经是 `live[n]`。复查请按符号：`git grep -n 'isMember' src/execute` → 定义 `:242`、分组 `:251`/`:261`，`|| isMember` **0 处**。

**替代了靶子，页里写明**：`test/cases/trans/reduced/`（`verify` 车道收的 4 条）**不在这棵树上**，所以 A4b 那两侧用的是 `docs/handover/reports/eval-defects-2026-11-01/minimal/` 里的 `recompile_segv.cml` 与 `cml_segv.cml`。下一位若在原目录下量，请把四格补上——结论（分辨不出）不变的话不必重写这页，变了就另写一页。
