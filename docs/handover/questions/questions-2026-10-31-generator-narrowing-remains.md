# 生成器的收窄还留不留：`FeatureNotSupported('generators')` 今天没有形状可钉

**问题**：`src/compile/gct/builder.cpp` 里那条 `FeatureNotSupported('generators')` 还留不留？`origin/develop` 落地了生成器机器之后，我按作者的要求试了三种更极端的形状，**三种都被接受**（退出 0、无诊断），也就是说：这条拒绝今天没有任何形状触发它。

**为什么要问**：它是一条**说"没实现"而其实已经实现**的诊断。留下它意味着某天某个形状落进那一格时，读者会看到"这个功能还没实现"，而机器就在旁边；删掉它意味着机器收不下的形状（如果还会出现）没有名字。作者裁过"不支持的拒绝不得钉成负例"，所以这条既不能靠钉用例维持，也不能悄悄留着。

**三次实测**（合并 `origin/develop` 之后，`2404ae3d7c (clean)`，`camel <file>` 默认档）：

① 三个携带量
```
func g(n: int): int.. sync {
    loop i = 0, s = 0, t = 0 while i < n do {
        yield i + s + t
        (i + 1, s + i, t + s)
    }
}
```
读数：**`EXIT=0`，stdout 为空，stderr 为空**——被降低。

② 嵌套两个循环，各带一个量
```
func g(): int.. sync {
    loop i = 0 while i < 3 do {
        loop j = 0 while j < 3 do {
            yield i * 10 + j
            j + 1
        }
        i + 1
    }
}
```
读数：**`EXIT=0`，无输出**——被降低。

③ 携带量依赖两个前轮
```
func g(): int.. sync {
    loop a = 0, b = 1 while a < 5 do {
        yield a
        (b, a + b)
    }
}
```
读数：**`EXIT=0`，无输出**——被降低。

**同一个问题在别处的影子**：`yield` 不带值是**语法错**（`SyntaxDiag::UnexpectedToken`，不是运行时拒绝），所以"空 `yield`"也不能当这条诊断的入口。

**问作者的话**：这三种形状都被降低，`FeatureNotSupported('generators')` 今天在 `gct/builder.cpp` 里还剩什么用途？
- **A. 删掉它**，让"机器收不下的形状"在有形状时重新命名一条诊断；
- **B. 留着它**，写明它守的是"将来仍会出现、今天举不出例子的形状"——那么它就该补一句 help 说清这一点，并接受它暂时没有用例；
- **C. 换一种守法的位置**（例如改成 `YieldShapeNotLowered` 之类的类型/形状诊断），由作者定名。

**堵着谁**：我自己这一笔——`stream.yield_lowering_refused` 随 develop 一并删掉了（那条两状态 `loop` 现在被降低，由 `generators_loop_two_states.cml` 覆盖），我没有可钉的负例；`catalog.coverage_zero` 今天仍绿（别处有用例引用这条诊断），但**它引用的不是生成器**，这条诊断在生成器面上已经是空话。