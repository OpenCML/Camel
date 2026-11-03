# 一个规则键只能挂一条规则，于是同一个算子的两种形状无法共存

## 问什么

`tensor:subtract` 的规则键是 `subtract/2`（`ruleKeyOf` 从算子名推出 head，`arityOfHead` 从本族索引取
arity）。一个键**只挂一条规则**，而这条算子会用两种形状被调用：

| 调用 | 操作数类型 | 谁写的 |
| --- | --- | --- |
| `x - m`（两个数组，可广播） | `(float[..], float[..])` | `probes/05_reduction.cml` 的 `x - mean<1, true>(x)` |
| `scores - 1000.0 * mask`（数组减一个数） | `(float[..], float)` | `probes/09_causal_mask.cml` |

已经落了 `(T[..], T[..])` 那一条（`jvp_subtract`），第二形态就被按名拒绝，**原文**：

```
error[RuntimeDiag::RuntimeError]: An error has occurred: transforms: jvp has no forward rule for
`tensor:subtract` (the rule takes (float[..], float[..]), and the operator has (float[..], float)).
```

要裁定的是：规则形参能不能写**并集**，让一条规则同时绑上两种形状；还是这只是"一个键一条规则"的表达力
上限，缺口就记在这里。

## 已量到的三件事

1. **键是 `subtract/2`，不是形状决定的**。`ruleKeyOf(family, "tensor:subtract")` 只读算子名与
   `arityOfHead`，与调用点的操作数类型无关 —— 形状是在规则**绑定**那一步比的
   （`portsTakeOperands`，`modules/transforms/jvp.cpp:112-140`，逐参数 `Type::equals`）。
2. **一个键一条规则是索引自己的约束**，不是这一族的：同族里第二个 `subtract` 规则会被
   `rulesets.err_duplicate_head.cml` 那条用例按名拒绝（"one key holds one rule" 是它的原文判据；
   `tensor:multiply` → `mul_tensor` 那次例外也是同一个理由促成的，见
   `ruleKeyExceptions` 的第二条）。
3. **声明里已经写过并集**：`stdlib/tensor.cml:146` 的 `take(t: float[..], indices: int64[..] | number[])`、
   `reshape(t: float[..] | number[])` 都是并集形参，并且编译得通、调用得分得清。**规则形参能不能写，还没测。**

## 已经量到的：并集写法在规则形参上**写不出来**（2026-11-03，实测）

按下节"默认"的第 1 步试了，**结论是先测并集这一步不用再试了 —— 它在语法层就不通**：

```
error[SyntaxDiag::MissingComma]: Expected ',' or ')' before '|'.
    --> stdlib/rulesets/jvp.cml:239:29
```

写法是 `func subtract::<T>(x: T[..] | T, w: T[..] | T): T[..]` 与对应的
`export rule jvp_subtract::<T>(x: T[..] | T, w: T[..] | T)`。`|` 在**普通函数/规则的形参表**里不是合法
记号，只在签名声明（`stdlib/signatures/*.cml` 与 `stdlib/tensor.cml` 那种 `export func` 的声明体）里
才是并集 —— §2 第 3 条那个先例属于**声明**的语法，不搬到**定义**的语法里。

所以并集这条路今天不存在，不是"没测过"。剩下的只有两条：保持一条形状的规则（今天的选择），或者给算子
拆第二个键（要动 `ruleKeyExceptions`，且第二形态的 key 没有算子名可推）。

## 不知道的

- 若愿意动核心：给 `tensor:subtract` 加第二形态的键（`ruleKeyExceptions` 加一行 + 一个模块内函数当键），
  形状仍是两把键两把锁，只是第二把锁有名字。代价是核心表多一行、两个键要各自维护。
- 掩码那条路还要第二种形状：`tensor:multiply` 的 `(float[..], bool[..])`（`probes/09_causal_mask`），
  与本节是同一类问题、不同算子。

## 默认（按此往下做，不为了让它过而把两种形状塞进一条规则）

1. ~~**先测并集**~~ —— **已测，并集在定义体里是语法错误**（见上节），这条路关闭。
2. **今天的选择：保持 `(T[..], T[..])` 一条规则**，`(array, scalar)` 形状保持那句拒绝，记在本页
   §"缺口"。不改 `ruleKeyExceptions`，不动核心。
3. 要改的是以后有人愿意动核心时：给第二形态加一把有名字的键，而不是把两种形状塞进同一条规则的形参。

**拒绝比错数好**：把两种形状塞进一条规则的写法（例如把第二个形参写成 `T` 再指望类型系统扩宽）如果
绑上了 `(float[..], float)` 却在广播形上给出错数，那是比今天红着一条探针更糟的结果 —— 这一页存在的
理由就是先记下这个判据，再动手。

## 缺口（今天红在哪）

- `probes/09_causal_mask`：`masked(scores, mask)` 的 `scores - 1000.0 * (1 - mask)` 停在 `tensor:subtract`
  的 `(float[..], float)` 形态；越过它之后还要 `tensor:multiply` 的 `(float[..], bool[..])` 形态
  （掩码是 bool，那是第二处同类的形状问题）。
- `probes/05_reduction`、`probes/08_embedding` 已经越过这条算子，读数写进了
  `showcase/c3-char-transformer/probe.py` 的 `EXPECTED`（`05` = 5.0 / 0.0，`08` = 25.0 / 14.0）。

## 读数（本机不构建，全部走主机 88zjwei，树 `~/camel-work/oprules`）

| 计划 | 读数 |
| --- | --- |
| `feat/compile/transforms` | 78 = **74 通过 / 3 失败 / 1 已知失败**，与 develop 逐名相同 |
| `feat/transforms/split` | 19 = **19 通过 / 0 失败** |
| `perf.showcase_c3` | 19 = **19 通过 / 0 失败**（`probes total=18 ok=9 refused=9`，`pinned=18 mismatched=0`） |