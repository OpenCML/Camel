# `T?` 状态槽只有裁定、没有代码，而问题问低了一层

**日期**：2026-11-18
**提出者**：协调侧
**状态**：待作者裁定（本页写死默认建议，并按默认当轮开工）
**触发**：`lane-genref` 按 `0f` 那一页的默认开工，第一步就停手

## 1. 两件实测事实（推翻了两层前提）

### 第一层：`T?` 槽从未落地

作者 2026-11-18 答（`9f6b1700d`）：

> **answer: a generator's state slot is T? and starts null, as ruled 11-16, so a Ref needs no unwritten form**

`lane-genref` 在同一次构建里量的：

```
develop 与作者的 `origin/fsh-dev@9f6b1700d`（**就是写那句答复的那个提交**）**都还是**：
  `src/compile/gct/builder.cpp:6192`
  `cells.push_back({name, zeroValueOf(type) != nullptr ? type : nullptr});`
⇒ **槽类型是裸 `T`；全文没有一处把槽写成 `T?`。**
11-16 答复第 1 条要的算子（「只做拆开 `T?`，是 null 就报内部错误」）**不存在**：
  `include/camel/core/operator_table.h:132` `kWellKnownCount=22`、id 1..21、**无 unwrap**；
  只有不检查的 `:op/tag_null_value`（它正是 `??` 用的那个）
⇒ **⇒ 这是一条「被裁过、没落地」的裁定。**
```

### 第二层：不只是 `Ref` 被挡住 —— **任何类型**的局部都没进过槽

```
槽的判定拿的是 `typeOfPlaceholder`（`builder.cpp:6190`），而它**恒返回 TypeVar**（`:7221-7227`），
`zeroValueOf(TypeVar)` 落 default = nullptr
⇒ **实测**（同一次构建）：`let x = 0` 的生成器同样是
  `:var/new/empty x | var int`、`__gen_step__(__gen_state__: (int,))`
⇒ **⇒ 任何类型的程序局部都没进过状态槽。**
⇒ **⇒ 所以我 2026-11-18 那页把门指在「`Ref` 没有零值」，是**错了一层**：
   真正的门在「槽的类型在这一层是 TypeVar」，与 `Ref` 无关。**
```

## 2. 那么 `Ref` 那半到底卡在哪

`lane-genref` 的话（**它自己标了「推测，没逐位读 plan」**）：

```
`var Ref<int>` 格子的槽不是被计数句柄（`src/execute/exec_plan.cpp:1636` `isCountedHandle` 只认 Ref 码，
格子码是 Tuple），盒子的引用在它最后一个节点用处就交还了
方向仍然对：**值元组 pack 成员时引用由 pack 节点持住**（`exec_plan.cpp:1123-1138`：
「a reference packed into another value comes back where that value's own last use is」），格子没有这条路
```

## 3. 要往下走需要两块**新代码**（`lane-genref` 列举）

```
① 算子表里那个**带检查的拆槽算子**（11-16 已授权：只由编译器发、可能出故障）
② 把槽建在「类型已经知道」的那一层（今天是 TypeVar，GCT 这一层拿不到 `Ref<int>`）
⇒ **两块都会让生成器用例的 gir 快照动** ⇒ 重录与否要裁定
```

## 4. 默认建议（按此当轮开工）

**先只做 ② 的前置测量，不动 ①。**

```
① **先量一件事**：语料里到底有几个程序会因为这个槽而拿到错的行为？
   ⇒ 已知计数：`var_new=213`、`var_new_empty=43`、`cell_typed_nodes=1188`
     （census：files 3891 / listed 3891 / counted 2728，`list_sha=72c1a623…`，base `608ab3dbd`）
   ⇒ **要分清**：43 个 `:var/new/empty` 里，**几个是生成器体里的局部**？
     若这一步只有 `stream/generators.cml` 和 probe，那么②的收益面是**一个用例 + 一个探针**。
② **② 的实现位置先写成一句话**：「类型在哪一层已知」 —— 若答案不在 GCT，说明改点不在
   `builder.cpp:6192`，而在更上游 ⇒ **先定位，再动手**
③ **①（拆槽算子）在②定位清楚之前不做** —— 它会动 gir 快照，而快照动了就要重录，
   重录一笔没有②支撑就是在给一个半成品锁形状
```

**⇒ 理由**：**一条「被裁过、没落地」的裁定，最贵的处理方式是马上把它实现掉；最便宜的处理是**先量它的收益面**。** **⇒ 今晚已经有三处「改对了但语料走不到」（两次 `var_new` 不降、`refcell` 的 213→213）——第四次不该再用同一种方式花掉一个晚上。**

**若作者认为该直接做 ①+② 并重录 gir，请改第 4 节。**

## 5. 相关页

- `docs/handover/questions/2026-11-18-a-ref-has-no-unwritten-form-for-the-state-slot.cn.md` —— **本页纠正它的第 1 节**（门指错了层）
- `docs/handover/questions/2026-11-18-a-cell-holding-a-ref-is-neither-counted-nor-checked.cn.md` —— 再上一层
- `docs/handover/reports/2026-11-18-the-cell-a-handle-needs-no.cn.md`（`refcell` 的报告页）## 裁定（已答，2026-11-18）

**`04ee677e7`：lower generator state where types are known, with `T?` slots.**
⇒ 本页第 2 节的判断被采纳（「这一层只有 TypeVar」）；第 4 节的默认（「先只量收益面」）**已执行**：
43 个 `:var/new/empty` 全在生成器体里、**18 个文件、30 个程序局部 + 13 个机器格子** ⇒ 槽落地时 43 → 13。
⇒ 落地路径见 `2026-11-18-lowering-generator-state-to-gir.cn.md`（几百行、18 个文件快照会动，等点名）。
