# 纯数据谓词写进 `when` 子句时读不出来：聚合元素类型在 `to_bytes` 上得到 `WhenConditionNotConstant`

提出者：`handover/h574-deletion-tails`（2026-10-25）。相关裁定：翻转线上"让纯数据成为一个谓词"的那条——
"设计原则 10.5 已经定义了'纯数据类型'：数字、布尔、复数，以及只由它们组成的结构体和元组……把它做成一个谓词
（如 `is_plain::<T>()`），`to_bytes`、`from_bytes`、DLPack 交换、数据块的分配都用这同一个谓词。"

**一句话的问题**：`is_plain` 是写在 `stdlib/types.cml` 里的一个普通 Camel 函数，它判聚合类型时要走字段
（`match type T { case struct | tuple => … }`），而 `when` 子句的编译期求值读不了"走字段"这一步——
于是 `to_bytes` 在元素类型是纯数据结构体/元组时不再是一条干净的"没有这个重载"，而是报
`WhenConditionNotConstant`，把语言内部的求值限制当成用户的错误说出来。

## 一、今天的状态（本树实测）

`stdlib/tensor.cml:84` 与 `stdlib/types.cml` 的谓词：

    export func to_bytes::<T>(t: T[..]): Bytes when is_plain::<T>()

    export func is_plain::<T>(): bool {
        return match type T {
            case struct | tuple => every_field_plain::<T>()
            case _ => is_numeric::<T>() || is_bool::<T>()
        }
    }

四个探针程序（`/tmp/p_*.cml`，`camel <file>`，本树 `out/latest`）：

    func f(xs: f32[])      { to_bytes(xs) }        // 通过（只有 EffectNotExecuted 警告）
    func f(xs: f32[2])     { to_bytes(xs) }        // 通过
    func f(ps: Point[])    { to_bytes(ps) }        // Point = { x: int, y: f32 }
    func f(ps: (int, f32)[]) { to_bytes(ps) }

后两条逐字（文件位置报在声明那一行，`tensor.cml:84:62` 就是 `when is_plain::<T>()`）：

    error[SemanticDiag::WhenConditionNotConstant]: The 'when' condition of 'to_bytes' cannot be
    evaluated when the declaration is instantiated: it calls 'is_plain', which this build-time
    evaluation cannot read: a type argument is not concrete in this body.
      --> /tmp/p_struct_open.cml:84:62
    help: The condition is checked at instantiation, before the body is built, so it can only read
    what is known there: the types bound to the declaration's type parameters, and the 'with'
    arguments the call writes as compile-time constants. …

读法：`is_plain::<f32>` 能求值，是因为它的 `case _` 臂只落到 `is_numeric`/`is_bool` 这两个**内建**谓词；
`is_plain::<f32>` 与 `is_plain::<Point>` 走的是同一份函数体，差别只在聚合那一臂要调用**另一个用户函数**
（`every_field_plain`，它自己还要递归判字段类型）。所以"能读"的边界是"这条 `when` 的求值会不会走到一次用户函数调用"，
而不是"类型具体不具体"。

对照：`from_bytes` 今天仍然按元素类型注册（`stdlib/signatures/tensor.cml`），对聚合类型给出的是干净的重载表：

    error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (Bytes, int[]).
    help: … Available overloads: <tensor:from_bytes_f32> … <tensor:from_bytes_f64> … <tensor:from_bytes_i32>
    … <tensor:from_bytes_i64> … <tensor:from_bytes_bool> … <tensor:from_bytes_bf16> … <tensor:from_bytes_f16>

## 二、本 lane 已经落下的那一半（确定的部分）

- C++ 侧"纯数据"的定义已经统一到一处：`scalarDataLayout(TypeCode)`（`include/camel/core/type/pure.h:73`）——
  数据块的分配（`isCompactElementType`）、DLPack 交换（`toDLPack`/`toDLPackType`/`fromDLPackType`）
  都读同一张表，不再各写一份元素类型表。
- `to_bytes` 的声明按裁定写成 `when is_plain::<T>()`；`from_bytes` 仍按元素类型注册。
- 没有改动的部分：`from_bytes` 的元素类型集合仍是那 7 个标量（f32/f64/i32/i64/bool/bf16/f16），
  DLPack 仍然按标量 dtype 命名。

## 三、选项与代价

**(a) 把 `is_plain` 做成内建谓词**（与 `is_numeric` 同级，编译器里按 `isPureDataType` 的同一判据实现，
`when` 求值走的是内建那一张表）。代价：核心要加一个谓词（求值器、`match type` 之外的判据来源、
给 AI 的参考与语言参考各一处）；好处是裁定字面成立——`to_bytes` 对纯数据聚合也就可读、可实例化，
`load_npy::<T>` 一类以后同样受益。随之要回答 `from_bytes` 对聚合元素类型怎么注册（今天按标量 dtype 注册，
聚合元素类型需要一个带元素类型的入口，形状之外的 ABI 也要定）。

**(b) `to_bytes`/`from_bytes` 的门槛写成它们真正实现的内建集合**（`when is_numeric::<T>() || is_bool::<T>()`），
纯数据谓词只用于 C++ 侧（数据块、DLPack）与文档。代价：裁定里"同一个谓词"只在 C++ 侧成立；
聚合元素类型回到干净的 `NoMatchingFunction`，`WhenConditionNotConstant` 的读数消失。
好处：不需要核心改动，读法在今天就自洽。

**(c) 保持现状**，把 `WhenConditionNotConstant` 当作"聚合元素类型不被接受"的表达。
代价：这条消息说的是求值器的限制，不是 `to_bytes` 接受什么；用户按它修不了程序，
而且任何以后写的 `when is_plain::<T>()` 都会继承这条读数。

## 四、卡住的是什么

- `to_bytes` 对"纯数据聚合元素类型"（结构体/元组数组）既不接受也不干净地拒绝——裁定在那一种类型上落不下去；
- `from_bytes` 的元素类型面（7 个标量）与 `to_bytes` 现在声明的面不一致，两个方向对不上；
- 依赖这一点的下游：`load_npy` 的声明式形状（裁定说 P4 后写 `load_npy::<T>(path: string): T[..]`）
  同样会把 `is_plain` 写进 `when`。

（本 lane 没有为此改语义、也没有新增用例钉住这条读数。）