# 「两张导数图」这一项按 `std::jgir` 现在的读法比的不是导数图 —— 该改措辞，还是该换读法

2026-11-20 ｜ 来自 `lane-capture-c` ｜ 对象：`internal/reports/eval-round10/capture-all.sh:404-425`（第 8 节）
｜ 读数在 `d69da02be8`，Release，主机 88，指纹 `261120_081441_d69da02be`（`build-info.json`：`clean`）

## ① 事实

**1. 脚本的头注释把「文档」当成了「导数图」。** `capture-all.sh:405-410` 写：

> The two losses are two writings of one loss, and the derivative is a library transform that runs at
> compile time, so **the derivative graph is a graph of the canonical document** — the comparison of
> the two documents is the comparison of the two derivative graphs.

第 423-424 行据此只做一件事：`first_diff loss-a.json loss-b.json`。

**2. 导数图不在那份文档里。** 在 `d69da02be8` 上跑 `camel loss-a.cml std::stats`：

```
GRAPH_STATS {"graphs":5,"nodes":57,"edges":65,"opers":16,"direct_calls":4,"indirect_calls":2,
             "closures":0,"branches":0,"loops":1,...}
```

五张图，节点数 `[2, 17, 14, 8, 16]`；`camel loss-a.cml std::jgir canonical=true` 的全文里**只有一个**
`autodiff:grad@1.0` 节点（在 `step` 那张 8 节点图里，类型是一个函数类型），它的结果被**间接调用**：

```json
{"id":"n3","kind":"OPER","payload":{"op":"autodiff:grad@1.0"},"type":12,...}
{"id":"n5","kind":"CALL","payload":{"call":"indirect"},...}
```

导数图由 `modules/autodiff` 的 `GradResolver`（`modules/autodiff/operators.cpp:220`）在建图/编译之后才产生，
**从不进入规范文档**：文档的 `graphs` 数组里没有它，`std::stats` 也只数程序自己的图。⇒ **这一项比的不是导数图，
是程序文档。**

**3. 两组对照把差别钉死（同一构建、同一秒、`/tmp/capC/run15`）。**

- **不跑 `opt`：逐字节全同。** 两份各 26097 B，sha256 **都是**
  `740b7a29f1b50b059261569df38bac889657ca76fea6467c62826e837a1e9bd1`，5 张图逐图全同（signature `1/1/9/11/14`），
  两个程序自己跑出来的 stdout 也同（`5cd621c3…`，三行 `loss before: 3.7500 / loss after: 0.0000 / [[1.9999925], [-0.99998784]]`）。
- **跑过 `opt`：两份不同。** 各 60189 B，sha256 `bbdedb3d67aa06deabf831cc31671f321a1b01bfd1b6f423b76a76db2dc62e79`（loss-a）
  vs `f90a699b4accbbc47afdd6350951eafb4aced3b6884cae8e133ab6bceb81bec5`（loss-b）。**`std::opt` 本身确定**
  （loss-a 连跑两次，文档 sha256 一模一样）⇒ 是两份写法的差别，不是噪声。
- **第一处差异 = 第 15441 字节**，落在一张 38 节点图的 `order` 输入表里：

  ```
  loss-a: slot3→n26, slot4→n27, slot5→n28, slot6→n29, slot7→n30
  loss-b: slot3→n29, slot4→n30, slot5→n31, slot6→n26, slot7→n27
  ```

- 两份文档**长度相同、每图节点数相同**（`[38, 38, 14, 8, 26, 8]`）⇒ **图形同构，差别只在节点编号/槽位顺序**。
  而两份源码唯一的差别是 `loss` 里 `let count` 与 `let total` 的先后（`case-study-variants/loss-a.cml` 对 `loss-b.cml`），
  差异正落在顺序边的槽位上。

## ② 问题

**这一项该改成什么：把措辞改成「优化后的编号稳定性」，还是先补一个能读导数图的入口、再按原意比？**

两条路的代价：

- **改措辞**：认下「这一项现在测的是编号是否稳定」。论文那句关于导数图的话必须改写，因为今天拿不出它的读数。
- **改读法**：要能读 `GradResolver` 的产物 —— **那个入口今天不存在**（规范导出只有程序图；`std::stats` 也只数程序图）。
  这是新活，而且很可能要先等「案例搬到库 `grad`」落地，因为只有库 `grad` 的导数才以图出现。
- **删掉这一项**：少一条证据，论文那句一并删。

## ③ 默认建议

**改措辞（认下测的是编号稳定），换读法另立一笔待派。** 具体：

1. `capture-all.sh` 第 8 节的头注释按事实改写：**「两张导数图」不是这份文档里的对象**；`first_diff` 比的是两份
   **程序**文档。它的结论要写成两句分开的话：
   - **程序文档**：`loss-a` 与 `loss-b` 逐字节相同（26097 B，`740b7a29…`）—— 这条成立；
   - **导数图**：今天没有读数（导数图不进规范文档）。
2. 论文侧不要写「两张导数图规范形相同」。能写的是：**两份写法把同一个损失写成同一个规范文档**，以及
   **优化之后两份文档仍同构、只在独立语句的顺序边槽位上不同**（字数、每图节点数都相同）。
3. **换读法（读 `GradResolver` 的产物）不在本轮做**：上一轮派工的「采集脚本补齐」里没有这一格，而定版 pin 已近；
   它要新开一个「让导数图可读」的入口，那是设计决定，不是采集决定。

**顺带一条给后来人的**：若哪天把这项改成「优化后的编号稳定性」，判据要写成「两份同构、每图节点数相同、字节不等」。
**只报「字节不等」会把「图不同」和「编号不同」混成一句**，而这两件事在这一项里必须分开（前者是缺陷，后者是排序规则的输出）。