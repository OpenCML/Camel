# 生成器状态槽要存一个 `Ref`，而 `Ref` 没有「未写」的形态

**日期**：2026-11-18
**提出者**：协调侧
**状态**：待作者裁定（本页写死默认建议，并按默认当轮开工）
**触发**：作者裁定「a cell holding a Ref is a holder, and the cell should go」（`9d29939c4`）之后，`lane-refcell` 只做掉了一半

## 1. 已经做到的（一半）

作者给的两种形态，一种已经消失：

```
**消失**：`var box = state::ref(0)`，由闭包读 / 写的形态
  ⇒ `std::jgir` 里 `cell_read_by_closure` / `cell_written_and_read` 两张图的 `:var/new` **0 次**
    （改前有一枚 `var Ref<int>` 的 `box`），闭包捕获口仍是 `Ref<int>`
  ⇒ 运行 `5 / 5 / 5 / 9`（改前第二、三个是 `InvalidArgument`）
⇒ 读数在 `67beb9b3d`（`lane-refcell`，6 files 350+/30−），新增红 0
```

## 2. 还在的（另一半），以及为什么

```
**还在**：生成器体里的局部
  `let box = state::ref(0); state::set(box,5); yield state::get(box)`
  ⇒ 图里 `:var/new/empty`，类型 `var Ref<int>` —— 格子没消掉
  ⇒ `state::set` 仍报 `InvalidArgument`（早释放：`created=1 counted=1 released=1 live=0`）
```

`lane-refcell` 给出的原因，逐字：

> 「一个 pull 里造出的 box 要活到后面几次 pull ⇒ 必须进生成器的**状态槽**；槽要先有一个「未写」的值，而 `zeroValueOf` 对 `Ref` 返回 `nullptr`（`src/compile/gct/builder.cpp:4096` 明写「其中之一将来要有未写形态，这就是那道门」）。给 `Ref` 定一个未写值 = 语言决定。」

**⇒ 也就是说：这不是缺陷，是**那个门本来就写着「将来要定」**。**

## 3. 要裁的是什么

**`Ref` 的「未写」形态是什么？**

四条路：

| 路 | 做法 | 代价 |
|---|---|---|
| **A（默认建议）** | **不给 `Ref` 定未写值**：生成器体里的 `Ref` 局部**不许进状态槽**，改为「一次 pull 内造、一次 pull 内用完」——跨 pull 存活的那条路直接诊断 | 会拒掉一类程序；但**拒一个说不清的东西，好过让它带着 `InvalidArgument` 跑** |
| B | 给 `Ref` 定一个未写值（null handle），状态槽存它 | 要在运行期到处判 null，且 `Ref` 从此有两种「没有」：未写、已释放 |
| C | 生成器体的句柄局部另有落点（不是状态槽） | 要回答「它放在哪、谁来释放、跨调用怎么找」——`refcell` 说「只需你答一句」，但这一句背后是一处新机制 |
| D | 别裁：写成语言禁令——生成器体里不许有 `Ref` 局部 | 与 A 相似但更硬；A 是「跨 pull 才拒」，D 是「一律拒」 |

## 4. 默认建议（按此当轮开工）

**走路 A，并且先只做「量」。**

```
① **不动 `zeroValueOf`、不动生成器状态元组、不动 5.4**（`refcell` 已经明确没动这三处，别接手去动）
② **先量一件事**：语料里有没有**任何**程序会把一个 `Ref` 局部跨 pull 存活？
   ⇒ 今晚已知：76 处 `state::ref` 全是 `let`；唯一带 `yield` 的 `stream/generators.cml` 里那个 box 是**形参**
   ⇒ **⇒ 若答案是「没有」，那么 A 的代价是零** —— 这一类程序今天不存在，
     定一条诊断只是把「今天不存在的东西」写清楚。**先量再裁。**
③ **量完若确实没有** ⇒ 加诊断，把那一形态拒掉，并在报告里写「语料中 0 例」+ 人口
④ **量完若发现有** ⇒ **停手报我**，把那些程序列出来，**不要自己定未写值**
```

**⇒ 理由：**`Ref` 的「未写」不是一处实现细节，是**语言里第一次出现「一个句柄可以还没被造出来」**。** **⇒ 而今晚已经三次证明：这类东西该改表示，不该改用它的人（`halt` 位、区域交出的值、装 Ref 的格子）。** **⇒ 定一个 null 形态是最省事的改法，也是最会把「没有」变成两个字的那个改法。**

## 5. 一条附带欠账（与本页同源，别忘）

```
`lane-refcell` 这一笔改的是对的，但 **`var_new` 213 → 213，不降**：
人口里没有「局部装 `state::Ref`」的程序，改到的形状只在探针里。
⇒ **⇒ 一个语义改正，如果语料里没有任何程序走它，它就还没有被判据钉住。**
⇒ **欠一个语料用例**：把 `cell_read_by_closure` / `cell_written_and_read` 那种形态
   （闭包读/写一个 `Ref` 局部）做成 `test/cases/` 里的正式判例，钉住 `:var/new` 为 0 与那个 `5 5 5 9`。
⇒ 不放在这一页解决，但要在同一批里落。
```

## 6. 相关页

- `docs/handover/questions/2026-11-18-a-cell-holding-a-ref-is-neither-counted-nor-checked.cn.md` —— 本页是它的续篇
- `docs/handover/reports/2026-11-18-the-cell-a-handle-needs-no.cn.md`（`refcell` 的报告页）
- `docs/handover/reviews/2026-11-17-who-owns-the-argument-after-a-call.cn.md`（S3b，与本页无直接关系但同属「谁持有」）
## 决策者答复（2026-11-18）

**四条路都不走，这件事 11-16 已经定了**：`reviews/2026-11-16-what-holds-a-generator-local-that-is-not-written-yet.cn.md`——"槽写成 `T?`、创建时为 `null`……拆开用带检查的算子"。生成器状态槽的"未写"不是 `T` 的某个零值，而是 `T?` 的 `null`；对 `Ref` 也一样：槽的类型是 `Ref<int>?`，创建时为 `null`，步进体读槽时用那个带检查的拆开算子拆成 `Ref<int>`。`Ref` 本身没有、也不需要"未写"形态，"`Ref` 有两种没有"的担心（路 B）因此不存在：`null` 属于 `T?`，不属于 `Ref`。

所以根子在 `zeroValueOf`：状态槽不该向类型要"零值"，而该一律是 `T?` 的 `null`。按第 8 条规矩在表示处改：生成器状态槽统一为 `T?`，`zeroValueOf` 不再被状态槽调用（若它已无别的调用者就删除；`gct/builder.cpp:4096` 那个"将来要定"的门随之消失）。

不走 A、D：一次 pull 里造的 `Ref` 跨 `yield` 用，是再普通不过的生成器写法（计数器、累加器），"语料里 0 例"只说明语料还没写到，不是可以拒掉它的理由。补一条用例：生成器体里 `let box = state::ref(0)`，每次 pull `state::set(box, state::get(box) + 1)` 后 `yield`，连续取三次得 `1 2 3`，四个后端一致、计数平衡。

- `docs/handover/reviews/2026-11-17-who-owns-the-argument-after-a-call.cn.md`（S3b，与本页无直接关系但同属「谁持有」）## 裁定（已答，2026-11-18）

**`9f6b1700d`：a generator's state slot is `T?` and starts null, as ruled 11-16, so a Ref needs no unwritten form.**
⇒ 本页第 4 节的默认（「先量语料有没有程序跨 pull 存活，若没有就加诊断拒掉」）**作废**：
作者说那个形态本来就该能跑，**不要加诊断**。
⇒ 而该裁定在树上**没有落地**，见 `2026-11-18-the-state-slot-is-a-ruling-with-no-code.cn.md`；
再往下是 GIR 层，见 `2026-11-18-lowering-generator-state-to-gir.cn.md`。
