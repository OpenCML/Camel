# 改写家族的另外两条：`extract` 声明什么，闭包捕获的类从哪来？

**本页问两件事**，都是 `rewrite` 家族在补完 `saturate` / `check` 之后剩下的红。

## 一、`rewrite:extract` 要不要同样声明 `readsGraph`

补完 `saturate` 与 `check` 之后，`opt_saturate_rhs_from_an_enclosing_region` 仍红，**卡点换成了它**：

```
node 15 of graph 'main' (rewrite:extract), in a graph whose type declares uses graph, var stdout,
  makes an access no clause can name (write unknown): 'rewrite:extract' declares no resources...
```

**要作者定的**：它是不是与 `saturate` 同形 —— **图是不可变的值、交回新函数值、不写旧图、规则集是 `Rule[]` 值**？

**默认建议（没回复就按这个开工）**：**三问同形就补 `readsGraph`，任一同形不成立就写进本页回作者。** 三问是：
① 它写不写任何东西（有没有 `AccessMode::Write`、有没有落回旧图）；
② 它交回的是不是新函数值；
③ 它有没有从外部拿非值的东西（随机源、文件、环境变量）—— 有就**先报，可能是缺陷**，按 `check` 那条的规矩（`check` 的种子是 `with` 参数 `seed` 经 `sampleStreamOf` 派生，全模块无 `random_device`、无时间播种）。

## 二、闭包捕获被读成 `anyValue` 是不是该特判

`ref.foreach_gives_back.{nvm,fvm,jit,prl}` 四条红，原文：

```
Pass 'test::plan_refs' returned a program whose call site reaches past the type it declares:
  node 1 of graph '__root__' (a call) declares uses var stdout; the graph it runs touches any unknown;
  no clause can name what node 5 of graph 'main' (:mark/foreach_arr) reaches:
  its declaration names no resource the analysis can read
```

`:mark/foreach_arr` 的声明**已经就是作者说的形状**（`builtin_semantics.cpp:599-603`，`functionOperand`，与 `map`/`filter` 同一行）。红的原因在别处：

```
src/runtime/resources.cpp:744-748 —— 对 signature.captures 一律记 anyValue + declared=false
```

理由（代码注释原文）：「A directly called graph has no capture slot among the node's inputs: the class is not known here, so the conservative reading is the only sound one」。**签名本身说不出是哪个资源**：`ResourceSignature::captures` 只有位置（`std::vector<uint8_t>`，`include/camel/core/resource_semantics.h:245-246`），没有类。

**没人把它当正向期望钉住**：`test/` 里 `any unknown` 只出现在 `test/plans/feat/opt/commit_checks.plan.toml:102` 的 `assert(unknown <= 3)`，**上界**。「放不下要拒绝」由 `err.wide_parameters` / `err.many_parameters_pure` 钉住，但走的是形参下标。

**特判的方向**：`Unknown`（拒绝）→ 有名类/绑定值（放行），**只可能少拒绝、不会多拒绝**。风险只在「同一节点读数变了」的精确输出用例：
- `test/plans/feat/compile/ref.plan.toml` 钉的 `PLAN_REF_*` 行与 `created/counted/released` 计数（若绑定到 `r` 的具体值，可能新增一条对 `r` 的访问、加顺序边、改 give-back 行）；
- `docs.tutorial_images` 的 DOT dump；
- `commit_checks` 的 `unknown <= 3`（上界，安全）。

**以上未实测。**

**要作者定的**：捕获槽的**类**该从哪来 —— 从闭包类型的捕获声明读，还是需要签名带上类（那是 `ResourceSignature` 的形状改动）？

**默认建议（没回复就按这个开工）**：**不动 `fromSignature`，把这 4 条记 `known_fail` 并写明结束条件「当闭包的捕获槽能说出它读的是哪个资源时」**。理由：这是**类型系统的形状问题**（签名没有类这个位），改它不是修一个读法，是改签名的定义 —— 属于原则问题，归决策者。