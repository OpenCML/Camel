# 跨 Inline 区的 order 边对效应序检查不生效：正向被拒，反向被接受但静默无效
2026-11-20 ｜ 来自 `lane-ecoop-e2e` ｜ 对象：`graph::order` 与 `commitViolation`（`PassEffectOrder`）；`src/runtime/resources.cpp:988,1586`、`src/runtime/graph_edges.cpp:618-638`

派工的原话是「`inline` 一个打印，**用今天修好的 `order` 排在原有打印之后**」。照着做了，读数是：
**正向被拒（与该边不存在时一字不差），反向被接受、而标记根本不跑。** 这不是 pass 写错，是接口。

## ① 现场（两条命令，逐字）

程序与 pass 都在 `internal/reports/eval-round10/e2e/`（`program.cml`、`passes.cml`），跑在主机
`~/camel-work/e2e`，`out/latest`（`02939ffb7a`，Release）。

要做的编辑：闭包体（g4，11 节点）里唯一一条链语句是它的 `:io/println`。pass 在一个**不在链上**的纯节点
（`weight = base + 1`）处 `inline` 一个只打印 `# tick` 的 body，得到 `added`（落在它自己的
`RegionKind::Inline` 区），再用 `order` 把它排在原打印之后。

**正向**（派工所指定）`camel program.cml passes::instrument_ordered std::nvm`，exit 1：

```
error[RuntimeDiag::PassEffectOrder]: 'graph::commit' committed a graph whose conflicting effects are not ordered: node 9 of graph '__g_441__' (:io/println), in one region of graph '__g_441__', has no order against node 13 of graph '__g_441__' (:io/println), and at least one of the two writes (write stdout / write stdout): no data edge, no sync edge, no order edge and no region's GATE input makes one wait for the other, so the commit would publish two effects on one resource that the program leaves free to happen in either order
    --> /home/zjwei/camel-work/e2e/internal/reports/eval-round10/e2e/passes.cml:146:12
     |
 146 |     return println('# tick')
     |            ^^^^^^^
```

**反向** `camel program.cml passes::instrument_reversed std::nvm`，exit 0，输出与原程序**逐字相同**：
`step 0 -> 11 / step 1 -> 12 / step 2 -> 13 / step 3 -> 14 / total 86 tail 14`，一行 `# tick` 都没有。

两条命令只差 `order` 的两个参数：正向 `order(t, 原打印, added)`，反向 `order(t, added, 原打印)`。
**把正向那条边加进去，拒绝原文与完全不加 `order` 时（`passes::instrument_unordered`）逐字相同。**

## ② 反向那一半更糟：标记在图里，不在程序里

`passes::instrument_reversed passes::report`：`e2e graphs=5 nodes=56`（改前 53），
`e2e canonical=58ce1b9f2d44b9175092ce94e1a680278a098b1b016bcc89e61ee2805ae8aff7` —— 图**变大了**。
`passes::instrument_reversed passes::dump` 把闭包体打成 14 个节点：

```
7 :op/add_l int region=RegionKind::Root anchor=false free=true key=:op/add_l|int|,2,3
8 :str/format string region=RegionKind::Root anchor=false free=false key=:str/format|string|,5
9 - string region=RegionKind::Inline anchor=false free=true key=-|string|
10 :io/println string region=RegionKind::Inline anchor=false free=false key=:io/println|string|,9
11 - string region=RegionKind::Inline anchor=false free=false key=-|string|,10
12 :io/println string region=RegionKind::Root anchor=false free=false key=:io/println|string|,8
13 - int region=RegionKind::Root anchor=true free=false key=-|int|,4
```

⇒ 标记的 `# tick` 常量、`println` 和 body 的出口都在 `RegionKind::Inline`；闭包自己那行（n12）在 `Root`。
一个 Inline 区只记录「代码从哪来」，**自己没有任何执行**（`graph_edges.cpp:637-638`：「A region that only
records where code came from -- an inlined body, a tagged block -- is looked through: it holds no
execution of its own」）。没有任何节点从 n11 取值，所以这个区从不进入、`println` 从不跑。
**检查算它是一个写、并且靠那条反向边认定它有序；执行时它不存在。** 同一个节点，两个答案。

## ③ 不一致在哪

* **正向**：边写成「`added` 的 order 前驱 = 原打印」，即这条边的 **user 在 Inline 区里**。到检查手上时
  这条边已经不在图里（否则 `orderedBefore` 双向任一条即可满足，见 `resources.cpp:988`），于是判无序 ⇒ 拒绝。
* **反向**：边写成「原打印的 order 前驱 = `added`」，**user 在 Root 区**。这条边活着，检查满足 ⇒ 接受。
* 同一条边、同一对节点，**只因为 user 落在 Inline 区还是 Root 区，结论相反**；而两种写法都没有让标记跑起来。
* 不会有别的提示：`EdgeChecker` 的 `crossedControlRegion`（`graph_edges.cpp:621-627`）明确**看穿** Inline 区
  —— 「A region that only records where code came from … is looked through」—— 所以它不报「order 边进入区」；
  唯一说话的是 `PassEffectOrder`，而它对正向和「压根没有边」给的是同一句。
* 机理上真正要判的两点：**(a) 跨 Inline 区的 order 边不该被丢**（或者该由同一条规则判两向）；
  **(b) 一个区的写被算进效应、而该区不执行，这个组合应当拒绝**，而不是接受。

## ④ 建议（默认建议，写死）

1. **按缺陷修 `order`：跨 Inline 区的 order 边要生效。** 派工的前提就是「`order` 已修好」，所以**不按现状改
   例子**；例子保持现在的形状（`internal/reports/eval-round10/e2e/`），修好后 `instrument_ordered` 应当
   直接通过，`# tick` 每调一次一行、排在原打印之后。
2. 顺手把 (b) 堵上：判对用的区与「这个区是否会执行」不一致时，让 `commit` 把这句话说出来。
3. 今天唯一能走通的写法是 `inline` 打在**链语句**上，body **顶替**该语句（继承它的位置与 order 边）——
   这就是本 lane 交付的 `passes::instrument`：能跑、每次调用一行、总数不变，代价是闭包原来那行被顶掉。
   另一支 `passes::dedupe`（把闭包里写了两遍的 `weight + x` 合成一个节点）输出**逐字相同**，用来交
   「改后运行、输出相同」这一支。

## ⑤ 附：「去掉闭包调用的效应声明」这个反例做不了

要造派工更想要的 `PassEffectBeyondType` 反例，需要一个能改函数值**类型**的图操作；今天没有。效应写在类型上
（`passes::dump` 里两个 `Func` 节点的类型是 `(x: int) => int uses var stdout`），而 `replace`、`inline`
都把类型一起带走，pass 无法让一个值的类型比它的图声称得少。只能另写一个类型里没有该效应的程序 —— 那不是
「编辑这个程序」，所以本 lane 只用 `PassEffectOrder` 那条反例。

## ⑥ 复现

```
sh internal/reports/eval-round10/e2e/capture.sh \
   > internal/reports/eval-round10/e2e/raw-output.txt 2>&1
```

`raw-output.txt` 301 行，上面每条命令与每段输出都在里面（`===== the edits the commit refuses =====` 与
`===== and the one it accepts and drops: the marker never runs =====` 两节）；`passes.cml` 里
`instrument_ordered`、`instrument_unordered`、`instrument_reversed` 三个导出就是这三条命令。