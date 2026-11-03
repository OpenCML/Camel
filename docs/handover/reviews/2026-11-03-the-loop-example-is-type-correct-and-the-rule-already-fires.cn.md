# 更正：`do { println(s) }` 类型合法，`LoopBodyTypeMismatch` 今天就会报

## 我们 2026-11-02 那页错了

`cc909562c` 那页的论据是：循环体出口的类型恒等于状态类型（"比较恒真"），所以"体没写出下一个状态"的程序会静默输出 `0 0 0`，连 `LoopBodyTypeMismatch` 都发不出来。**实测推翻这条论据。**

## 实测（今天，`lane-resid` 树，HEAD `6e0a5fa6c`）

**(a) 体末尾返回 `void`** —— `loop s = 0 for i in [0,1,2] do { emit(s) }`，`func emit(x: int): void`：

```
error[SemanticDiag::LoopBodyTypeMismatch]: The body of this loop gives a value of type 'void'; the state of the loop has type 'int'.
  --> v1.cml:2:40
```

**(b) 状态 `int`、体末尾 `string`** —— `do { "n" }`：同一个码，`value of type 'string'; the state … 'int'`，另带 help「The initial state fixes the state type…」。

**(c) 体的值确实驱动下一个状态，且每轮重算**：

```
loop s = 0 for i in [0,1,2] do { s + i * 10 }   ⇒ 30（s 依次 0 → 0 → 10 → 30）
loop s = 0 for i in [0,1,2] do { s + i }        ⇒ 3 （0 → 0 → 1 → 3）
loop s = 100 for i in [0,1,2] do { s }          ⇒ 100（不动点）
```

⇒ 原则 3.10 在普通 `loop` 上**今天已经成立**。

## 那个例子为什么合法

`stdlib/prelude.cml:858`：`export func println::<T>(x: T, ..rest): T` —— **`println` 返回它的参数**。

所以 `s: int` 时 `println(s): int`，与状态类型相同 ⇒ **`0 0 0` 不是"没写出下一个状态"，而是"写出的下一个状态就是它自己"**：一个不动点。这条规则落不落地，它都不会报错。

## 因此

- **不需要改语义、不需要挪比对位置**；作者 17:10 那条"照原则改写全树用例与示例"在普通 `loop` 上没有对象（`loop … do { println(s) }` 今天在任何用例与页里都不再作为"漏写"的例子出现）。
- **真正的靶子是 `void` 体与 `string` 体，今天已被正确拒绝**。
- **我们此前基于"恒真"做的三条推论全部作废**：① 体值经区域出口端口读回；② 污染点在 `expectedValueType_`（位置类型）；③ 生成器的 cell 需要"先登记名字后定型"。**作废理由是同一个**：三条都建立在"C 例是漏写"这个错误例子上，而该例合法。
- 仍然成立、且今晚已落地的是两件与循环无关的事：`GeneratorLoopBodyNeedsNextState` 是否可由 `LoopBodyTypeMismatch` 覆盖（待验证）；以及 `sync` 块里两条副作用语句之间缺 `SYNC` 边那一条（已另立页）。

## 我方默认

**不动语义。** 把 `cc909562c` 那页标为"论据作废、结论撤回"，并**保留这份更正页作为唯一记录**；`LoopBodyTypeMismatch` 的措辞与位置保持原样。