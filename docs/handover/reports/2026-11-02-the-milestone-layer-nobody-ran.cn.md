# milestone 那一层，今夜之前没有人跑过

**2026-11-02 晚间。给作者读的一页，不是问题页。每条都有读数或原文，读数带 tip。**

## 七件

1. **milestone 这一层今夜之前从没跑过。** `-Plan test/plans -Tier milestone` 是 **27 个计划文件、3390 量级**。三个 tip 给出三份不同的红清单：`2c1a7b6c4` 上 `3302/3286/16`、`gs6` 旧基点 `3397/3373/21`、`remat` 树 `3395/3367/27`。⇒ **一个读数不带 tip 就不是读数**，这一层尤其如此，因为它的红清单每天都在变。
2. **两条 GC 自检"生来就是红的"。** `age_` 是 4 位、封顶 15，而阈值写成 16 ⇒ **升代永不发生**；另一条只跑 8 次 `minorGC()` 而阈值 15。两条都不会报错，只会安静地不测。**已修**（`5301360dd`、`2c1a7b6c4`），并加了"阈值不可达就抛"的守卫。⇒ **一条测不到的测试，比一条红的测试更贵**。
3. **`known_fail` 在没写 `backends` 的条目上悄悄失效。** `expandBackends` 第一行就 `return [test]`，标记根本没被读。改 runner 让两种写法都生效，再全树扫：**6 条标记全部生效，没有一条落在缺陷形状里**（`a01fcdfdc`）⇒ **它不是"有些条目失灵"，是"这种写法谁都还没用过"**。
4. **"零条用例启动"检查落地。** **309 个计划里 31 个是沉默的零**；当天就撞上一个真缺陷（`feat/trans/cml_sweep.plan.toml` 缺计划级 `tier`，自称快层而唯一一条是 `slow`）⇒ **"跑完什么都没说"从此报红**（`006adafb0`）。
5. **`poison` 扫描器配上 `std::fvm` 抓到真 bug。** `nn:*_bytecode` 两条 `exit 134`，原文 `a raw pointer was kept across a safepoint; see docs/technical/36_gc_handle_rules.md`。同一族在 `remat.split` 的崩溃里也出现（`TupleType::typeAt` 越界，Stale `Type*`）⇒ **这不是两条红，是一族纪律问题**（在查）。
6. **Linux 上"崩溃没有栈"是我们自己造成的。** `src/utils/assert.cpp` 的 `#else` 分支没实现，**只打印 `(not available on this platform)`**——那句话把"这段代码没写"说成了"这个平台没有"。且 88 的 GCC 工具链没有 `stdc++exp` 后端 ⇒ 改走 `backtrace(3)` + `-rdynamic`；而 `-rdynamic` 单独一项就会改变模块加载行为（`dlsym` 的搜索范围包含全局作用域）⇒ **仪器只在诊断构建里开，默认构建不动**。
7. **扫描：`Type::Float64()` 有 132 行 / 30 文件，只有 8 处能证明今天会出错**（其余 83 处合法、6 处默认值、12 处测试、3 处其它、20 处"待证"不许当缺陷报）⇒ **"看起来像缺陷的形状"与"缺陷的定义"是两件事**。

## 给作者的三个数字

**今夜修绿 5 条**（两条 GC 自检、三条 `stream.tee_report`）、**定性 2 条**（`nn:*_bytecode` 与 `remat.split` 同族：裸指针跨 safepoint）、**工具改动 3 件**（`known_fail` 生效、零条用例检查、`-StackTrace` 诊断构建）。

## 这一层的账怎么记

`develop` 的每轮闸门是 `-Plan all -Tier fast`，它按 `tier` 跳过 milestone 条目；`scripts/plan-inventory.mjs:121` 又把 `jit_framed` 登记进 `FORBIDDEN`，那句理由写的是"its 2 milestone tests are GC stress"。⇒ **为 2 条跑不动的条目，把同一文件另外 55 条也划走了**，而这一层没有人跑。**报 `[SUMMARY]` 必须带 `-Tier`：一个绿只有在说清盖住哪一层之后才是绿。**