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