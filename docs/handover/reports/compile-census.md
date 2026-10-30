# 编译普查：谁能编译、谁只是没人看

**一句话**：`perf` 不在 `-Tier fast` 里，所以 `test/cases/perf/transformer_step.cml` 坏在主干上很久没人知道；这条普查先回答「到底有多少个这样的」，再说它能不能当闸门。

**判据**：**fast 层之外的用例不是「慢」，是「没人看」。一个能编译但从不运行的用例，与不存在没有区别——而它比不存在的更糟，因为它让人以为覆盖了。**

## 读数一：裸编译一遍（3169 个文件）

对 `test/cases/` 下全部 `*.cml` 跑 `camel <file> std::gir`（编译后立刻返回空图，只计编译），12 路并行：

```
files=3169 ok=2102 fail=1067
```

- 其中 **203** 条按文件名就是「故意编译失败」的用例（`err_*` / `*refuse*`）；
- 其余 **864** 条里最大的一类不是缺陷，而是**我的跑法缺东西**：`SemanticDiag::ModuleNotFound` **193** 条（兄弟目录里的模块要 `--module-root`，只有计划文件知道），其后 `NoMatchingFunction` 136、`ArgumentsRejected` 44、`LiteralNotRepresentable` 36、`UnresolvedReference` 32……

**结论一：裸跑 `std::gir` 不能当闸门。** 它缺计划文件给的 `--module-root`、环境变量与参数，所以 1067 条里绝大多数是假阳性。要做成闸门，必须**按每个计划条目自己的 args 跑**，而不是按文件裸跑。

## 读数二：与计划期望交叉（1824 条「应当能过」）

把 21 个 `perf` 计划与全部计划一起解析，取「期望成功」的条目（无 `expect_exit = 1`、无 `error[` 期望）：**1824 条**。其中裸跑失败的 **124** 条，抽样全部是 `ModuleNotFound`（模块根由计划给），也就是说**这 124 条不是坏的，是我的跑法不完整**。

## 读数三：坏掉的那一个（真的）

```
[FAIL] training_steps.*                                          （4 / 6）
[INFO] expected exit 0, got 1
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: ...
   --> test/cases/perf/transformer_step.cml:90:28
```

`test/plans/perf/training_steps.plan.toml` **今天在主干上是红的**：6 条里 4 条失败，`transformer_step.cml:90` 的 `adam(...)` 找不到匹配。它没被 `-Tier fast` 覆盖，所以闸门一直是绿的。

**顺带一个读数限制**：这台 Windows 主机不报 CPU 时间，`perf` 层几乎每一条都因此失败（`no reading cpu of N s budget`），所以**perf 层的红在这台机器上不能当作「用例坏了」的证据**——但 `training_steps` 的红是编译错误，不受这条限制。

## 结论二：闸门该长什么样（未做，需裁定）

三条路，代价递增：

1. **只把「纯编译 + 纯运行」的计划（如 `training_steps`）挪进 fast 层**——最便宜，覆盖最小；
2. **写一个 fast 层用例：遍历所有计划条目，按条目自己的 args 只跑编译**——真覆盖，但要复用 harness 的参数/环境解析，约等于在 `scripts/test.js` 里加一个 mode；
3. **按计划运行 `-Tier slow` 的子集**——最接近真实，代价也最大。

**今晚只交读数，不接闸门**：读数二的 124 条说明裸跑这条路不可用，接上去会立刻一片假红。