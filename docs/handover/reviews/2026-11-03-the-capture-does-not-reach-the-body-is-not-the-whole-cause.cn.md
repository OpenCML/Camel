# 捕获"没进循环输入"不是全部原因：两条通道都实现了，错数一个也没动

## 问什么

作者 2026-11-03 22:50 给的根因是：抬升后的循环体直接读区 0 的 `Port`，而那个 `Port` 不在循环的输入里，于是体在自己的帧里按同一个槽号读到别的东西。**这条根因已经按两种通道各实现了一遍：**

- **通道甲：捕获当成循环的 norm 输入** —— 运行期**拒绝**（见 §1，错误原文在那里）。
- **通道乙：捕获当成 state 分量** —— 图建起来、跑通、两半都进去（`captures=2`，值类型 `((float, float), float, float)`），**没有任何运行期错误**，读数仍是 12/2/1。

**那么下一步该查哪一层？** 这是这一页唯一的问。

## 1 通道甲被运行期契约挡回（原文）

按运行期契约，循环体的端口个数就是 `element + k`（state 的分量数），捕获没有位置：`src/runtime/loop.cpp#LoopDraftView::initialState` 的 `initialState(j) = normInputs[j + iterates]` 把 iterable 之后**每一个** norm 输入都读成一个 state。所以把捕获追加成 norm 输入，等于给循环多造了几个 state：

```
error[RuntimeDiag::PassInternalError]: Pass 'std::nvm' failed with an internal error:
Runtime graph 'jvp_of_f' has LOOP node 14: its body graph returns a '(float, float)',
the state is '((float, float), float, float, float, (float, float))'.
```

对比 `include/camel/runtime/exec_plan.h#ExecLoop`：`slots` 的排布是"state 各分量、cond 端口、**体的端口：先 element 再 k 个**"。

## 2 通道乙通过运行期，读数没动

捕获的两半（值 `c`、切向量 `dc`）各占一个 state 分量，体里各得一个 port，原来读区 0 的两处改读自己的 port，每轮原样交回；循环值类型相应从 `(float, float)` 长成三分量。建起来、跑起来、无错误：

```
PROBE loop f=f captures=2 type=((float, float), float, float)
A v=  12.0   A d1= 2.0   A d2= 1.0        （纸面 16 / 4 / 5）
```

## 3 对照全对：只有"体内读外层值"这一种形状错

| 程序 | 形状 | 纸面 | 实测（通道乙之后） |
|---|---|---|---|
| A | 体里直接读捕获的标量 | 16 / 4 / 5 | **12 / 2 / 1** ✗ |
| B | 体里读捕获结构体的成员投影 | 16 / 4 / 5 / 12 | 16 / 4 / 5 / 12 ✓ |
| `g` | 把捕获换成常量（`x * 2.0 + k`） | 16 / 4 | 16 / 4 ✓ |
| `h` | `let d = c;` 再读 `d` | 16 / 4 | 12 / 2 ✗ |

`g` 与 B 在所有版本里都正确，A 与 `h` 在所有版本里都错，**换了两遍捕获的到达方式，错数一个也没动**。

## 4 已排除的四条

1. **体内加一份副本**（`Copy`，norm 指体外那份）—— 四行读数不动（第一次的 region 判据写错、等于空跑，改成按新节点判之后重量一遍，两次一样）；
2. **捕获当归一输入** —— 运行期拒绝，错误原文见 §1；
3. **捕获进 state（值 + 切向量两半）** —— 建起来、跑通、`captures=2`、四个分量、无错误，读数仍 12/2/1；
4. **喂回的状态是不是原来那份** —— 捕获程序与常量对照的循环节点输入行**逐字相同**（`norm`、`with`、初始对、体结果全部同形，只有节点号与 `di` 平移）。

## 5 我们的默认：下一步查规则调用的展开与循环结果对的装配

值的半边现在由状态端口每轮给（它每轮回来是对的），切线的半边由**规则调用**给：`x * c` 的切线节点是 `expandRuleRightSide` 展开出来的（`modules/transforms/rule_expand.*`），它的操作数是 `(x, y, dx, dy)` 四个端口。A 的 `v` 也错（不只是 `d`），所以默认先查这两处：

- **规则调用的展开**：`modules/transforms/rule_expand.*` —— 展开出的节点落在哪个区域、它的操作数拿到的是哪一份；
- **循环结果对的装配**：`jvp.cpp` 里把体结果装成 `(值, 切线)` 的那两处（`liftedPair` / `liftedLoopBodyResult`）。

理由一句话：**同形状下 Data 对、Port 错**这一点已经由 `g` 立住，而"Port 怎么进体"换了两种写法都不动读数 —— 那下一个嫌疑人就是**读到的两份东西怎么被用**，不是它们怎么进来。

## 6 相关

- 读数、整图 dump、四条读数拼不出机制的自述：`2026-11-03-a-captured-scalar-inside-a-loop-body-is-silently-wrong.cn.md`
- 循环状态不是浮点标量那一路：`2026-11-03-reversing-a-backward-can-only-be-pinned-when-a-loop-state-is-not-a-float.cn.md`