# 一个装着 Ref 的格子，计划期算持有者、运行期不算句柄

**日期**：2026-11-18
**提出者**：协调侧
**状态**：待作者裁定（本页写死默认建议，并按默认当轮开工）
**触发**：`lane-refstep` 修好「Ref 交进生成器之后的早释放」之后，报出一处同族缺口

## 1. 事实（读数，不是推理）

`lane-refstep` 的读数（`--gc-check foreign`，临时仪表已撤）：

```
`REFROW main pos 148 node 142 give value 141 … last 1`
value 141 = `state::ref` 造出的 box（`Ref<int>`），它唯一的用户是 `main` 里那个 Func 节点
⇒ 那一行的 give-back 在「把 box 交给生成器之后、第一次 pull 之前」把计数归零
⇒ 已在 `3c460b37a` 修（`packsItsInputs`：被调返回类型是 `Stream`/`Function` 时打包入参）
```

而**同一族的另一半仍然开着**，作者给的最小例：

```
let box = state::ref(0);
state::set(box, 5);
yield state::get(box)
```

`lane-refstep` 的原话：

> 「计划把『写进 cell』当作 box 的最后一次使用，而 cell 本身不是计划计数的值
> （`var Ref<int>` 的 code 与 `Ref<int>` 相同，`valueIsCounted` 认了它，运行时 `countedBlockOf`
> 又不是句柄，行的效果是**两边都不作数**）。」

## 2. 为什么它现在两边都不作数

```
计划期：`valueIsCounted(TypeCode)` 看 TypeCode ⇒ `var Ref<int>` 与 `Ref<int>` 同码 ⇒ **认它是计数句柄**
运行期：`countedBlockOf(...)` 看这个值是不是句柄 ⇒ **不认**
⇒ 于是那一行既不被计划依赖（计划以为它已经由 cell 持有），也不被运行检查（运行以为它不归它管）
⇒ **不是漏了一处判断，是两处判断各说各话而没人发现它们不一致。**
```

## 3. 要裁的是什么

**一个格子里装着一个 Ref，谁是持有者？格子死的时候，谁还引用着那个 box？**

三条路，各自要付什么：

| 路 | 做法 | 代价 |
|---|---|---|
| **A（默认建议）** | **格子算持有者**：`let` 一个装 Ref 的格子，就是那个 box 的一次持有；格子死 ⇒ 送出 give-back | 要定「格子死在哪一点」（作用域退出？最后一次读？），而这个点今晚已经被证明容易读错（`armend` 的出口 GATE、`refstep` 的 pull 时机） |
| B | **格子不算持有者**：`var Ref<int>` 与 `Ref<int>` 的 `valueIsCounted` 必须**分开判** | 改一处判断即可，但会立刻把「写进格子的 Ref」暴露成**早释放**，可能新增红 |
| C | 别裁：把「格子装句柄」写成语言禁令（诊断） | 改语言，不再是一家的事 |

## 4. 默认建议（按此当轮开工）

**走路 B 的前半：先让两处判断一致，再决定谁持有。**

```
① **先只改判断**：`valueIsCounted` 对「装句柄的格子类型」与「句柄本身」给出同一个答案
   ⇒ 取哪一个都可以，**但必须一致**，并在报告里写明取的是哪一个、为什么
② **改完立刻量**：`var_new`（同人口）、`feat/linear`、六计划
③ **若走 B 之后冒出新增红** ⇒ **那就是答案：格子必须算持有者（路 A）**，停下来按 A 做，别硬推
⇒ 理由：**「两边不作数」比「两边都作数」更坏** —— 后者最多多留一个东西，
   前者会让一个东西在**两个阶段都无人负责**时安静地消失。
```

**若作者认为该直接走路 A 或 C，请改第 4 节并把 ① ② ③ 作废。**

## 5. 相关页

- `docs/handover/reports/` 里 `lane-refstep` 的同族缺口记录（本页第 1 节的出处）
- `docs/handover/questions/2026-11-18-generator-halt-has-no-reader.cn.md` —— **已由作者答「drop the generator's halt bit」并落地**（`6ce3bf1f2` 裁定、`647b4a31a` 合入）## 裁定（已答，2026-11-18）

**`9d29939c4`：a cell holding a Ref is a holder, and the cell should go.**
⇒ 已落地一半（`lane-refcell`，`9d589d2f3`）：跨闭包读/写的 `Ref` 局部不再是格子。
⇒ 未落地的一半与后续见 `2026-11-18-lowering-generator-state-to-gir.cn.md`。
⇒ **本页状态已不是「待裁定」，保留原状以便回看当时的问法。**
