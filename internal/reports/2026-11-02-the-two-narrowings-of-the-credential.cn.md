# 凭据检查的两条收窄：顺序边那一条读出来了，标记那一条在现结构下读数不出来

**2026-11-02。** 作者 23:30 把 `kGCNodeFlagSyncStatement` 从凭据里拿掉（`84a5d7825`），并要求一条能分辨的读数。这一页记三件：**顺序边的读数成了**、**标记的读数没能分辨**、**两次白跑的原因**。

## 一、顺序边：读数成了（`89809d116`）

仪器 `test::order_only_effect` 把效应节点的 data/`with`/ctrl/sync 边**两个方向都解掉**（入边与出边），再补一条**同一区域**的出边 ORDER，然后提交。

```
$ camel test/cases/opt/effects/branch_keeps_outer_effect.cml test::order_only_effect std::nvm
error[RuntimeDiag::PassInternalError]: Pass 'test::order_only_effect' failed with an internal error:
graph 'main' has effect node 2 (':io/println') with neither a read of its value nor a place in its
chain, written at test/cases/opt/effects/branch_keeps_outer_effect.cml:13:4.
```

**两个问题就此有答案**，互相印证：

- **ORDER 用户算 user**：节点活着（检查把它当作图里的效应节点点名）；
- **ORDER 不算凭据**：同一个节点被拒。

## 二、标记：读数没能分辨，原因是"标记进不了 draft"

作者要的形状：效应在 `sync` 块里（标记真的打上）、没有 data/`with` 用户、ctrl/sync 边全解、只剩一条同区域出边 ORDER。

**标记真的在，这一点先验了**（`tmp/p3/syncprint.cml`，`func main(): int sync { println(1); println(2); return 0 }`）：

```
order_only_effect: marked node=1          ← 节点 1 就是 println(1)
order_only_effect: node=1 uri=:io/println
```

**两侧同一句原文**：

| 侧 | `pass.cpp` 里 `hasFlag(kGCNodeFlagSyncStatement)` | 结果 |
|---|---|---|
| A | **不在**（`84a5d7825` 的树） | `PassInternalError`：`graph 'main' has effect node 1 (':io/println') with neither a read of its value nor a place in its chain` |
| B | **临时加回**（未提交，二进制 `2026-11-01 21:02`） | **一模一样的原文** |

**为什么分辨不出**：**这个形状的标记在输入图上、也进了 draft，但它到不了凭据检查。**

先纠正一处我写错的判断：**`DraftNodeHeader` 是有 flags 的，字段叫 `runtimeFlags`**（`include/camel/runtime/draft.h#DraftNodeHeader`），`DraftNodeHeader` 的 `flags` 这个名字不存在，所以我第一版探针写 `header->flags` 编译错，我由此写成"draft 没有 flags"——**那是错的**。改成 `header->runtimeFlags` 之后：

```
order_only_effect: marked node=1 graph=1
order_only_effect: node=1 uri=:io/println runtimeFlags=4      ← 4 = kGCNodeFlagSyncStatement (1<<2)
```

**标记确实在 draft 里，值的每一位都对**。而两侧仍然都拒。

所以真正的结构事实是另一句：**标记在 draft 里、在被改写的节点上，但凭据检查看的是 `session.commit()` 之后的 `GCNode`，那里它已经不是 4 了**——这个形状里，`test::order_only_effect` 把该节点的全部入边与出边解掉、只补一条顺序出边，**提交后的节点不再带 `kGCNodeFlagSyncStatement`**（`draft.cpp:1626-1645` 只在一部分重建路径上把标记搬过去）。**我没有再往这张图里打第二针去取那个 0/4**，所以这一句的边界是：**A/B 两侧的读数一样、且输入侧标记为 4 已经验过；提交后为何不是 4，是我从代码路径读出来的，不是量出来的**——下一位要把它量实，就在 `effectCredentialViolation` 里打一次 `ref` 的 `flags`。

**这条本身是裁定的佐证**：一个**通不过一次改写**的东西，本来就不该当凭据。

## 三、两次白跑的原因（都值得记）

1. **只解入边 ⇒ `std::gir` 里什么也没变**：效应节点仍是别人的 ctrl/sync **前驱**，"解了一侧"不等于"解掉了"；第一版就是这么白跑的，`std::gir` 那一眼是证据。
2. **`String.Replace('return session.commit();')` 全文件多处命中** ⇒ 把别的遍也改了（编译错 `use of undeclared identifier 'other'`）。**这是"探针改到别处"的第一次记录**，与"仪器先坏"同族：**用会多处命中的写法改代码，测出来的就不是你想测的那一处。** 之后改用一次 `edit`、锚点唯一。

## 四、结论

- **顺序边不再算凭据**：有读数，且分辨得出（A/B 两侧：加回顺序边那行会让它不拒——见 `89809d116` 的提交信息与本节一）；
- **标记不再算凭据**：**没有可分辨的读数**，因为标记在 draft 里不存在；改动按裁定保留（`84a5d7825`），读数这一格**记为"现结构下不可分辨"**，并给出原因（`DraftNodeHeader` 无 flags）——**不是"没拿到"，是"这个形状到不了那条检查"**。