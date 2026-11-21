# 值半要不要收一个切向端口 —— 割「提升过的函数」时的那条缝

**状态**：开放。这一页写的是 2026-11-20 在 `lane-jvp-closure` 上量到的一件事，以及它岔出来的两条路。
默认已经写死在 §4，但**§5 那个岔口要先被看见**，因为两条路的签名后果不同。

## 1. 当前拒法（逐字）

```
(split_of_jvp_of___g_361____first_with_as_param_forward):?:?: [Error]: An error has occurred:
transforms: split does not build this function (the value half reads the tangent of parameter 0,
which the forward half does not take).
```

三条用例（`nested.nest_square` / `nested.nest_cube` / `nested.nest_quartic`）同一句。它是
`modules/transforms/split.cpp` 的「a tangent port」那一支：`portOf[index]` 是
`kInvalidNodeRef`，而这一半的体在读 `index`。

## 2. 读数（宿主，`-Jobs 8`，`nvm`，2026-11-20）

```
TANPORT side=forward index=1 count=2 half=1 param=0 mapsTangent=1 portType=float
        exit=((float, float), (float, float) => (float, float) pure)
TANPORT-USER edge=norm slot=3 kind=10 type=float
```

四件事：

1. `count=2, half=1`：被割的函数是**提升过的**，它的 norm 端口是 `值半 | 切向半`，`port 0` 是参数 0
   的值、`port 1` 是它的切向（`parameterOfPort`，`split.cpp:283`）。
2. `exit=((float, float), (float, float) => (float, float) pure)` = `Lift<(float, F')>` =
   `((float,float), Lift<F'>)` —— 被割的是**内层那个提升函数**，不是外面那个。
3. 读 `port 1` 的节点：`kind=10` = `Func`，而 **`type=float`**。
4. `Func` 节点的 `dataType` 是**它的答案类型**（`jvp.cpp:1920-1922` 正是按
   `dataType()->code() == TypeCode::Function` 区分「函数值」与「调用」）。答案是 `float` ⇒
   **它是调用，不是闭包值** ⇒ 那条 norm 边是**调用的实参**。

而实参里含 `ẋ` 是明写的法则（`jvp.cpp:1945-1961`）：

> the lifted callee takes the value and its tangent as two ordinary arguments (the signature is
> `(T, T) => ...`), not as one tuple

所以提升图里 `ẋ` **本来就作为被调方的参数**传进去；`ẋ` 在参数位置，这一点没有错。

## 3. 于是缺陷的名字

```
值半要「调一个提升过的被调方」，而那个调用要传 `ẋ`
⇒ 值半没有 `ẋ` 这个输入 ⇒ 它只能偷读
⇒ 看上去像「切向滑进了捕获」，而类型上不是（那是实参，不是捕获）
```

**一条被拒的边，修好之后可能仍然是那条边**：变的是它从「被拒」到「走过去」。所以判据不能是
「`TANPORT-USER` 消失」，必须是**它的拒绝原因消失**（`portOf[1]` 被声明、被正常读）。

## 4. 默认（写死，按它开工）

```
值半声明的端口 = 提升签名里已有的那些端口（值与切向各半）
它只丢切向的「结果」（出口的切向分量），不丢切向的「输入」（被调方要吃的 `ẋ`）
```

理由就是 §3：值半的体里**调用了提升过的被调方**，那些调用的实参里含 `ẋ`；不给端口，它只能偷读。

## 5. 岔口：加端口，还是让值半去调「被调方的值半」

两条路都能让 `portOf[1]` 被声明，而**签名后果不同**：

- **A（本页默认）**：值半收下 `count` 个端口（值 + 切向），出口只留值的分量。
  后果：`split` 交出的「值半」是一个**对切向有输入依赖**的函数；`lin` 的签名也随之多收切向。
  它与「函数没有切平面」不冲突（切向是**调用**的实参，不是某个函数拿到的切向量），但
  `stdlib/vjp.cml` 那条 `transpose(lin)(1.0)` 的契约要重新对一遍。
- **B**：值半里把「调提升过的被调方」换成「调它的**值半**」，于是 `ẋ` 根本不出现。
  后果：值半的签名保持「只有值」，而代价是割要**递归地**先有被调方的值半（与 `liftedCalleeOf`
  同形的一张表）。

**判据差别**：A 之后 `TANPORT-USER` 那条边**仍在**（它是实参，本来就该在）；B 之后它**消失**。
⇒ **先量一件事再选**：值半里那条调用链的**深度**——若每个被调方的值半都已在手（`split` 已经有
`liftedCallee` 那一族表），B 只差一次替换；若不在手，B 就是递归割，代价是 A 的数倍。

## 6. 这一轮被量错、被说错的地方（逐字留着）

- **「`ẋ` 滑进了捕获」**：错。`kind=10` 只说明了它是 `Func`；`type=float` 才说明它是调用。
  两列都在读数里，只用了一列。
- **「`TANPORT-USER` 该消失」**：错。它本来就该在（实参），变的是它从被拒到走过去。
- **「`Lift<F'>` 的捕获里含切向 ⇒ 值半必须收那个端口」**：这句被驳回过一次（「把现在这样实现
  当成必须这样实现」），而它**在 §3 的读法下是对的**——不是因为它现在这样实现，是因为调用要传参数。
  两句的差别在于**理由**，而理由现在是量出来的。

---

**已答（2026-11-21 移入 `reviews/`）**：答在 `questions-2026-10-29.md` 的「回 18:55 车道报告（2026-11-20 19:05）：五件裁定」一节里 `lane-jvp-closure` 的那段回报（第 6598 行，量于 `33d19e6ed`），并见同页「答 21:29 两页」（第 6651 行）：`split` 把读切向的节点放进切向半，**不把切向实参拉进值半复制**。§5 的 A/B 岔口按这两句与 `liftFrom` 一侧的解码顺序处理，A 的「值半收下切向端口再复制」不取。正文不在此复制。
