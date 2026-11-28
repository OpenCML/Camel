# 卡：一条 XFAIL 标记"陈旧"的真身——不是转绿，是**判词丢了**

**发现者**：`lane-block-hash`（`D-1128-57` 的收尾件，`lane/xfail-onnx-markers`）。起点是决策者说
`ort_loop_loaded_weights` 的 XFAIL 已经陈旧（"要么绿了、要么断言换了"）。

## 现象

改之前那一条的判词只有两行、什么也没说：

```
[FAIL] ort_loop_loaded_weights                              550.56 ms
[INFO] expected exit 0, got exit 1
[INFO] known failure changed its mind: expected "kind: ExportUnsupported, message: in 'arm4.1.pb':
       the dtype or shape of a value changes from one call of a recursion to the next",
       got "expected exit 0, got exit 1"
```

同一个 plan 里另一条 `ort_loop_values_dynamic` 一直是老老实实的 `[XFAIL] expected exit 0, got exit 1`。
差别不在"红不红"，在**判词里还有没有信息**。

## 追到底（探针，跑完即删，没进提交）

`test/cases/modules/onnx/ort_loop_loaded_weights.cml` 的出口是自吞的：

```cml
match export_model<...>(...) {
    case error e => sync { println(e); 0 }   // ← 把拒绝打印成值，再答 0
    case _ => 1
}
println(exported)
if (get_env('ONNX_OUT') ?? '') == '' || exported == 1 then 0 else 1
```

camel 的真实输出（探针抓的原文）：

```
rc: 0
stdout: "{ kind: ExportUnsupported, message: in 'arm4.1.pb': the dtype or shape of a value changes
         from one call of a recursion to the next; this exporter does not yet carry an array whose
         length changes between rounds as an ONNX sequence, ...
stderr: ''
files:  ['native_0.npy', 'native_1.npy', 'native_2.npy', 'native_3.npy', 'w.npy', 'x.npy']   ← 没有 m.onnx
```

- **`exit 0` 不是绿**：`.cml` 里那个 `if … then 0 else 1` 只是一个表达式的值，没当返回码用，所以
  无论如何都是 0。python 侧 `camel()` 只看返回码，于是放行；下一步 `onnx.load(m.onnx)` 崩在
  `FileNotFoundError`，判词退化成 runner 的那句模板话。
- 于是"这个用例现在是什么状态"这件事，**从判词里读不出来**了：它仍红（导出被拒），但红在哪、拒的
  是哪句，全丢了。陈旧的是**标记**（`known_fail_msg` 还钉着旧句子），不是事实。

## 修法（不是改期望）

1. `known_fail_reason` 两条都还引着被撤回的前提 `a loop can only carry values of a fixed dtype
   and shape`（`test/plans` 里 2 命中、`src` 里 0）⇒ 改成引**树里真在打的那句拒绝**；
   `known_fail_note` 写明"task-383 落地时转绿"（`known_fail_ends_when` 只有 `{prints}`/`{passes}`
   两种形状，"谁修"只能落 note）。
2. `known_fail_msg` 换成现在真打的那句 `this exporter does not yet carry an array whose length
   changes between rounds as an ONNX sequence`。
3. **用例侧加一句**（`camel()` 里）：程序打印了 `kind: ExportUnsupported` 就带着拒绝原文抛
   `FAIL: the export was refused:`。不动语义、不动 `expect_*`、不动结束条件。

改后：

```
[XFAIL] ort_loop_values_dynamic                             962.64 ms
[XFAIL] ort_loop_loaded_weights                             836.98 ms
[SUMMARY] total: 2, pass: 0, fail: 0, known failures: 2
[KNOWN] …ort_loop_loaded_weights: prints "ort loop loaded weights: worst"
```

## 规矩（这一条要记住的形状）

> **一个吞掉拒绝的用例，会把判词退化成一句没有信息量的话。修法是让拒绝原文能被抛出来，不是改期望。**

- 判据分辨：`exit 0` 只说明"这条路走完了"，不说明"这件事对了"；`case error e => println(e); 0`
  这类"把错误当值再答成功"的写法，在用例里等价于**把红灯的线剪了**。
- 同一形状的第二例：`--verify-ops` 那句两侧文本同错，所以比出来是绿的——**绿不等于对**。
- 自查一句：**任何 XFAIL 的 `known_fail_msg`，必须在"判词文本"里真的出现过**；`known failure
  changed its mind` 是 runner 在替你看这句话，它一喊，先查"判词是不是丢了"，别急着改期望。
- 加标记三件套的顺序：`known_fail_reason` 引**树里现存的**那句（不是被撤回的那句）、
  `known_fail_note` 写谁修、`known_fail_msg` 钉**现在的判词文本**。