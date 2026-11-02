# 端口类型曾经是一个写死的 `float`

**2026-11-02，`lane-m4b`。一处改动：造被转置成员的端口时，类型来源从 `Type::Float64()` 改成被转置函数自己的形参。契约没动。**

## 改前

`buildTransposeMember` 造端口那一句：

```cpp
Type *const portType = bundlesResiduals ? function->funcType()->normTypeAt(0)
                                        : Type::Float64();
```

`plainParameters` 那一支——也就是"几个形参各占一个端口"的普通函数——**端口类型写死成 `float`**。`index_into(float[], int)` 于是得到三个 `float` 端口，而它的形参是 `float[]` 与 `int`。

4 条边不变量（节点 14 与 16 各两条，消息形态）：

```
passes a float[] as ordinary argument 0 of 'transpose_of_rulesets.jvp::index_into<float>', which takes a float
passes a int     as ordinary argument 1 of 'transpose_of_rulesets.jvp::index_into<float>', which takes a float
```

## 改后

```cpp
// The port's type is the parameter's own: a `float[]` parameter is an array port, not
// a number one. `participates` says whether a port exists; it never says what the
// port is made of.
Type *const portType = bundlesResiduals ? function->funcType()->normTypeAt(0)
                                        : function->funcType()->normTypeAt(i);
```

边不变量**归零**，同一个 callee 的 `want` 从 `float` 变回真实形参：

```
PROBE_EDGE i=1 ports=3 want=int   have=int
PROBE_EDGE i=2 ports=3 want=float have=float
```

**签名契约没有动**：`transposeSignature` 的 B 臂本来就按 `function->normTypeAt(i)` 与 `tangentSpaceOf` 建形参，代码与它自己的注释一致。缺陷只在实现里那一次写死。

## 五个"仪器形状像一个结论"的变体

同一个形状：**读数的样子被当成了结论**。

1. `payloadAs`——栈上临时量转出去，读起来像活着的；
2. `setSyncInputs`——调用了，读起来像接线了；
3. 悬垂 `span`——离开作用域前读起来还有效；
4. `bin/camel`——二进制读起来像跑的就是它，实际跑的是 `libcamel.so`；
5. **读数被截断与读数没出现看起来一样**——`Select-Object -First 12` 切掉的那一行，被当成了"没走到"。

第 5 条是这一轮自己的：我连续三轮按"`i=0` 没打印"推论"那一格没走到"，而打印在无条件位置，是截断把它切掉了。**先确认仪器在哪个分支里、输出有没有被截断，再解释它为什么沉默。**

## 状态

`feat.compile.transforms` = `total: 50, pass: 49, fail: 1`。剩下那条红是 `grad_of_an_array_parameter`：

```
[INFO] expected exit 0, got null
[INFO] stdout does not contain '[2.0, 3.0]'
[stdout] 6.0
```

`6.0` 是前向值（3.0 × 2.0）。**边不变量已经不再是这条用例的问题**：剩下的缺口是数值与退出码，另案处理。