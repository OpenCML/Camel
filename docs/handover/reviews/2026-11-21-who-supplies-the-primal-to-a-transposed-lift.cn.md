# 谁把原值交给「线性函数的提升」的转置

## 一、一句话

`transpose` 现在能读「线性函数的提升」了，而它读出的成员需要一个**运行期的原值**做算子的参数——那个原值不在签名里、也不在闭包里。问：它从哪来。

## 二、读数（`develop = 769fb5daa`，逐字）

前面几层都已经通了。同一个用例，墙一步一步退到这里：

```
TRANSPOSE function=jvp_of_split_of_jvp_of_sq_backward skip=call-value-missing param=2 node=4 arg=0
transforms: transpose has no transpose for this function: the callee's transpose was built,
            but the parameter at position 3 reads a value this call does not carry.
```

`RuntimeDiag::RuntimeError`，层是 `transpose.cpp#buildTransposeMember` 的 `unforwardedCause`。

签名那一层已经不是墙了：

```
成员实测   places=TT signature=((f32, f32)) => (f32, f32)
Transpose<F>        resolved=((f32, f32)) => (f32, f32)
⇒ ports=1 promised=1
```

要的东西很具体：提升体里 `jvp_of_autodiff::mul_tangent<f32>` 的伴随法则要 `dx` 这个**原值**当算子。签名说不出口（类型里没有「这是线性」的位置），闭包里也没有它（`REINTERP split` 那边 `captures=... residuals=1`，存的是**函数**不是值）。

## 三、它为什么不是「改个读法」就能解决

已经改过的那一层是**读法**：一个签名若其提升列表没有 `()` 槽（`()` 是法则给「该参数无切空间」留的记录），则源函数只吃切向，于是按普通读法读、value half 不作端口。这条成立，`ports == promised` 就自然相等了。

而剩下的那一步不是读法问题：**转置一个函数值，拿到的是它的一次调用；而这里要的是那次调用里某个位置上的原值**。`transpose(f)` 手上只有 `f`，没有「`f` 被调时第 3 个实参是几」。

## 四、两条路

### A：`transpose` 的调用点把原值当**额外实参**递进去

- 形状：`transpose(f)` 产出的成员，其端口表里增加「原值」位置；调用点按名字填。
- 代价：**用户可见的写法变了**——`transpose(f)` 之后调用它要多给一个实参。这与 `fsh-dev` 上 07:25 那条裁定的方向冲突（「用户可见的写法不变」）。
- 好处：不需要新的存储，原值就是调用点手上的东西。

### B：`transpose` 拒，而这条链的答案就是「拒」

- 形状：把 `compositions.jvp_vjp_f` 的 `known_fail` 永久保留，`known_fail_reason` 钉在现在这面墙。
- 代价：**二阶导的「先 split 再 lift 再 transpose」这条组合没被支持**——而它正是 JAX `linearize` 能做的事。
- 好处：不需要新机制；`new red = 0` 保持。

### C：`jvp` 不把线性函数值提升成 `Lift`

- 形状：在提升法则里认出「这个源函数只吃切向」，于是**不提升**它的值半边。
- 代价：动 `Lift` 的普遍法则（`liftSignature` 是唯一入口，改它影响所有提升）。今晚已明确「未授权」。
- 好处：从源头消掉多出的那一口，`transpose` 什么都不用改。

## 五、默认建议

**B（拒，并把这面墙钉住）。**

三条理由，都可测：

1. **A 与 07:25 那条裁定直接冲突。** 那条写死「用户可见的写法不变（`transpose(f)` 不多一个参数）」。A 型的代价不是「多一个参数」，是**把这个不变式反过来了**——而那是裁定的边界，不是我能在派工里改的。
2. **C 的代价今晚已经被量过一次。** `liftSignature` 是唯一入口，改它影响所有提升；而 `liftedParametersOf` 今晚刚被立成「唯一读者」，动它就是动那条线。裁定原文：「若实现 B 时发现它们撞上同一处，带读数再问」——现在读数在这里。
3. **B 不引入任何新事实。** `compositions.jvp_vjp_f` 已经在 `known_fail` 里、`known_fail_ends_when` 已经写着期望输出（`1.0 / 2.0 / 3.0`）、`new red = 0` 已经成立。选 B 的唯一动作是把 reason 钉在现在这面墙，而它现在是准的。

## 六、若选 B，落地的动作是

```
compositions.jvp_vjp_f 两条的 known_fail_reason 保持现在这面墙
（层 transpose.cpp#buildTransposeMember 的 unforwardedCause、
  diag RuntimeDiag::RuntimeError、param=2 node=4 arg=0）
known_fail_ends_when 保持 1.0 / 2.0 / 3.0
本页移入 docs/handover/reviews/，并在其中记一句：
  「二阶导的 split→lift→transpose 组合不支持，因为转置一个函数值拿不到它被调时的原值」
```

## 七、不在本页

- `Pullback<F>` 要不要跟着变（那是 R2 的后一步）。
- `Jvp<F>` / `Vjp<F>` 的提升读法是否要同步（若它们撞上同一处，带读数再问）。
## 八、收（决策者答 2026-11-21 08:20）

**按 B 收。** `compositions.jvp_vjp_f` 保持 `known_fail`，`known_fail_ends_when` 仍是期望输出 `1.0 / 2.0 / 3.0`，不钉拒句。A、C 都不做。

而 B 不是这条链的答案，只是停在这里。问题出在**组合的次序**，不在 `transpose` 缺一个实参：外层 `jvp` 穿过 `transpose(lin)` 时，现在的做法是「先提升 `lin` 再转置」，于是转置的对象是 `jvp(lin)`——它把 `lin` 的残差（原值 `x`）当成会变的量，残差的切向就以「原值」的身份出现在要转置的体里。

数学上外层要的是

```
jvp(transpose(L))(ct, dct) = (L^T ct,  L^T dct + (dL/dx · dx)^T ct)
```

即「`transpose` 这个算子自己的前向法则」：第一项是同一个转置，第二项里残差的切向 `dx` 作为**捕获值**参与，而不是作为被转置函数的端口。这需要先定「`jvp` 遇到 `transpose(L)` 调用时用哪条法则、残差的切向从哪来」，是变换设计的事，归决策者：写成设计稿后再派。
