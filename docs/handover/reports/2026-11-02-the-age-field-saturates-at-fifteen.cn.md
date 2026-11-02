# age 字段封顶 15，而阈值写的是 16

**2026-11-02，`lane-m4b`。一条从出生起就没跑过的自检，和一行常量。**

## 读者先看这一行

```
include/camel/core/mm/alloc/header.h:72   void incAge() { if (age_ < 15) age_++; }
                                          void setAge(size_t age) { GUARD(age <= 15, "Age must be between 0 and 15"); }
src/core/mm/gc_debug.cpp:36               promotionAgeThreshold_ = 16;
```

`age_` 是 4 位、封顶 15；阈值是 16 ⇒ **任何对象都不可能晋升**。不是复制路径没走到，是走到了、加龄了，但加不上去。

## 怎么到的这里（三次打点，两轮排除）

1. `gc.remembered_set_minor_copying` 报 `GC remembered-set self-test failed to promote owner object`；`-S` 指到 `206e40a94 feat: add GC write barriers` —— **自检与它测的功能同一笔落地，而 milestone 层从没人跑过**。所以这条红不构成"某一天改坏了"的证据。
2. 第一次打点：`i=4 age=5 elder=0 … i=14 age=15 elder=0, i=15 age=15`，阈值 16 ⇒ **age 停在 15**。
3. 第二次打点加 `large`：`elder=0 large=0` ⇒ 对象一直在 young/survivor。
4. 第三次打点加 `moved`：**第 16 轮 `moved=1`、age 仍是 15** ⇒ 按事先写死的三支判据，落在"走了复制路径却没加龄"，即计数错。
5. 但"先判 `age>=thr` 再 `incAge`"这个最省事的假设**被代码次序排除**（`gen.cpp:1259` 的 `incAge()` 在 `:1260` 的 `age()` 之前，`:1290` 才是判定）。
6. 枚举搬动路径：`incAge()` 全树只有一处调用、搬动路径只有一条 ⇒ **"另一条没加龄的路径"不存在**，方向要重找。重找的结果就是本文第一行：**加龄确实发生了，但它封顶在 15。**

## 修法（本提交）

- **把"永远不会发生"从沉默变成响**：构造 `GenerationalAllocatorWithGC` 时校验 `promotionAgeThreshold_ > 15` 就抛错，理由写在注释里：一个 age 字段到不了的阈值会静默地关掉晋升。
- **让自检的阈值取字段能到达的值（15）**，于是晋升路径真的被走一遍：`gc.remembered_set_minor_copying` **转绿**（`gc.plan.toml` `45 条 / 44 绿 / 1 红`，剩下的是 `gc.foreign_resource_lifetime`，另一件）。

## 留给作者的问句（按默认先做，不等回复）

**默认晋升阈值该是几？`age` 该不该加宽到 5 位以上？**
**默认建议**：阈值取 `age_` 能到达的值（15 或更小），**`age` 不加宽**——晋升只需要"够老"，不需要"记住有多老"；加宽字段是另一件事，且会改对象头大小。

## 形状

这条 bug 不靠读代码发现，靠的是让代码自己说：**三次打点、两轮排除，最后是 `16` 与 `15` 这两个数打架。** 排除"自检写错"这件事是打点做的，不是推断的。
## 第二条自检：同一个形状，另一个数

`gc.foreign_resource_lifetime` 报 `foreign pinned handle requires an object in a non-moving allocation region`。判定它只用读两处：

- `isObjectAddressStable`（`gen.cpp:143`）只在 `inElderGenSpace(header) || inLargeObjSpace(header)` 时为真；
- 那条自检在 pin 之前只跑 **8 次** `minorGC()`（`foreign_handle.cpp`，自检体），而晋升阈值是 **15** ⇒ 对象还在 young，**pin 的契约没问题，是"8 次就够"这个假设过期了**。

所以这一条不是"分配路径错"，是**期望过期**：那个 8 与阈值 15 不是同一套数。改法是把"固定 8 次"换成"收集到 `isObjectAddressStable` 为真，最多 64 次"——**上界是给坏掉的晋升路径留的响**，不是给循环兜底。

`gc.plan.toml` 至此 **45 条 / 45 绿**，layer 里那两条"出生即红"的自检第一次真的跑过并通过。