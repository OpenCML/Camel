# 构建期怎么知道一张图已经是提升的结果

**日期**：2026-11-21 13:40（+08:00）
**来源**：`jvp-closure` 车道在落 `32bcee3a0` 时，五道墙全部撞过，最后停在一条**图级**信息缺口上。

## 一、结论一句话

**两次提升之间共享的是「图」，不是「边」。** 配对版一旦作为**函数值**被上层 `jvp` 拿到，`jvp` 只会问「这张图的提升是什么」，而**没有「这张图已经是某个提升的结果」这一信息可用** —— `liftedFrom` 只在运行期（`GCGraph`）上可读，构建期手里的 `GraphRead` 没有等价读数。

## 二、读到的形状（全部有读数）

`jvp_vjp_f` 的六行：

```
liftGraph name=__g_359__                        source=359 member=363 paired=0
liftFuncValue caller=__g_359__ ref=2 calleeId=361 calleeName=vjp::vjp<…>
liftGraph name=vjp::vjp<(x: f32) => f32 pure>   source=361 member=364 paired=0
liftGraph name=__g_362__                        source=362 member=365 paired=1
liftGraph name=sq                               source=275 member=366 paired=0
liftGraph name=jvp_of___g_362__                 source=365 member=380 paired=0
```

**第 6 行是问题：`source=365` 就是第 4 行刚造出来的配对版。** 而 `liftGraph` 的图名是 `"jvp_of_" + callee->name()`，所以第 6 行说明 `calleeIdOf(ref)` 返回的是 **365**，即那条边指向的是「一张图」—— 而这张图是本趟刚造的。

随后运行期报：

```
Runtime indirect call norm-arity mismatch: caller graph 'jvp_of___g_359__' node 6 has 2 args,
target graph 'jvp_of_jvp_of___g_362__' has 4 norm ports.
```

## 三、五道墙各自撞在哪（排除记录）

| # | 改动 | 层 | 结果 |
|---|---|---|---|
| 1 | Fill 成员一律配对（`liftedValueOf`） | 边 | 同一报错，**逐字节相同** |
| 2 | Fill 成员仅**数值**配对、函数型走普通提升 | 边 | 逐字节相同 |
| 3 | `liftedValueOf` 函数分支加**双向**判据（copy 类型已是 `Lift<F>` 就用它） | 节点 | 逐字节相同 |
| 4 | `liftFuncValue` 复用 `state.callees` **值集合**里本趟已造的图（不靠名字前缀） | 本 pass 表 | 逐字节相同 |
| 5 | 代码格 wiring 选中配对变体 | **图级** | **造出 365，唯一正读数** |

**四份「逐字节相同」排除的不是三处嫌疑，是「边级与节点级这一整片区域」。**

**而第 4 条的排除方式值得记**：若第二次提升发生在**同一个** `LiftState` 里，值集合一定命中（365 必然在表里）。**它没命中 ⇒ 第二次提升发生在另一次 `buildJvpGraph` 调用里，表是空的。** 一条反证就把「本趟记账失误」这个方向关掉。

## 四、已经做对的、和仍然没有读数的

**做对的**：
- **配对变体确实被选中并造出来了**（`paired=1` → `member=365`）—— 「捕获也变」这条路**是通的**；
- **`pairedCallee` 本来就是边级**（按 `ref` 判），缓存本来就按 `(calleeId << 1) | paired` 两格存 —— **这两件事不需要改**；
- **`fill=3` 的闭包布局由 `clone` 自模板继承**（`nodevm.cpp:528-553`），所以**只需替换代码常量，`fill=3` 一处不改**；
- 新算子 `transforms:lift_paired_closure` **用户写不出来**（8 处全在 `jvp.cpp` / `jvp_operator.cpp`，六个用户面目录零命中，无 `declareCallForm`）。

**仍然没有读数的**：**新算子至今**一次都没进过判据区间** —— 既没被证明有效，也没被证伪。** 因为失败发生在它之后（`node 6` 的调用边上）。

## 五、要裁的那一件事

**构建期怎么让 `jvp` 知道「这张图已经是某个提升的结果」？**

三条候选，各有代价：

- **甲：给 draft 一级加一个「我是谁造出来的」标记**（等价于运行期 `liftedFrom`）。**代价**：那是新的图级 API，与 `func.h:14-16`（布局是关于图的事实）同向 —— 但这个标记是「**来历**」，不是「布局」。
- **乙：配对版的图名或注册表承载这个来历**，`jvp` 查它。**代价**：靠名字就是字符串猜测（本格已明令禁止）；靠注册表则是第二份真相。
- **丙：让「提升一个已经提升过的图」成为**幂等**的**（第二次提升返回原图）。**代价**：它会把「有意套两层 `jvp`」（`jvp∘jvp` 是合法写法）与「误把提升版再提升」**混为一谈** —— 而这两件事在 `nested-transforms` 里必须分得开。

## 默认建议（写死，本轮按它开工）

**走甲：给 draft 一级加一个「这张 draft 是提升自哪张源图」的只读标记。**

理由：

1. **它是**来历**，不是**布局**，所以不与 `func.h:14-16` 冲突**（那条讲的是「捕获的名字与布局属于图」；而「这张图是从哪张图提升来的」是**血缘**，本来就是图级的）。
2. **它让判据变成双向无缺口**：有来历 ⇒ 不再提升；无来历 ⇒ 提升。**没有第三分支。**
3. **它不碰 `jvp∘jvp` 的合法性**：丙做不到这一点（幂等会把「有意套两层」吃掉），而甲只在**同一张图**的血缘上判，不判「套了几层」。

**执行口径**：
- 标记只由构造提升图的那些地方写（`group.reserve("jvp_of_" + …)` 与 `transforms:lift_transpose` / `transforms:lift_paired_closure` 的内核）；
- `liftFuncValue` 与 `liftGraph` 在入口读它：有 ⇒ 复用、不再提升；无 ⇒ 提升；
- **判据**：`jvp_of_jvp_of_…` 零命中（报搜索范围）；`jvp_vjp_f` → `1.0 / 2.0 / 3.0`；`grad(grad(x⁴))(2) = 48`；`4296/4291/0/5`；`36/9/0/27`；`floatgate.f32_closure_member_ports` 与 `residuals_in_the_closure` 逐字不变。

**若走甲时发现「有意套两层 `jvp`」会被误判成「误再提升」⇒ 停手报读数。** 那时需要的是「**按层数**记血缘」（不止一张父图），那是另一页。

**另一条本文要记的教训**：**「按用途，不按图」这个提法是对的，但它可能是**已经实现**的。** 本格在第三次裁定前应该先 `git grep` 那个判据在不在 —— 若在，就说明阻塞在**下一层**，继续往下一层问，而不是再裁一次同一个层级。

## 六、落第 2 条时的实测：两次提升分属两趟

在 `liftGraph` 入口打印 `state.callees.size()` 与「`source` ∈ 值集合」，一次运行五行：

```
SCOPEDUMP name=__g_359__                        source=359 size=0 inValues=false
SCOPEDUMP name=vjp::vjp<(x: f32) => f32 pure>   source=361 size=1 inValues=false
SCOPEDUMP name=__g_362__                        source=362 size=0 inValues=false
SCOPEDUMP name=sq                               source=275 size=0 inValues=false
SCOPEDUMP name=jvp_of___g_362__                 source=365 size=0 inValues=false
```

**第五行 `size=0`** —— 提升 `jvp_of___g_362__`（`source=365`）的那一刻，本趟表里**一个登记都没有**。

**⇒ 这是另一次 `buildJvpGraph`（新的 `LiftState`），不是同一趟里漏登记。**

**⇒ 所以「本趟映射」（第 1 条答的第 1、2 条）在**结构上不到达**那一处。** 因为它以「一趟走图」为界，而两次提升分属两趟。

**又一条读数**：`__g_362__` 自己那次（`source=362`）**也是 `size=0`**，且这次运行里**没有出现 `paired=1`** 的那次 lift。两轮拼起来：**配对变体是在**别的**那趟里造的，进而在**新的一趟**里被当成普通图再提升。**

## 七、第四个问题（本页的页尾）

**谁发起了第二次 `buildJvpGraph`（输入是那张刚造的配对版），而它在「一趟走图」的模型里算不算同一趟？**

- **若算同一趟（只是 `LiftState` 被重建了）⇒ 那是 `LiftState` 的生命周期问题**，修法在「谁负责持有它」，不在判据。
- **若不算同一趟（是外层另一次 `jvp` 变换合法地开始了）⇒ 那 `jvp∘jvp` 本来就该再提升一次**，而错的就变成了「**那张配对版不该被当成普通源图交出去**」—— 即 Fill 的规则产出**不该让外层看见它是一张可再提升的图**。

**两种修法完全不同，而它们的分岔就在「趟」这个字的定义上。**

**顺带一条：`LiftState::callees`（`jvp.cpp:438-450`，键 `(id<<1)|paired` → `GraphId`）**早已存在**，反向索引也一直可查 —— **不是缺结构，是结构的作用域不够。** 这是本格第三次发现「我裁的判据已经实现」：先是 `withClosureType` 被 `func.h:14-16` 否掉，再是「按用途不按图」已存在，再是这张表已存在。