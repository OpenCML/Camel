# edit/view 车道顺手挖到的两个小缺陷：`commit` 的注解不带期望类型、`@example-fragment` 的行为

2026-11-26，`lane/editview-d`。两条都不是本车道造的，都在本车道里把人绊到了；照「不改语义、不在用例里掩盖」处理，记在这里。

## 一、`let g: (x: int) => int = commit(d)` 的注解**不**提供期望类型

`commit(d)` 的签名是 `commit::<F>(d: Graph) => F!`：`F` 只出现在返回位。于是

```camel
let g: (x: int) => int = commit(d)      // 期望类型不从注解推到 F
```

不成立——注解在这一处没有参与推断。已经实测过：报的是 `F` 推不出来，而不是类型不匹配。

处理：本车道所有**非返回位置**的写法都显式给：

```camel
let g: (x: int) => int = commit::<(x: int) => int>(d) !!
```

约 **20 处**用例文件因此带上 `commit::<F>(d)` / `commit::<(x: int) => int>(d)`。

这是缺陷，不是用例该吸收的噪音：`let x: T = e` 这种形式在语言里应当提供期望类型（`let x: int = 1 + 1` 是这样用的）。修好后那 20 处可以回退成注解式，本页是它的凭据。

**不在用例里掩盖**：没有改成 `commit(d)` 再把结果强转，也没有把类型写成 `any`。

## 二、`@example-fragment` 与 `@example`

先纠正一个曾在车道里流传的错前提：「生成器只认 `@example`」。不对——生成器**读** `@example-fragment`：

* `scripts/stdlib-reference.mjs:878-879`
* `scripts/stdlib-reference.mjs:1762-1766`
* `scripts/check-doc-snippets.mjs:1352`

也就是说 `@example-fragment` 是给 `@example` 里的片段补上下文的（「`n` 是周围草稿里的一个节点」这类），两者是配对关系，不是二选一。`src/stdlib/graph.cml` 的 `erase` / `eval` / `name` 都按这个配对写：

```camel
/// @example-fragment n is a node of the surrounding draft
/// @example erase(n)
```

**待补（下一轮在 host 上取）**：`node scripts/check-doc-snippets.mjs` 对 `name(graph_of((x: int): int => x + 1))` 这类片段的**逐字**报错行还没抄下来；抄到后附在本页末尾，并说明是「片段里的短名在文档渲染后不可解析」还是「片段本身编译不过」。不先写猜测。

## 两条的共同点

都在「用例的写法」和「语言的语义」之间露出来：`commit` 那条是推断该给的没给，`@example` 那条是文档工具该读的读了但我们曾以为没读。都按缺陷记、按缺陷修，不在用例里抹平。