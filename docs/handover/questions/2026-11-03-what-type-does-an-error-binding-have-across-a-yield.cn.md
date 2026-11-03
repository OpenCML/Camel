# `case error e` 绑定的名字跨 `yield` 进状态时，那个 cell 的类型从哪来

## 问什么

`case error e` 绑定的名字跨 `yield` 进状态时，那个 cell 的类型从哪来？

一句话背景：作者 2026-11-03 18:10 已经裁定"绑定是局部量，跨 `yield` 就进状态"，所以这个 cell 要不要存在不是问题，问题是**它的类型从哪一份事实里读**——`case v` 读的是 payload，`case error e` 读的不是。

## 1 现在已经落地的答案（第 2 条的"对象"读法）

`case error e` 现在**建得起来**，没有拒绝。cell 的类型取的是 `type::errorType()` —— 不是新词汇，是核心里本来就有、GIR 自己也一直在用的那一个对象。落地在 `src/compile/gct/builder.cpp` 的 `Builder::visitGeneratorBody`（生成器 `match` 臂里、`case` 绑定登记那段），和 `case v` 复用同一套登记：`addCellOfType` + `cellNames`，绑定私有名、case 自己的 body 写 cell 再 `jumpTo(armPc)`。

用例 `stream.generators_error_binding_across_yield`（`test/cases/compile/stream/generators_error_binding_across_yield.cml`）在第一个 `yield` **之后**读 `e.message`：

```
func g(x: int!): string.. sync {
    match x {
        case v => { yield "ok" }
        case error e => { yield "!"; yield e.message }
    }
}
```

`g(fail())` 输出 `!` 与 `broke`，exit 0。`feat/compile/stream` 147/147 绿。

## 2 事实：`typeof` 为什么给不出它

- `PayloadOf<T!>` 是 `payloadType()`，即"值"那一格（`include/camel/core/type/composite/tuple.h:124`）；`T!` 的错误不是 payload，所以 `typeof` 主体写不出错误绑定的 cell 类型。这就是这一条当初变成内部错误的原因。
- 错误那一格**本来就有类型，而且只有一个对象**：`type::errorType()`（`tuple.h:192-194`，`{ kind: string, message: string, location: string }`，注释原文 "One object; every fallible type carries it"；design principles 3.9）。
- `TagErrorInfo` 的返回类型就是它（`src/builtin/operators/tagged.cpp:172-174`），GIR 自己也拿它比对（`src/compile/gir/builder_control.cpp:723`、`1420`）——所以这不是 GCT 单方面发明的一个类型。

## 3 事实：它还有个名字，但程序写不出来

- 名字是有的：prelude 里 `export type Error`（`stdlib/prelude.cml:39`），由 `registerNativeType("prelude", "Error", errorType())` 注册（`src/builtin/modules/globals.cpp:2396`）。
- **但用户模块里查不到这个名字**。实测原文（落地过程中试过按名字取类型，`lookupType(Reference("Error"))`）：

```
(generators_error_binding_across_yield).../generators_error_binding_across_yield.cml:20:14: [Error]:
Unresolved type reference: 'Error'. Ensure the type is defined and visible in the current scope.
(name=SemanticDiag::UnresolvedTypeReference, code=0x0300000A)
```

所以现状是：**编译器内部拿得到这个对象，程序写不出这个类型名**。这两件事的差别就是这一页剩下要裁的东西。

## 4 三条路，各带代价

| 路 | 做法 | 代价 |
|---|---|---|
| 1 | 加类型函数（`ErrorOf<T>` 一类），让 `typeof` 能写出错误那一格 | 最正；要动类型词汇表（`src/core/type/typecall.cpp` 的 `kTypeFunctions`），是**语言改动**，且有程序能写什么、已写程序怎么显示两个连带问题 |
| 2 | 错误那一格本来就是有类型的，把这份已有事实用起来 | **已落地**（§1 的对象读法）：不改词汇表、不改 GIR、不改程序能写什么。代价是这条路只在编译器内部成立——**程序仍然写不出 `Error`**；要让程序可写，得改 prelude 的导入可见性规则，那是导入那条线的事，不是生成器的 |
| 3 | 保持拒绝 | 现在不需要（该形状已能编译运行）。若哪天对象路线不可用而退回 3，**前提是诊断具名**（新增 semantic 诊断、给编号），不许是 `InternalDiag::UnknownInternalError`，且页里写明缺的是什么 |

## 5 我们的默认

**按第 2 条的对象读法落地（已落地，§1）**；第 1 条不在本车道做。理由：2 不新增词汇、只是把 GIR 已经在用的那一个 `errorType()` 拿到 GCT 的 cell 上，与"从声明里读目标类型"是同一条思路；而 1 一旦落地，`typeof` 就不再是"只看值"，要连带裁 `Error` 这个名字在用户模块里的可见性——那是导入规则的问题，应该和导入那一页一起裁，不该由一个生成器的空子顺手决定。

第 3 条作为回退保留：退回时必须具名，不许回到内部错误。

**默认落地不许做的事**：不许为了让用例过，把错误绑定的类型猜成宿主的某个类型（`string`、主体类型、`any`），也不许为它造一个只在 GCT 里存在、GIR 不认的类型。

## 6 相关

- 裁定原文：作者 2026-11-03 18:10，"绑定是局部量，跨 `yield` 就进状态"。
- 同一机制的另一半（`case v`、枚举模式绑定）：`stream.generators_match_binding_across_yield`，「A generator's `match` binding is a cell of the body」那一笔。