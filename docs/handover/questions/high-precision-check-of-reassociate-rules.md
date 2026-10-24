# `reassociate` / `contract` 规则的采样比较：精度法与它留下的三处要裁定

Author: 车道 `handover/h439-hi-precision-check`（`D:\Projects\Camel-wt\h439-hi-precision-check`），2026-10-24。Status: open — 第 4 条那个"参考精度里还留不留许可给的容差"需要语言作者确认；第 2、3 条是本车道顺手量到的边界，落地时按我的读法走了。测量在 Linux 主机（`88zjwei`）上、分支 `handover/h439-hi-precision-check`（自 `origin/develop` 的 `ced544710`）完成，命令是 `camel-remote.ps1 -Plan test/plans/feat/modules/rules.plan.toml -Tier fast -Jobs 8`。

## 数字先

- 落地点：`modules/rewrite/module.cpp` 的 `propositionComparison()`（新函数，两个调用点各自改为它在命题为 `real equality` 且元素类型是 f32/f64 时接管比较）。参考值一边在 long double 里求（`realValueOf`），比较也在那里做；不再拿两个元素类型结果互比。
- 红前逐字读数（同一台机器、`check_library.cml`）：`refuted tensor_f32_combine_mixed: … the rewritten left side differs at (tensor<f32> [2, 2, 2] {-0.5, 76.488, -79.654, 1e+30, -349.024, 1e-30, -669.542, 1e-40}, 48, -47): tensor<f32> [2, 2, 2] {…, 1.0000041e+30, …} and tensor<f32> [2, 2, 2] {…, 1e+30, …} (bound 11 ulps, 0 relative, scale 2401)`。
- 那条式子的两边用今天的方式直算（`test/cases/modules/rules/check_real_equality.cml` 第一行）：`at x = 1e+30 1.0000041e+30 1e+30` —— 两个 f32 结果差 27 个结果 ulp，参考精度里两边都是 `1e+30`。这才是裁定说的"右边更准"。
- 红后：`sampled tensor_f32_combine_mixed: proposition real equality + IEEE on special values, evidence samples 200, reason the condition selects the instances, so no sampled value was compared, the condition selects the instances, fired 26 of 64`；`test/plans/feat/modules/rules.plan.toml` 136 项里除本车道未动的老旧钉子外全绿，库里 43 条规则没有一条 `refuted`。
- `refine_accuracy` 那一类逐字未变：改前库里就有的三只钉子 `1.91128e-06` / `7.75714e-08` / `ratio 8.01648`（`check_accuracy.cml` 的反例）改后仍逐字命中；本车道再把 `accurate_log1p 0.193`、`float_log1p<f32> 0.201`、`float_log1p<float> 0.269`、`float_expm1<f32> 0.201`、`float_expm1<float> 0.233` 钉成逐字读数。命题不是 `real equality` 的行根本不进新路径。

## 我落地的方法（要确认的就是它）

1. 命题是 `real equality + IEEE on special values` 且元素类型是 f32/f64 时，两边各自在参考精度里求值，**比较在那里做**。
2. 特殊值那一半：某一边的原生值是 NaN/±inf、而参考值是有限值时（即原生值不是那个实数的舍入），按 IEEE 逐位比较两个原生值。读数：`refuted overflow_to_nan: … the two sides differ at (1.0000000150474662e+30): -nan and 0 (a special value is not the same bit for bit)`（该规则两边参考值都是 0，仍被拒）。h432 的 `float_combine_mixed<f16>` 发现（x=10000、c1=39、c2=-55 处左边 NaN、右边 -inf）在这个方法下仍然被拒。
3. 有符号零：没要 `no_signed_zeros` 时逐位。读数：`refuted zero_sign: … (-0 and 0 (a zero of the other sign is a different bit pattern))`；同一式加了 `no_signed_zeros` 就 `sampled`。
4. **保留的容差**：参考值之间仍用该规则自己的数值契约为容差（`bound.ulps` 个元素类型 ulp + `bound.relative`，加一项次正规绝对项）。理由：规则的**自身语法**就会在元素类型里舍入一次（`x * (c1 + c2)` 把 `c1 + c2` 折叠成元素类型常数），许可正是为这种换舍入发的；而抵消把两个元素类型结果拉开的量，现在根本不进比较。
5. 参考读不了这段片段（算子不在参考片段里）时回落到旧的原生比较，免得把检查自己的局限判成规则的红。

## 三处要裁定

- **(1) 参考精度里还留不留第 4 条那点容差？** 若改成"参考精度里严格相等"，库里有 4 条 tensor 规则（`tensor_f32_combine` / `tensor_f32_combine_one` / `tensor_f32_fold` / `tensor_f32_combine_mixed`）与 `verify_report.cml` 的 `fold` 会变红，而红的原因是规则自己把 `c1+c2` 折叠了一次：`fold` 那条的逐字读数是 `refuted fold … the rewritten left side differs at (79.65200000000006, 0.1, 2): 167.26920000000013 and 167.26920000000013 (the real values differ in the reference's precision)` —— 两个原生值印出来一模一样，参考值差约 6.6e-15（`c1+c2` 折成 f64 的 `2.1000000000000001`，左边是实数上的 `0.1+2`；差的不到结果的一个 f64 ulp 的四分之一，长双精度下约 370 个 ulp）。我判断这是许可覆盖的一次合法舍入，不该判红。请裁定：要严格相等的话，"规则自身折叠常数"算不算许可覆盖、这 5 条是否接受变红。
- **(2) 下溢成零与次正规的边界。** 现在 `0` 与"下溢后的极小非零参考值"算相等（靠契约里那项次正规绝对量），而"一边是 0、一边不是 0"不走符号零那一半。要不要单独判一条？
- **(3) f16/bf16 什么时候接进来。** 本车道 `realComparable` 只认 f32/f64，所以 `bf16`/`f16` 的实例落在旧路径上（本树读数仍是 `sampled float_combine_mixed<f16>: … reason the check cannot make a value of type 'f16'`，采样器不画 f16）。h432 的分支未并入本工作树，它那条 `float_combine_mixed` 的 `knownRefuted` 也还没撤。请裁定由哪条车道把 f16/bf16 接进参考精度里比。
- 附带观察（不请裁定，供顺手修）：`stdlib/rules.cml:119-120` 的"the check compares its two sides with the error bound of that permission"在改写之后不够准（现在是"在参考精度里比，容差才是许可给的"）。我没动这个库文件，好让"守卫与许可逐字未改"这件事的 diff 是空的。