# 格式说明的不对称在 develop 上已不存在，剩下的是 C++ 写出器

日期：2026-10-31　车道：`lane-fmt`

裁定的原文是：

> **「`${arr:.6f}` 在插值里被拒，而 `'{:.6f}'->format<xs>` 接受。插值和 `format` 必须接受同一套格式说明，这是规则统一的缺陷，修掉。」**

这一页先说前提：**这条不对称在 develop 上已经不存在了**，两条写法落到同一个调用、给出同一段文本；今天真正剩的缺口在另一端——`std::cpp` 写出的 C++ 程序里，数字数组的格式说明会被拒。前提的现状先记下来，比反复解释省事。

## 一、前提的现状：两个读数一样

同一个程序，`let fs: float[] = [0.7692, 0.7808]`：

```
插值   `out: ${fs:.6f}`           ->  out: [0.769200, 0.780800]   exit 0
format 'out: {:.6f}'->format<fs>  ->  out: [0.769200, 0.780800]   exit 0
```

**两边都接受，逐字节相同。** 作者自己撞上的那一段也一样：把
`showcase/d4-mcts/selfplay.cml` 的 `let curve = '{:.6f}'->format<losses>` 改成直接插值
`${losses:.6f}`，程序 exit 0，打印

```
mcts selfplay losses=[0.640625, 0.483687, 0.358262, 0.279323, 0.263602, 0.297754, 0.325618, 0.320763, 0.293756, 0.261926]
```

## 二、为什么一样：本来就是一个解析器

- `src/builtin/operators/str.cpp:235` —— `checkFormatPattern`，**唯一**判断一条格式说明是否被接受的代码。
- `src/builtin/executors/builtin.cpp:690/694` —— `:io/print`、`:io/println`（`formatWithArg`）与 `:str/format`（`formatNormArg`）登记的是**同一个**检查器。
- `src/compile/gir/builder_operators.cpp:3699` —— 编译期**唯一**的调用点。
- `src/parse/ast/builder.cpp:4736` —— `visitFStringData` 把 `${expr:spec}` 就地落成同一个 `:str/format` 调用（表达式进 `with`、pattern 进 `norm`），此后各阶段看不见"插值"这回事。

所以没有第二套解析器，也没有"让哪一处向哪一处看齐"可谈。裁定引用的读数来自更早的二进制：
`85a591b34`（2026-10-25，**act on every element when a specification meets a numeric array**）
把"规格作用于数字数组的每个元素"这半补上，同时给 `checkFormatPattern` 加了数组分支；
在此之前 `'{:.6f}'->format<xs>` 打印数组的无规格文本，插值同样。

## 三、92 对矩阵：旧写出器 vs 新写出器

13 个类型 × 每个 8 或 5 种规格（`{}`、`{:.6f}`、`{:d}`、`{:>8}`、`{:.2f}`、`{:x}`、`{:e}`、`{:08.2f}`），
每对写两遍（插值 + `format`），同一份 C++ 源分别用**旧** `format.h`（`HEAD` 那一版）和**新**
`format.h` 编出来跑，再与 `std::fvm` 的文本逐字节比：

```
[SUMMARY] pairs=92 front_reject=47 same=33 differs=0 untranslatable=12 one_side=0 checked_programs=66
          old_programs_fail=20 old_programs_wrong=6 old_pairs_fail=10 old_pairs_wrong=3 old_pairs_same=20
          new_header_failed=0
```

- **47 对**前端按类型同拒（`int[]` 配 `{:.6f}`、`f32` 配 `{:d}`、元组/结构配数值规格…）。它们在任何后端都一样，不因 C++ 而变。
- **12 对** `std::cpp` 根本不能翻译（`f16`/`bf16` 没有 C++ 字面量、未定型数组），修前修后都一样。
- 剩下 **33 对**是这次变的：修前 10 对在 C++ 里直接 `FormatFailed`，3 对**静默写错**（`{:>8}` 把整个数组的文本当字符串补齐，exit 0），20 对本来就对；修后 **33/33 逐字节相同**，失败 0、写错 0。
- 程序级：修前 20 个跑不起来、6 个悄悄写错，修后 **0 / 0**。

（同一套读数在更早的 102 对扫描里是 57 对前端同拒；多出的 10 对是我把类型名写成 `complex`/`tensor`
被当成未解析引用，属噪声。）

## 四、修的是哪一半

`include/camel/standalone/format.h`：`FormatArg` 多一种 `Kind::Array`，数组参数随身带上"按规格逐元素写"
的那份文本——`SpecPerElement` 只认 `int32`/`int64`/`float`/`double`，与 `isNumericElementType`
同口径；元素不是数字的数组（`string[]`、`bool[]`、`f32[][]`）仍走文本，规格照旧排整段文本，与解释器一致。

两行逐字节相同（同一个程序，`'{:.6f}'->format<fs>`）：

```
std::fvm  [0.769200, 0.780800]
std::cpp  [0.769200, 0.780800]
```

钉住它的用例是 `test/cases/trans/cpp/format_array.cml`，列进 `test/tools/cppexec/expect.txt`
（由 `feat/trans/cpp.agree_with_fvm` 翻译、编译、运行、与 `std::fvm` 逐字节比）。

## 五、`agree_with_fvm` 那一片红：`:var/after` 从来没有翻译

`feat/trans/cpp.agree_with_fvm` 是 `slow` 层，`-Plan all -Tier fast` 跑不到它。它在慢层里**是红的**——
`std::cpp` 直接拒绝整个程序：

```
:var/after … on a value of type 'var int', which has no translation in std::cpp yet
```

`test/cases/linear/nvm/var_params.cml` 与 `test/cases/trans/cpp/exec/variables.cml` 都在这个理由下被拒。
但整闸只跑 `-Tier fast`，这一片红从来没进过读数：**缺口不需要被藏起来，它只要待在没人跑的那一层就够了。**

`:var/after(cell, call)` 是"变量在被那次调用写过之后的版本"（`(var T, any) => var T`）：结果是那个单元本身，
第二个实参是那次调用、**不被读、可以是 `void`**。写出器补上它（`23298498c`）之后，生成的程序里那次调用
本来就是自己的一条语句，效果按图给的顺序跑，版本就是那个单元——与一次写留下的一样。

两个程序现在都编译、运行、与 `std::fvm` 逐字节相同：

```
std::fvm  12 33 / abc / 5 12 12
std::cpp  12 33 / abc / 5 12 12
```

整张 `feat/trans/cpp.plan.toml`（含 slow 那一项，118 个程序）：`[SUMMARY] total: 11, pass: 11, fail: 0`。

顺带一条：`test/tools/toml-lite.mjs` 的转义，**换序修不好**（`\\` 先塌成 `\` 之后，`\n` 那一遍仍会咬住
后面的 `n`），改成**一次左到右解码**；`"x\\ny"` 旧读法是 `\` 加一个真换行（进了 JS 就是行继续），新读法是
`\` 和 `n` 两个字符。

## 六、两条判据

> **裁定也可能基于过时的读数：拿到裁定先去 develop 上复现一次；不复现，就先把前提写下来，再谈要不要改。**

> **一个后端缺一条翻译，不会报错，只会把整个计划留在慢层；慢层没人跑，缺口就一直绿着。**