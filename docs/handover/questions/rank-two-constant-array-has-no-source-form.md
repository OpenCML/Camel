# 秩 ≥2 的常量数组在源码里构不出来：`core.loopDomain.extent` 的秩 ≥2 分支没有读数（实测，2026-10-25，h453 lane）

前提裁定：`docs/handover/reviews/questions-2026-10-24-evening.md` 的
`loop-domain-through-the-tensor-iterator`（`core.loopDomain` 的 `extent` 就是 `shape[0]`，秩 1 时等于
元素个数，所有读者共用这一个事实），以及 `docs/technical/39_loops.md` §2a（`for x in xs` 对任何数组都
沿第一维，由核心负责）。本 lane 按裁定把事实写进核心（`src/runtime/core_facts.cpp` 的
`constantLength` 只读常量 DATA 数组的 `size()`，即 `shape[0]`），**没有改语义**。

## 一、五种写法实测（`camel inspect --facts <case>` 的 `FACT` 行，逐字）

| 写法 | `core.loopDomain` 的读数 | 元素 |
|---|---|---|
| `let a: int[3] = [1, 2, 3]` | `for: t sequential, t < len(node 0) = 3; element port 2 reads xs(t)` | `int` |
| `let d = [[1.0, 2.0], [3.0, 4.0]]` | `... t < len(node 0) = 2; element port 2 reads xs(t)` | `f32[]`（`len(r)` = 2） |
| `let b: f32[2, 2] = [1.0, 2.0, 3.0, 4.0]` | `... t < len(node 0) = 4; element port 2 reads xs(t)` | `f32` |
| `reshape([1.0, 2.0, 3.0, 4.0] as f32[], [2, 2])` | `... t < len(node 2); element port 4 reads xs(t)`（**无 extent**） | `core.shape` = `f32 [2]` |
| `([1.0, …] as f32[]) as f32[2, 2]` | 编译拒绝：`Dynamic cast from 'f32[]' to 'f32[2, 2]' is not allowed.` | — |
| `let t: Tensor = [1.0, 2.0, 3.0, 4.0]` | 编译拒绝：`The literal '1.0' cannot be represented as type 'void'` | — |

第三行是**平铺字面量 + 平铺注解**：声明写 `f32[2, 2]`，实际值仍是秩 1 的 4 元素数组（循环走 4 轮、
元素是 `f32`），即注解没有把平铺字面量按声明成形。第四行说明 `reshape` 的**结果类型**是
`f32 [2, 2]`（`core.shape` 有读数），但 iterable 是 reshape 算子、不是常量数组，所以没有 extent
（`std::opt` 没有把它折成静态常量）。第五、六行说明常量**裸 `Tensor`** 也写不出来（`void` 元素类型
收不了字面量，`f32[]` 到平铺形状的动态转换被拒）。

## 二、要裁定的问题

1. 平铺字面量 + 平铺注解（第三行）该不该按声明成形？按声明成形，秩 ≥2 的**常量**数组才存在，
   `extent` 的秩 ≥2 分支（行数 ≠ 元素个数）才有 `FACT` 读数；不按声明成形，则请确认
   `new`/`reshape` 是唯一的平铺构造，本事实的秩 ≥2 分支等折叠（或等某个按声明成形的写法）落地后
   才有读数。
2. 若「可折叠成常量数组的 shape」也应当给出 `extent`（本 lane 按字面理解为「值可折成常量的 cast/
   copy 链」），请说明由谁落：本 lane 实测**构造不出**可触发的输入（见上表后三行），因此没有写这条
   看穿分支（不留未验证代码）。`core_facts.cpp` 今天只读常量 DATA 数组。

本 lane 能给出的秩 ≥2 读数只有运行时那半：`test/cases/loops/iterate_tensor.cml`（`new([[1,2],[3,4],
[5,6]])` 走 3 行、`zeros([0, 2])` 走 0 行、行内再循环，期望 `21 3 3 6 7 21 3`）与
`test/cases/loops/h453-*`（测量用临时文件，已删）。

## 附：另一棵树上的完整实现（事实，供"按声明成形 vs 按今天读数"定夺）

`handover/h469-literal-shape` 上有一份**可以整体复活**的完整实现：提交 `20e7a7713`（`+641/−36`），
在 `src/compile/gct/builder.cpp` 加 228 行、`src/core/rtdata/conv.cpp` 加 64 行，其中 `statedExtents()`
负责按注解给的形状把平铺字面量成形。本翻转线上**整个不存在**这份改动（今天仍是
`Array::create(alloc, type, 0)` + `append`），`git grep` 在 `handover/h465-flip-integration` 里对
`h469`/`literal_rank_two`/`err_literal_ragged`/`reshape_constant` 零命中。

它的 5 条用例与 6 个 plan 段**钉的是"按声明成形"这个行为**，与本文件的实测读数方向相反：若维持今天的
读数，那些用例与 plan 段**不要**补进树（补进来会让 `array_shape` 计划红）；若裁定按声明成形，则可以把
`20e7a7713` 整条复活，不必重做。本文只记这个事实，不定语义。

### 复活可行性的实测（h490，事实，仍不写裁定）

1. 把 `20e7a7713` cherry-pick 到翻转线：21 个文件里 20 个自动合并，**唯一冲突**是
   `src/runtime/draft_session.cpp` 的 `cloneStaticSlot`——而翻转线上已有同一缺陷的**更大**修法
   （compact 分支 + `Array::create(..., array->shapeSpan())` + `numel()` 走元素），h469 那 12 行被完全
   包含 ⇒ 取 HEAD。复活后整体 diff `+633/−32`。
2. 三棵树同口径的红集合**两两逐字相同**：基线 `6770eda25` / 只复活代码 `daa56b6a7` / 代码加用例
   `451e830fd`，都是本 lane 当时的那 10 条，**新增红 0**；`array_shape` 计划多 **7** 段 `[[tests]]`
   （更正先前说的 6 段）且全 PASS。
3. **今天没有任何用例钉住"平铺/嵌套字面量写进秩 ≥2 声明位"这个读数**——全文检索只有 h469 自己那 3 个
   文件这么写。复活后 `err_literal_rank` 的 PASS 意味着 `let b: f32[2, 2] = [1.0, 2.0, 3.0, 4.0]` 从
   "能编译、读作 4 个元素"变成**编译拒绝**（`DimensionMismatch`），`reshape_constant` 的 PASS 意味着
   常量 `reshape` 折出的值是 `[2, 2]`/numel 4：这两件事今天**既不是红也不是绿**，是在空地上把读数落定。
4. `statedExtents()` 的射程：唯一调用点在 `src/core/rtdata/conv.cpp` 第 141 行（定义在 :28），**按类型
   判定**（任何"完整常量声明形状且乘积 == 元素数"的 `ArrayData` 都按该形状物化，`f32[4]` 也返回 `{4}`），
   字面量只是来源之一；`f32[]`/`f32[n]`/`f32[2, ?]` 仍走旧路。字面量**位置**那一半在
   `src/compile/gct/builder.cpp`（`statesARank`/`isConstantLiteral`/两个调用点/`isArrayLiteralData`/
   `literalShapeOf`/`visitRankedInto`）与 `src/compile/gir/builder_control.cpp`（`arrayLiteralFitsShape`
   换掉 3 处 `holdsStatedLength`）。
5. 它给的代价结论（供作者判）：整体复活＝解一处冲突、fast 层红集合一字不动、它自己 7 条全绿，是**最低
   代价选项**；但整体复活等于在**没有用例钉着的空地上**把该读数落定为"拒绝"。若作者认为该走"按声明
   成形"，应只复活 `statedExtents()` 而重写 GCT/GIR 那三处拒绝与那 7 条用例。