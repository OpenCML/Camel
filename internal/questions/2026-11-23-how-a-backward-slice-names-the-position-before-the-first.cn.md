# 反向切片的末端怎么写：`-1` 是唯一能用的写法，而它现在是故障

## 事实

`reverse` 改用索引数组之后，同一族里仍有六处写着那个端点，它们都是**用例**，不是库：

| 处 | 切片的值（静态类型） | 现在报的 |
|---|---|---|
| `test/cases/modules/onnx/ort_core_slice.cml:83` | `f32[4]` | `SemanticDiag::ArgumentsRejected`（`83:27`） |
| `…:93` | `f32[6]` | 同上（`93:27`） |
| `…:103` | `f32[2, 4]` | 同上（`103:30`） |
| `…:113` | `f32[...]` | 同上（`113:19`） |
| `…:118` | `f32[6]` | 同上（`118:27`） |
| `test/cases/modules/tensor/subscript/slice_step_negative.cml:15` | `f32[4]` | 同上（`15:20`） |

六处的正文逐字相同，只有参数类型一处不同：

```
error[SemanticDiag::ArgumentsRejected]: Invalid call to '__slice__' with argument types
<> (f32[4], int, int, int, int, int): a slice bound must not be negative, and -1 is;
the last element is 'last(t)'.
```

`camel` 的退出码是 1，**程序整份不编译**：`ort_core_slice.cml` 一次报 5 条，`slice_step_negative.cml` 报 1 条。两个用例所属的计划读的是「跑起来」：
`onnx.plan.toml` 的 `export_core_slice` 期望 `exported`、`subscript.plan.toml` 的 `slice_step_negative` 期望
`ran\n[3]\n9.0\n[4]\n[3]\n`。所以这两条在 `develop` 上是红的，红在**编译**，不是红在值。

## 为什么这不是「旧写法」

三条读法把这件事钉在一起，而它们互相矛盾：

1. **拒绝在哪儿**：`src/stdlib/std/builtin/modules/globals.cpp:440-447`（`staticSliceRejection`），先判 `from < 0`、
   再判 `to < 0`，**在问 extent 之前**。于是 `ort_core_slice.cml:113` 上的 `f32[...]`（秩未知、extent 未知）
   也照样被拒 —— 拒绝的理由**与越界无关**，是「端点不许是负数」。5.4 那句「不支持负数下标（负数是故障），
   最后一个元素用 `last(t)`」正是它的依据。
2. **运行时怎么读**：`src/stdlib/std/builtin/operators/struct.cpp:337-364`（`sliceRange`）。步长为负时，
   **没写的**末端取 `-1`（`357`），而**写了的** `-1` 走 `358-360` 的 `end < 0` 分支抛 `SliceOutOfRange`。
   也就是说：内核认「第一个元素之前那个位置」，但它只允许这个位置出现在**没写**的时候。
3. **导出器怎么编码**：`ort_core_slice.cml` 的头注释与 `src/stdlib/onnx/lowering.cpp:465` 都写着，反向切片没写的末端
   在 ONNX 里要用「比任何 extent 都小的末端」表示，因为 `-1` 会被 ONNX 加到 extent 上、变成最后一格，切片就空了。
   于是「第一个元素之前」这个位置，在 Camel 的内核、ONNX 的编码里各有一个写法，**而在 Camel 的语法里没有**。

所以不能靠改用例：用例写的正是「反向走到第一个元素」，而**现在唯一能表达它的写法被前端拒掉**。
这不是把 `[a:b]` 改成 `[a..b]` 那一类文本迁移 —— 那些已经在 `8ee76003e` 修完，`git grep` 全树 0 行。

## 要裁的一件

**带步长的切片，末端怎么表达「走到第一个元素」？** 两条路，改动的地方完全不同：

- **甲：放宽「端点是负数即故障」**，让**步长为负时**的末端 `-1` 表示「第一个元素之前」，与 `sliceRange`
  没写末端时的默认值同义。改动只在 `staticSliceRejection`（`globals.cpp:444-447` 之前加一条按 step 的分支）
  与 5.4 那句「不支持负数下标」的措辞；运行时的 `358-360` 也要按同一条松开，否则编译通过、跑起来照样抛。
- **乙：在语法上给一个可省末端的步长形式**（`t[a.. step s]`，或 `range(a, .., s)`），末端干脆不写，
  于是「不写就是 `-1`」那条内核默认自然成立，`-1` 仍然可以是故障。改动在解析器与 `builder_*` 的切片降级、
  以及 5.4 的切片写法那一节；两份用例改成新写法。

两条都要动语义或语法，**不是本车道能自己定的**。判据上还有一条现成的对照：正向切片的「省末端」已经存在
（`t[a..]`、`[..]`），反向缺的是同一个东西 —— 但反向的省末端**必须同时带步长**，而步长现在只能写在
`range(...)` 里，那里末端是必填参数。这是问题形状本身，不是笔误。

## 这一页不做的事

- 不动那六处用例：它们的正文是「反向走到第一个元素」，在裁之前改成别的写法，等于把一个表达不了的程序
  改成一个表达别的东西的程序。
- 不动 `staticSliceRejection` 的其余判断：它说「轴外边界是故障」是对的，错的是**负端点作为末端**这一条
  与 5.4 的措辞绑在一起。
- 不新增用例。裁决落地时，这两份用例自己就是读数：`export_core_slice` 与 `slice_step_negative` 从红转绿，
  且 `ort_core_slice.py` 的五组对照（`[5:1:-2]`/`[1:5:2]`、`[3::-2]`/`[3::2]`、`[::-2]`/`[::2]`、
  `[::-1]`/`[:]`、`[:, ::-2]`/`[:, ::2]`）逐项不变。