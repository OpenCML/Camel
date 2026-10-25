# 维度名放宽与"元素类型改变"两条读数：放宽走不到，元素类型那条报的不是裁定要的诊断

> 说明（协调者）：提出问题时的代码在本地**翻转线** `handover/h465-flip-integration`（尚未并入 develop、也未推送）；页里引用的提交号若在 develop 上查不到，属于那条线。


（h476-recursion-cases 车道新增，2026-10-25。裁定原文见
[`../reviews/questions-2026-10-24-evening.md`](../reviews/questions-2026-10-24-evening.md) 的
§dim-name-relaxation-has-no-readable-difference 与 §recursion-specialization-fixed-point-followups 第 2 条。）

**问题一句话**：两条读数与裁定的预期不符——①裁定说`T[n]`形参"按实参绑定"、放宽到 `?` 之后应当只有
**两个**实例（`f32[2]` 与 `f32[?]`），实测是**一个都不建**（`specializations: 0`，非递归的 `f32[n]` 形参
同样 0）；②裁定说"递归调用改变了元素类型"要报一条点名它的编译错误，实测报的是实例化深度守卫
（`GenericInstantiationTooDeep`）。**这两条算缺陷（要按裁定修），还是裁定要按实测改写？**

**卡住**：`test/plans/feat/opt/generic.plan.toml` 的两条用例（`specialize_dim_name_grow`、`specialize_element_type_nest`，
各自的 `desc` 与本页的读数逐字一致，本条被 `test/cases/opt/generic/specialize_dim_name_grow.cml` 与计划文件
按路径引用）、`src/runtime/draft_inline.cpp`（`collectDirectFuncSpecializations`，`:576`）、
`src/compile/gir/builder_generics.cpp`（`kMaxOpenInstancesOfOne` 的判据，`:1701`）与
`src/compile/gir/builder.h`（`kMaxOpenInstancesOfOne = 8`，`:1192`）。

## 1. 放宽（`grow`）：0 个实例，不是 2 个

作者给的写法原样入库：`test/cases/opt/generic/specialize_dim_name_grow.cml`（只把 `read_int()`
换成 `int(get_env('H476_GROW_DEPTH'))`——计划运行器把每个测试的标准输入关掉
（`test/tools/run-tests.mjs` 的 `stdio: ['ignore', ...]`），环境读数是同一种运行时值）。

读数（88 主机，`d4989050c` 的 Release 构建）：

| 跑法 | 读数 |
|---|---|
| 无 pass，深度 2 | stdout `5.0`；stderr `DimensionNameUsedOnce`（`code=0x03100161`，`24:1`） |
| `std::opt::specialize std::stats std::nvm` | `GRAPH_STATS {"graphs":3,…,“specializations”:0}` 加 `5.0` |
| `std::opt::fold std::opt::specialize std::stats std::nvm` | 同上，`specializations: 0` |
| `std::opt::specialize std::jgir` | 图只有 `__root__` / `main` / `grow`；`grow` 的签名逐字 `(xs: f32[n], k: int) => f32`；没有 `grow$…` |

放宽（`f32[2]` → `f32[?]`）走不到，断在它前面一步：**写 `f32[n]` 的形参在调用点不产生"细化"**，
所以第一圈就没有实例键可建。隔离读数（探针，未入库）：

- `take(xs: f32[n])`，非递归，调用点实参 `f32[2]`：`specializations: 0`，3 张图。
- `take2(xs: f32[n], ys: f32[n])`——名字写两遍、确实成关系、不再有 `DimensionNameUsedOnce`：
  仍然 `specializations: 0`，3 张图。
- `grow2(xs: f32[n], ys: f32[n], k: int)`（同 `grow`，名字写两遍）：`specializations: 0`，
  图里只有 `grow2`，签名逐字 `(xs: f32[n], ys: f32[n], k: int) => f32`。

计数上限对照：`kMaxTypeSpecializationsPerRecursiveFunction`
（当时在 `include/camel/runtime/draft_inline.h:43`；该常量此后按裁定第 3 条从代码里删除，
落点是 `bb4e01be8` "Remove the count bound on a recursive function's type specializations"，
"past it the call kept its current callee" 的截断换成一条报出函数名的失控守卫）
从 8 改成 4096，上面每个读数一字不变（`specializations` 都是 0）；探针跑完常量已改回 8。

相邻用例 `specialize_dim_name_recursion`（`walk`）里的 `walk$spec0` 与本条不冲突：那个程序在入口
调用给递归的 `int` 值参数传的是常量 `3`，本条把深度读成运行时值。`collectDirectFuncSpecializations`
（`src/runtime/draft_inline.cpp:572`）里值绑定与类型细化是两条路；`f32[n]` 走不到类型细化那条。

**要定的一条**：`T[n]` 形参今天到底应不应该在调用点产生实例（细化）？按第 2 条"形参写了维度名时
按实参绑定"，`grow([1.0, 2.0], k)` 的 `n` 该绑成 2、递归那层绑成 3、第二圈再放宽为 `?`；今天的实测
是这些都不发生，于是作者期望的"恰好两个实例"拿不到。问题不在 `concat` 的结果 extent——非递归的
`f32[n]` 形参同样是 0 个实例。

## 2. 元素类型改变：报的是实例化深度守卫，不是裁定要的那条诊断

写法：`test/cases/opt/generic/specialize_element_type_nest.cml`，`func nest::<T>(xs: T[n], k: int)`
每层把数组再包一层，元素类型逐层变成新的具体类型（`float[n]`、`float[n][n]`、……）。

实测（`camel <file> std::opt::specialize std::nvm`，退出 1，程序没有运行）：

```
test/cases/opt/generic/specialize_element_type_nest.cml:17:40: [Error]: Instantiating the generic
function 'nest' with T = float[n][n][n][n][n][n][n][n] is refused: 8 instances of it are already
being built inside one another. (name=SemanticDiag::GenericInstantiationTooDeep, code=0x03000055)
```

位置逐字：JSON 里 `line: 16, character: 39`（0 基）= 第 17 行第 40 列，正是递归调用
`nest([xs], k - 1)`；`relatedInformation` 逐层写回 `nest<float[n][n][n][n][n][n][n]> …`。

这不是"递归调用改变了元素类型"那条诊断，而是 typecheck 里 `kMaxOpenInstancesOfOne = 8`
（`src/compile/gir/builder.h:1162`，判据在 `src/compile/gir/builder_generics.cpp:1701`）的计数守卫
——**撞上限**。

另一条读数：只交替两种元素类型的环（`f32[n]` ↔ `int[n]`，探针 `probe_map.cml`：两个函数互相调用，
各自用 `map` 转出对方的元素类型）**完全不报错**：退出 0，stdout `3`，
`GRAPH_STATS {"graphs":6,…,"specializations":0}`，只有 `DimensionNameUsedOnce`。这条环的键会重复、
本来就到不动点，所以"不报错"未必是错的；但第 2 条的措辞是"键不同就是元素类型不同 → 报编译错误"，
与它不一致。

**要定的一条**：元素类型改变这条算"已经有了（只是措辞与位置不同）"还是"还缺一条诊断"？若按裁定补，
`nest` 那条用例的行列与消息要跟着换。本车道没有替它加任何限制：`nest` 按今天的读数钉住
（`specialize_element_type_nest.refused`），`f32[n]` ↔ `int[n]` 那条只留在本页。

## 选项

1. **A — 按裁定修实现**：`T[n]` 形参在调用点按实参绑定（`grow` 拿到作者期望的两个实例），元素类型改变
   另报一条点名它的诊断。代价：`test/plans/feat/opt/generic.plan.toml` 两条用例的 `desc` 与读数要重推；
   放宽那一圈的实例键要能建出来（今天值绑定与类型细化是两条路，`f32[n]` 走不到后者）。
2. **B — 按实测改写裁定的措辞**：`T[n]` 在调用点不产生实例（实例签名就是声明），元素类型改变由实例化
   深度守卫挡住就算"已经有了"。代价：裁定的"恰好两个实例"、以及 `docs/design-principles` 5.5 那段的读法
   要一起改写，`specialize_dim_name_grow` 的期望（`f32[2]` 与 `f32[?]`）作废；且交替元素类型那条环仍然
   一个字都不报。

探针源码在 `D:\Projects\Camel-wt\notes\h476-probes\`（`probe_take.cml`、`probe_take2.cml`、
`probe_grow2.cml`、`probe_map.cml`），未入库。