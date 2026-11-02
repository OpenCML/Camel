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
## 第三种实例：`hint 2 → peak 20` 在 poison 下不再出现

`poison:` 变体的 `stream.tee_report` 报 `stdout does not contain 'STREAM_TEE {"tee":1,…,"elements":20,"peak":20,…,"hint":2,…}'`。用工具本来的入口直跑（`ssh 88zjwei`，`LD_LIBRARY_PATH` 取 `camel-remote.ps1:162` 的默认值，否则二进制报 `GLIBCXX_3.4.36 not found`——**空输出有两种：用例什么都没做，或仪器根本没起来**）：

```
STREAM_TEE {"tee":1,…,"elements":1,"peak":1,…,"hint":2,"cap":64}
STREAM_TEE {"tee":2,…,"elements":20,"peak":20,…,"hint":2,"cap":64}
STREAM_TEE {"tee":3,…,"elements":1,"peak":1,…,"hint":64,"cap":64}
STREAM_TEE {"tee":4,…,"elements":3,"peak":3,…,"hint":64,"cap":64}
```

六条、hint `2,2,64,64,…`；期望写的是三条、hint `2,64,8`。**报告打了，而且打得更细——过期的是"第一个 tee 的侧先把 20 个元素读完"这个交错前提，而交错不是契约。**

三个实例现在是同一族：`16` vs `15`（永不晋升）、`8` vs `15`（没晋升就要求稳定地址）、`hint 2 → peak 20`（poison 下不出现的交错）。**共同点：测试在测另一个世界。**

## 为什么把不变量交给 runner，而不是让用例自己校验

**A**：用例自己校验自己的输出，等于让被测者当裁判。**B**：给 `run-tests.mjs` 加 `expect_stdout_matches`（正则），加的是表达能力，受益的是所有"每条都必须满足 P"的用例，不只这一条。今天先用 `STREAM_TEE {` 与 `"cap":64` 两条字面断言止住红——它仍能抓到"报告没打"和"cap 写错"，**抓不到 `peak <= cap`，那一条等正则落地再补**。
## 一族两小类，名与判据

- **甲类「测试在测另一个世界」**：期望过期。三个实例：`16` vs `15`（阈值到不了，永不晋升）、`8` vs `15`（没晋升就要求稳定地址）、`hint 2 → peak 20`（poison 下不再出现的交错）。**修法：改期望。**
- **乙类「两个各自通过的功能第一次同时出现」**：`poison:modules.nn:gru_lite_large_bytecode` 与 `…residual_mlp_large_bytecode`。**"同时出现"只是发现方式**：直跑 `--gc-check poison` 后 stderr 是
  `[camel] GC poison: a pointer to a Camel object was used after a safepoint moved the object (a raw pointer was kept across a safepoint; see docs/technical/36_gc_handle_rules.md)`
  ⇒ **FastVM 那条执行路径把裸指针跨过了一个 safepoint**。**修法：改代码。**

**判据：甲类改期望、乙类改代码，混在一起会让人去改期望。**

## 扫描器替我们做组合覆盖，而它从没被跑过

`poison:` 变体不是谁写的，是 `gc_poison_sweep` 从计划条目自动派生出来的（`skip_args_prefix = "--gc-"`，`test/plans/feat/mm/gc_poison_sweep.plan.toml:38`）——**我们有一个机制在替我们做组合覆盖，而这个机制所在的那一层（milestone）从没被跑过。它第一次跑就抓到一个真 bug，而且那个 bug 是 poison 检查自己报的案，不是我们猜出来的。**

**"不是我的线"不等于"没人管"**：把发现交出去时，要把它交到具体人手上——这条报给了协调人，由它派给 `guard`，闭环才成立；写在页里不算交付。